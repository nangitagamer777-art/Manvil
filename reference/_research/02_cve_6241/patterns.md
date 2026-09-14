# Usage Patterns from CVE-2023-6241

The exploit source code demonstrates the correct sequence of ioctls for a
full CSF initialization and submission cycle. The patterns below are
extracted from that code and are the basis for Manvil's kernel API layer.

## Pattern 1: Device handshake

int fd = open("/dev/mali0", O_RDWR);

struct kbase_ioctl_version_check vc = {0};
ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &vc);

struct kbase_ioctl_set_flags flags = {0};
flags.create_flags = group_id << 3;
ioctl(fd, KBASE_IOCTL_SET_FLAGS, &flags);

void *tracking = mmap(NULL, 0x1000, 0, MAP_SHARED, fd,
                      BASE_MEM_MAP_TRACKING_HANDLE);

Notes:
  VERSION_CHECK accepts a zeroed struct. The kernel fills in its version.
  SET_FLAGS encodes the MMU group ID in bits 3 through 6.
  The tracking page is mapped with prot = 0, only to reserve the address.

## Pattern 2: Memory allocation

union kbase_ioctl_mem_alloc alloc = {0};
alloc.in.flags = BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_GPU_RD |
                 BASE_MEM_PROT_CPU_WR | (group_id << 22);
alloc.in.va_pages = pages;
alloc.in.commit_pages = pages;
ioctl(fd, KBASE_IOCTL_MEM_ALLOC, &alloc);

void *region = mmap(NULL, 0x1000 * pages, PROT_READ | PROT_WRITE,
                    MAP_SHARED, fd, alloc.out.gpu_va);

Notes:
  The cookie returned in out.gpu_va is also the GPU VA under SAME_VA.
  It is used directly as the mmap offset.
  The CPU pointer returned by mmap equals the GPU VA.

## Pattern 3: Queue registration and binding

struct kbase_ioctl_cs_queue_register reg = {0};
reg.buffer_gpu_addr = queue_addr;
reg.buffer_size = queue_bytes;
ioctl(fd, KBASE_IOCTL_CS_QUEUE_REGISTER, &reg);

union kbase_ioctl_cs_queue_bind bind = {0};
bind.in.buffer_gpu_addr = queue_addr;
bind.in.group_handle = group_handle;
bind.in.csi_index = csi_index;
ioctl(fd, KBASE_IOCTL_CS_QUEUE_BIND, &bind);

void *user_io = mmap(NULL, 0x3000, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, bind.out.mmap_handle);

Notes:
  The bind returns a mmap handle for three pages: input, output, and
  hardware doorbell.
  csi_index selects the Command Stream Interface within the group.

## Pattern 4: KCPU command enqueue

uint8_t qid;
struct kbase_ioctl_kcpu_queue_new qnew = {0};
ioctl(fd, KBASE_IOCTL_KCPU_QUEUE_CREATE, &qnew);
qid = qnew.id;

// Prepare the specific command payload in shared memory.
struct base_jit_alloc_info info = {0};
// ... fill info ...

// Wrap it in a generic base_kcpu_command.
struct base_kcpu_command_jit_alloc_info jit_alloc = {0};
jit_alloc.info = (uint64_t)&info;
jit_alloc.count = 1;

struct base_kcpu_command cmd = {0};
cmd.type = BASE_KCPU_COMMAND_TYPE_JIT_ALLOC;
cmd.info.jit_alloc = jit_alloc;

// Enqueue.
struct kbase_ioctl_kcpu_queue_enqueue enq = {0};
enq.id = qid;
enq.nr_commands = 1;
enq.addr = (uint64_t)&cmd;
ioctl(fd, KBASE_IOCTL_KCPU_QUEUE_ENQUEUE, &enq);

// The kernel writes the result to shared memory. Poll for it.
volatile uint64_t *result = (volatile uint64_t *)gpu_alloc_addr;
while (*result == 0) {
    // busy wait
}
uint64_t final = *result;

Notes:
  The exploit uses an unbounded busy wait. A timeout must be added.
  A command array can carry multiple commands per enqueue.

## Pattern 5: JIT memory allocation

struct kbase_ioctl_mem_jit_init init = {0};
init.va_pages = va_pages;
init.max_allocations = 255;
init.trim_level = trim_level;
init.group_id = group_id;
init.phys_pages = va_pages;
ioctl(fd, KBASE_IOCTL_MEM_JIT_INIT, &init);

// Then allocate within the JIT region using KCPU JIT_ALLOC commands.
// See Pattern 4.

Notes:
  JIT provides on-demand physical page population for large regions.
  Each allocation has a unique id (u8) used for free.
  The gpu_alloc_addr field points to user memory where the kernel writes
  the resulting GPU VA.
