# CSF Register Map

Complete map of the CSF register blocks, their offsets, and their
semantics. Derived from mali_kbase_csf_registers.h.

## Memory layout

Register blocks are at fixed addresses in the CSF memory space:

DOORBELLS_BASE          0x0080000
GLB_CONTROL_BLOCK_BASE  0x04000000
USER_BASE               0x0010000

Individual blocks like CS_KERNEL_INPUT_BLOCK, CS_USER_INPUT_BLOCK, and
CSG_INPUT_BLOCK have base 0x0000 because they are mapped per queue or
per group. Their offsets are relative to their own base.

## Doorbells

DOORBELLS_BASE          0x0080000
DOORBELL_0              0x0000
DOORBELL(n)             DOORBELL_0 + n * 65536
DOORBELL_COUNT          1024
DB_BLK_DOORBELL         0x0000

A doorbell is triggered by writing a u32 to
DOORBELL(n) + DB_BLK_DOORBELL. The hardware detects the write and
notifies the firmware. This is the fast path for kicking a queue.

## User register page

USER_BASE               0x0010000
LATEST_FLUSH            0x0000

LATEST_FLUSH is a flush identifier updated by the firmware after a
cache clean and invalidate. The userspace maps this page in every
process to read the current flush state.

## Queue register blocks

### CS_USER_INPUT_BLOCK (mapped at user IO offset 0)

CS_INSERT_LO            0x0000   current insert offset, low word
CS_INSERT_HI            0x0004   current insert offset, high word
CS_EXTRACT_INIT_LO      0x0008   initial extract offset, low word
CS_EXTRACT_INIT_HI      0x000C   initial extract offset, high word

The CS_INSERT value is written by the userspace to announce new commands
in the ring buffer. The firmware reads from CS_EXTRACT to CS_INSERT.

### CS_USER_OUTPUT_BLOCK (mapped at user IO offset 4096)

CS_EXTRACT_LO           0x0000   current extract offset, low word
CS_EXTRACT_HI           0x0004   current extract offset, high word
CS_ACTIVE               0x0008   hardware active flag

CS_EXTRACT is updated by the firmware as it consumes commands. The
userspace reads it to know how much room is available in the ring.

### CS_KERNEL_INPUT_BLOCK (kernel configures, userspace reads for debug)

CS_REQ                  0x0000   request flags
CS_CONFIG               0x0004   stream configuration
CS_ACK_IRQ_MASK         0x000C   interrupt mask
CS_BASE_LO              0x0010   ring buffer base, low
CS_BASE_HI              0x0014   ring buffer base, high
CS_SIZE                 0x0018   ring buffer size
CS_TILER_HEAP_START_LO  0x0020   heap start pointer, low
CS_TILER_HEAP_START_HI  0x0024   heap start pointer, high
CS_TILER_HEAP_END_LO    0x0028   heap end pointer, low
CS_TILER_HEAP_END_HI    0x002C   heap end pointer, high
CS_USER_INPUT_LO        0x0030   user input page VA, low
CS_USER_INPUT_HI        0x0034   user input page VA, high
CS_USER_OUTPUT_LO       0x0038   user output page VA, low
CS_USER_OUTPUT_HI       0x003C   user output page VA, high
CS_INSTR_CONFIG         0x0040   instrumentation config
CS_INSTR_BUFFER_SIZE    0x0044
CS_INSTR_BUFFER_BASE_LO 0x0048
CS_INSTR_BUFFER_BASE_HI 0x004C

### CS_KERNEL_OUTPUT_BLOCK

CS_ACK                  0x0000   acknowledge flags
CS_STATUS_CMD_PTR_LO    0x0040   program counter, low
CS_STATUS_CMD_PTR_HI    0x0044   program counter, high
CS_STATUS_WAIT          0x0048   wait condition status
CS_STATUS_REQ_RESOURCE  0x004C   requested resources
CS_STATUS_WAIT_SYNC_POINTER_LO 0x0050
CS_STATUS_WAIT_SYNC_POINTER_HI 0x0054
CS_STATUS_WAIT_SYNC_VALUE 0x0058
CS_STATUS_SCOREBOARDS   0x005C
CS_STATUS_BLOCKED_REASON 0x0060
CS_FAULT                0x0080
CS_FATAL                0x0084
CS_FAULT_INFO_LO        0x0088
CS_FAULT_INFO_HI        0x008C
CS_FATAL_INFO_LO        0x0090
CS_FATAL_INFO_HI        0x0094
CS_HEAP_VT_START        0x00C0
CS_HEAP_VT_END          0x00C4
CS_HEAP_FRAG_END        0x00CC
CS_HEAP_ADDRESS_LO      0x00D0
CS_HEAP_ADDRESS_HI      0x00D4

## Blocked reason values

CS_STATUS_BLOCKED_REASON_REASON_UNBLOCKED      0x0
CS_STATUS_BLOCKED_REASON_REASON_WAIT           0x1
CS_STATUS_BLOCKED_REASON_REASON_PROGRESS_WAIT  0x2
CS_STATUS_BLOCKED_REASON_REASON_SYNC_WAIT      0x3
CS_STATUS_BLOCKED_REASON_REASON_DEFERRED       0x4
CS_STATUS_BLOCKED_REASON_REASON_RESOURCE       0x5
CS_STATUS_BLOCKED_REASON_REASON_FLUSH          0x6

## CS_REQ flags

State field (bits 0-2):
CS_REQ_STATE_STOP       0x0
CS_REQ_STATE_START      0x1

CS_REQ_EXTRACT_EVENT            bit 4
CS_REQ_IDLE_SYNC_WAIT           bit 8
CS_REQ_IDLE_PROTM_PEND          bit 9
CS_REQ_IDLE_EMPTY               bit 10
CS_REQ_IDLE_RESOURCE_REQ        bit 11
CS_REQ_TILER_OOM                bit 26
CS_REQ_PROTM_PEND               bit 27
CS_REQ_FATAL                    bit 30
CS_REQ_FAULT                    bit 31

CS_REQ_TILER_OOM is written by the firmware when the tiler heap cannot
grow further. If the group was created with the TILER_OOM exception
handler flag, this bit signals the userspace to act. Otherwise the
kernel terminates the group.

## Stream configuration

CS_CONFIG_PRIORITY              bits 0-3
CS_CONFIG_USER_DOORBELL         bits 8-15

CS_CONFIG_USER_DOORBELL is the index of the doorbell assigned to this
queue. The userspace must read this value to know where to write for
kicks.

## CS_STATUS_WAIT fields

CS_STATUS_WAIT_SB_MASK                  bits 0-15
CS_STATUS_WAIT_SYNC_WAIT_CONDITION      bits 24-27
CS_STATUS_WAIT_PROGRESS_WAIT            bit 28
CS_STATUS_WAIT_PROTM_PEND               bit 29
CS_STATUS_WAIT_SYNC_WAIT                bit 31

SYNC_WAIT_CONDITION values:
  LE (less or equal)  0x0
  GT (greater than)   0x1

The sync wait condition matches the CQS wait operations used by the KCPU.

## CS_STATUS_REQ_RESOURCE fields

COMPUTE_RESOURCES       bit 0
FRAGMENT_RESOURCES      bit 1
TILER_RESOURCES         bit 2
IDVS_RESOURCES          bit 3

IDVS is index driven vertex shading. It is the modern vertex processing
path used together with the tiler.

## CS_FAULT_EXCEPTION_TYPE values

KABOOM                              0x05
CS_RESOURCE_TERMINATED              0x0F
CS_BUS_FAULT                        0x48
CS_INHERIT_FAULT                    0x4B
INSTR_INVALID_PC                    0x50
INSTR_INVALID_ENC                   0x51
INSTR_BARRIER_FAULT                 0x55
DATA_INVALID_FAULT                  0x58
TILE_RANGE_FAULT                    0x59
ADDR_RANGE_FAULT                    0x5A
IMPRECISE_FAULT                     0x5B
RESOURCE_EVICTION_TIMEOUT           0x69

## CS_FATAL_EXCEPTION_TYPE values

CS_CONFIG_FAULT                     0x40
CS_ENDPOINT_FAULT                   0x44
CS_BUS_FAULT                        0x48
CS_INVALID_INSTRUCTION              0x49
CS_CALL_STACK_OVERFLOW              0x4A
FIRMWARE_INTERNAL_ERROR             0x68

## Heap counters

CS_HEAP_VT_START         number of vertex or tiling operations started
CS_HEAP_VT_END           number of vertex or tiling operations completed
CS_HEAP_FRAG_END         number of fragment operations completed
CS_HEAP_ADDRESS          pointer to the current heap context

The kernel validates the heap counters on each grow request. Inconsistent
values cause the group to be terminated.

## CSG register blocks

### CSG_INPUT_BLOCK

CSG_REQ                 0x0000
CSG_ACK_IRQ_MASK        0x0004
CSG_DB_REQ              0x0008
CSG_IRQ_ACK             0x000C
CSG_ALLOW_COMPUTE_LO    0x0020
CSG_ALLOW_COMPUTE_HI    0x0024
CSG_ALLOW_FRAGMENT_LO   0x0028
CSG_ALLOW_FRAGMENT_HI   0x002C
CSG_ALLOW_OTHER         0x0030
CSG_EP_REQ              0x0034
CSG_SUSPEND_BUF_LO      0x0040
CSG_SUSPEND_BUF_HI      0x0044
CSG_PROTM_SUSPEND_BUF_LO 0x0048
CSG_PROTM_SUSPEND_BUF_HI 0x004C
CSG_CONFIG              0x0050
CSG_ITER_TRACE_CONFIG   0x0054

### CSG_REQ fields

State field (bits 0-2):
CSG_REQ_STATE_TERMINATE 0x0
CSG_REQ_STATE_START     0x1
CSG_REQ_STATE_SUSPEND   0x2
CSG_REQ_STATE_RESUME    0x3

CSG_REQ_EP_CFG                  bit 4
CSG_REQ_STATUS_UPDATE           bit 5
CSG_REQ_SYNC_UPDATE             bit 28
CSG_REQ_IDLE                    bit 29
CSG_REQ_DOORBELL                bit 30
CSG_REQ_PROGRESS_TIMER_EVENT    bit 31

### CSG_EP_REQ fields

CSG_EP_REQ_COMPUTE_EP       bits 0-7
CSG_EP_REQ_FRAGMENT_EP      bits 8-15
CSG_EP_REQ_TILER_EP         bits 16-19
CSG_EP_REQ_EXCLUSIVE_COMPUTE bit 20
CSG_EP_REQ_EXCLUSIVE_FRAGMENT bit 21
CSG_EP_REQ_PRIORITY         bits 28-31

### CSG_OUTPUT_BLOCK

CSG_ACK                 0x0000
CSG_DB_ACK              0x0008
CSG_IRQ_REQ             0x000C
CSG_STATUS_EP_CURRENT   0x0010
CSG_STATUS_EP_REQ       0x0014
CSG_RESOURCE_DEP        0x001C

## Global control block

GLB_VERSION             0x0000
GLB_FEATURES            0x0004
GLB_INPUT_VA            0x0008
GLB_OUTPUT_VA           0x000C
GLB_GROUP_NUM           0x0010
GLB_GROUP_STRIDE        0x0014
GLB_PRFCNT_SIZE         0x0018
GLB_INSTR_FEATURES      0x001C
GROUP_CONTROL_0         0x1000
GROUP_CONTROL(n)        GROUP_CONTROL_0 + n * 256
GROUP_CONTROL_COUNT     16

### GLB_VERSION fields

PATCH           bits 0-15
MINOR           bits 16-23
MAJOR           bits 24-31

### Group control block

GROUP_FEATURES           0x0000
GROUP_INPUT_VA           0x0004
GROUP_OUTPUT_VA          0x0008
GROUP_SUSPEND_SIZE       0x000C
GROUP_PROTM_SUSPEND_SIZE 0x0010
GROUP_STREAM_NUM         0x0014
GROUP_STREAM_STRIDE      0x0018
STREAM_CONTROL_0         0x0040
STREAM_CONTROL(n)        STREAM_CONTROL_0 + n * 12
STREAM_CONTROL_COUNT     16

### Stream control block

STREAM_FEATURES          0x0000
STREAM_INPUT_VA          0x0004
STREAM_OUTPUT_VA         0x0008

### STREAM_FEATURES fields

WORK_REGISTERS          bits 0-7
SCOREBOARDS             bits 8-15
COMPUTE                 bit 16
FRAGMENT                bit 17
TILER                   bit 18

## Global input block

GLB_REQ                 0x0000
GLB_ACK_IRQ_MASK        0x0004
GLB_DB_REQ              0x0008
GLB_PROGRESS_TIMER      0x0010
GLB_PWROFF_TIMER        0x0014
GLB_ALLOC_EN_LO         0x0018
GLB_ALLOC_EN_HI         0x001C

### GLB_REQ fields

GLB_REQ_HALT                    bit 0
GLB_REQ_CFG_PROGRESS_TIMER      bit 1
GLB_REQ_CFG_ALLOC_EN            bit 2
GLB_REQ_CFG_PWROFF_TIMER        bit 3
GLB_REQ_PROTM_ENTER             bit 4
GLB_REQ_PRFCNT_ENABLE           bit 5
GLB_REQ_PRFCNT_SAMPLE           bit 6
GLB_REQ_COUNTER_ENABLE          bit 7
GLB_REQ_PING                    bit 8
GLB_REQ_FIRMWARE_CONFIG_UPDATE  bit 9
GLB_REQ_SLEEP                   bit 12
GLB_REQ_INACTIVE_COMPUTE        bit 20
GLB_REQ_INACTIVE_FRAGMENT       bit 21
GLB_REQ_INACTIVE_TILER          bit 22
GLB_REQ_PROTM_EXIT              bit 23
GLB_REQ_DEBUG_CSF_REQ           bit 30
GLB_REQ_DEBUG_HOST_REQ          bit 31

GLB_REQ_PING is a health check of the firmware. If the firmware does not
acknowledge within the firmware timeout, the kernel initiates a reset.
