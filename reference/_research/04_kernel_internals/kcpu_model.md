# KCPU Model

The kernel side KCPU command queue, its state, and how it processes the
thirteen command types.

## KCPU queue structure

struct kbase_kcpu_command_queue {
    struct kbase_context *kctx;
    struct kbase_kcpu_command commands[256];
    struct work_struct work;
    u8 start_offset;
    u8 id;
    u16 num_pending_cmds;
    u32 cqs_wait_count;
    u64 fence_context;
    unsigned int fence_seqno;
    bool fence_wait_processed;
    bool enqueue_failed;
    bool command_started;
    struct list_head jit_blocked;
    bool has_error;
};

The commands array is a circular buffer of 256 entries. start_offset
points to the next command to execute. num_pending_cmds tracks how many
commands are queued but not yet completed.

The work item is scheduled whenever there is work to do. Processing is
asynchronous and runs in a kernel workqueue.

## Command array size

#define KBASEP_KCPU_QUEUE_SIZE ((size_t)256)

At most 256 commands can be pending per queue. Enqueue operations block
if this limit is reached.

## Limits

Up to 256 KCPU queues per context (identifier is u8).

## Per-command structures

Each command type has a specific info struct in the kernel. They mirror
the userspace structures but hold kernel pointers and additional state.

Fence:
  struct kbase_kcpu_command_fence_info
    dma_fence_cb    completion callback
    dma_fence *     the fence object
    kcpu_queue *    back pointer

CQS set:
  struct kbase_kcpu_command_cqs_set_info
    base_cqs_set *  array of objects
    nr_objs         count

CQS wait:
  struct kbase_kcpu_command_cqs_wait_info
    base_cqs_wait_info *  array of waits
    unsigned long *       signaled bitmap
    nr_objs               count
    inherit_err_flags     error propagation

CQS set operation (timeline):
  struct kbase_kcpu_command_cqs_set_operation_info

CQS wait operation (timeline):
  struct kbase_kcpu_command_cqs_wait_operation_info

JIT alloc:
  struct kbase_kcpu_command_jit_alloc_info
    list_head       node in submission order list
    base_jit_alloc_info *  array of allocations
    u8              count
    bool            blocked

JIT free:
  struct kbase_kcpu_command_jit_free_info
    list_head       node in submission order list
    u8 *            array of ids
    u8              count

The list_head is at the front of both JIT structures so they can share
a common list.

Suspend buffer:
  struct kbase_kcpu_command_group_suspend_info
    kbase_suspend_copy_buffer *  user buffer info
    u8                            group handle

Import:
  struct kbase_kcpu_command_import_info
    u64  gpu_va

## Processing model

When a KCPU queue has work, the kernel schedules the work item. The
work item processes commands in order from start_offset.

A command may block if its dependency is not satisfied. When blocked,
processing stops until the dependency resolves. Later commands in the
queue wait.

Fence wait commands block on a dma_fence callback. When the fence
signals, the callback resumes processing.

CQS wait commands block until all their CQS objects satisfy their
conditions. A signaled bitmap tracks which objects have signaled.

JIT alloc commands may block if the kernel cannot satisfy the
allocation. The queue is added to a jit_blocked list. When memory
becomes available (via JIT free or via eviction), the kernel retries.

## Command ordering

KCPU commands are processed strictly in submission order. There is no
reordering. A blocked command blocks all commands after it.

This is a key property. Manvil must assume that if a CQS wait never
satisfies, the KCPU queue is stuck and all subsequent commands
(including signal and free operations) are stuck too.

## Timeout requirement

The kernel does not have a built-in timeout for KCPU commands. If a
command blocks forever, the queue stays blocked until the context is
destroyed or the GPU is reset.

Manvil must add a timeout on the userspace side. A blocked KCPU queue
should be detected and reported within a reasonable bound.

Suggested default: five seconds for a single enqueue operation that
returns a result. Higher for operations that depend on external events.

## Fence handling

Each KCPU queue has its own fence context (a unique u64) and a fence
sequence number that increments on each FENCE_SIGNAL.

Fences created by this queue are visible to userspace as file
descriptors. Userspace can wait on them from outside the GPU.

This is the mechanism used by Vulkan for VkFence and for external
synchronization with other subsystems (display, video decode, camera).

## JIT blocking model

When a JIT allocation cannot be satisfied immediately, the kernel
adds the queue to a global jit_blocked_queues list and retries when
memory becomes available.

Freeing another JIT allocation triggers a retry. Eviction of inactive
allocations also triggers a retry.

The kernel guarantees that JIT commands are processed in submission
order across all queues in a context. This prevents priority inversion
between JIT allocations.

## Error state

Each KCPU queue has a has_error flag. When a command fails, the kernel
may set this flag. Subsequent commands may be rejected until the flag
is cleared.

The exact semantics depend on the command type. Some errors are
recoverable, others are not.

Manvil should treat any KCPU queue error as a signal to recreate the
queue.

## Userspace API

Three ioctls operate on KCPU queues:

  KBASE_IOCTL_KCPU_QUEUE_CREATE  (ioctl 45)
  KBASE_IOCTL_KCPU_QUEUE_DELETE  (ioctl 46)
  KBASE_IOCTL_KCPU_QUEUE_ENQUEUE (ioctl 47)

Create returns an id (u8). Delete takes an id. Enqueue takes an array
of base_kcpu_command structs, a count, and an id.

The userspace structure base_kcpu_command is 24 bytes. It contains a
type byte, padding to 8 bytes, and a 16-byte union with the payload.

The kernel copies the array into its own commands array. The userspace
can then reuse its buffer.
