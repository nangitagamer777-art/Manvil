# KCPU Commands

KCPU is the kernel-side command queue used to control the CSF firmware. It
is separate from the user command queues that carry graphics and compute
work.

## Lifecycle

1. KBASE_IOCTL_KCPU_QUEUE_CREATE returns a base_kcpu_queue_id (u8).
2. Commands are placed in a user-visible array of base_kcpu_command structs.
3. KBASE_IOCTL_KCPU_QUEUE_ENQUEUE passes the array to the kernel.
4. KBASE_IOCTL_KCPU_QUEUE_DELETE releases the queue.

Up to 256 KCPU queues per context.

## Command types

From the base_kcpu_command_type enum:

0  FENCE_SIGNAL
1  FENCE_WAIT
2  CQS_WAIT
3  CQS_SET
4  CQS_WAIT_OPERATION
5  CQS_SET_OPERATION
6  MAP_IMPORT
7  UNMAP_IMPORT
8  UNMAP_IMPORT_FORCE
9  JIT_ALLOC
10 JIT_FREE
11 GROUP_SUSPEND
12 ERROR_BARRIER

The earlier fork implementation covered ten of these. FENCE_SIGNAL,
CQS_WAIT, and CQS_WAIT_OPERATION were not implemented. Manvil should cover
all thirteen.

## CQS operations

CQS objects are user-visible u32 or u64 values in GPU-accessible memory.
They act as timeline counters.

Data types:
  BASEP_CQS_DATA_TYPE_U32 = 0
  BASEP_CQS_DATA_TYPE_U64 = 1

Wait operations:
  BASEP_CQS_WAIT_OPERATION_LE = 0  wait while value <= target
  BASEP_CQS_WAIT_OPERATION_GT = 1  wait while value > target

Set operations:
  BASEP_CQS_SET_OPERATION_ADD = 0  increment value
  BASEP_CQS_SET_OPERATION_SET = 1  set value

Limit: BASEP_KCPU_CQS_MAX_NUM_OBJS = 32 objects per CQS command.

## Command structure

base_kcpu_command is a tagged union:

__u8  type
__u8  padding[7]
union info {
    base_kcpu_command_fence_info              fence
    base_kcpu_command_cqs_wait_info           cqs_wait
    base_kcpu_command_cqs_set_info            cqs_set
    base_kcpu_command_cqs_wait_operation_info cqs_wait_operation
    base_kcpu_command_cqs_set_operation_info  cqs_set_operation
    base_kcpu_command_import_info             import
    base_kcpu_command_jit_alloc_info          jit_alloc
    base_kcpu_command_jit_free_info           jit_free
    base_kcpu_command_group_suspend_info      suspend_buf_copy
    __u64 padding[2]
}

## Important correction

JIT_ALLOC and JIT_FREE manage the just-in-time memory allocator. They are
not related to shader compilation. The earlier implementation conflated the
two. Manvil must treat shader loading as a separate mechanism.

## Suspend buffer

GROUP_SUSPEND copies the suspended group state into a user-provided buffer.
The buffer pointer, size, and group handle are provided. This is used for
context switching and virtualization scenarios.
