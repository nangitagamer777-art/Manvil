# KCPU Reference Implementation Notes

Observed usage of the KCPU command queue from the CVE-2023-6241 exploit
source. This document records what the implementation does, in the order
it does it.

## Creating a KCPU queue

uint8_t qid;
struct kbase_ioctl_kcpu_queue_new qnew = {0};
ioctl(fd, KBASE_IOCTL_KCPU_QUEUE_CREATE, &qnew);
qid = qnew.id;

The ioctl returns a single byte identifier. Up to 256 KCPU queues can
exist per process. Manvil typically needs one.

## Shared result memory

Before enqueueing commands that produce a result, the caller must
provide a location where the kernel can write the result. The result
memory must be GPU accessible and mappable from the CPU at the same
time.

void *gpu_alloc_addr = map_gpu(mali_fd, 1, 1, false, 0);
memset(gpu_alloc_addr, 0, 0x1000);

The exploit uses a single page for all results. Manvil can allocate one
or more pages depending on concurrency requirements.

## JIT allocation flow

Step 1. Prepare the specific allocation info in shared memory.

struct base_jit_alloc_info info = {0};
info.id = jit_id;
info.gpu_alloc_addr = gpu_alloc_addr;
info.va_pages = va_pages;
info.commit_pages = commit_pages;
info.extension = 1;
info.bin_id = bin_id;
info.usage_id = usage_id;

Step 2. Wrap it in the KCPU command.

struct base_kcpu_command_jit_alloc_info jit_alloc = {0};
jit_alloc.info = (uint64_t)&info;
jit_alloc.count = 1;

struct base_kcpu_command cmd = {0};
cmd.type = BASE_KCPU_COMMAND_TYPE_JIT_ALLOC;
cmd.info.jit_alloc = jit_alloc;

Step 3. Enqueue.

struct kbase_ioctl_kcpu_queue_enqueue enq = {0};
enq.id = qid;
enq.nr_commands = 1;
enq.addr = (uint64_t)&cmd;
ioctl(fd, KBASE_IOCTL_KCPU_QUEUE_ENQUEUE, &enq);

Step 4. Wait for the result.

*((uint64_t*)gpu_alloc_addr) = 0;

volatile uint64_t ret = *((uint64_t*)gpu_alloc_addr);
while (ret == 0) {
    ret = *((uint64_t*)gpu_alloc_addr);
}
return ret;

The kernel writes the resulting GPU VA to gpu_alloc_addr. The caller
polls until the value changes.

The exploit uses an unbounded busy wait. Manvil must add a timeout.

## JIT free flow

Step 1. Prepare the free info.

uint8_t free_id = jit_id;
struct base_kcpu_command_jit_free_info info = {0};
info.ids = (uint64_t)&free_id;
info.count = 1;

Step 2. Wrap and enqueue.

struct base_kcpu_command cmd = {0};
cmd.type = BASE_KCPU_COMMAND_TYPE_JIT_FREE;
cmd.info.jit_free = info;

struct kbase_ioctl_kcpu_queue_enqueue enq = {0};
enq.id = qid;
enq.nr_commands = 1;
enq.addr = (uint64_t)&cmd;
ioctl(fd, KBASE_IOCTL_KCPU_QUEUE_ENQUEUE, &enq);

JIT_FREE does not produce a result in the same way JIT_ALLOC does. The
caller does not need to poll.

## Generalization to other commands

The same pattern applies to all 13 KCPU command types:

1. Prepare the specific info structure in shared memory.
2. Wrap it in a base_kcpu_command with the correct type tag.
3. Enqueue through KBASE_IOCTL_KCPU_QUEUE_ENQUEUE.
4. If the command produces a result, poll the shared memory address.

The differences between commands are only in the specific info structure
and whether a result is produced.

## Manvil implementation notes

Manvil should provide one wrapper per command type in the kernel_api
layer:

manvil_kcpu_fence_signal()
manvil_kcpu_fence_wait()
manvil_kcpu_cqs_wait()
manvil_kcpu_cqs_set()
manvil_kcpu_cqs_wait_operation()
manvil_kcpu_cqs_set_operation()
manvil_kcpu_map_import()
manvil_kcpu_unmap_import()
manvil_kcpu_unmap_import_force()
manvil_kcpu_jit_alloc()
manvil_kcpu_jit_free()
manvil_kcpu_group_suspend()
manvil_kcpu_error_barrier()

Each wrapper:

1. Validates input.
2. Allocates or reuses a shared memory slot for the result.
3. Builds the specific info and the base command.
4. Enqueues via ioctl.
5. Waits with a bounded timeout.
6. Returns the result or an error.

The timeout applies to commands that expect a kernel response. The
value should be configurable and default to a reasonable bound such as
five seconds.
