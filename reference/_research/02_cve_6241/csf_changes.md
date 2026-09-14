# CSF Specific Changes in UAPI 1.11 to 1.14

Focused summary of the changes that affect the CSF protocol itself,
beyond header reorganization.

## Group creation: csi_handlers field

Version 1.12 added a new input field to CS_QUEUE_GROUP_CREATE.

Previous layout (1.10):

  __u8 compute_max;
  __u8 padding[3];

Current layout (1.12 and later):

  __u8 compute_max;
  __u8 csi_handlers;
  __u8 padding[2];

Purpose: signal to the kernel that the application intends to handle
certain CSI exceptions itself in linear buffers.

Flag definitions from mali_base_csf_kernel.h:

  BASE_CSF_TILER_OOM_EXCEPTION_FLAG  (1u << 0)
  BASE_CSF_EXCEPTION_HANDLER_FLAGS_MASK  (BASE_CSF_TILER_OOM_EXCEPTION_FLAG)

Setting BASE_CSF_TILER_OOM_EXCEPTION_FLAG changes kernel behavior when
the tiler heap runs out of space. Without the flag, the kernel terminates
the offending group. With the flag, the notification path delivers the
event to the application.

## Tiler heap init: buf_desc_va field

Version 1.14 added a new input field to CS_TILER_HEAP_INIT.

Previous layout (through 1.13):

  __u32 chunk_size;
  __u32 initial_chunks;
  __u32 max_chunks;
  __u16 target_in_flight;
  __u8 group_id;
  __u8 padding;

Current layout (1.14 and later):

  __u32 chunk_size;
  __u32 initial_chunks;
  __u32 max_chunks;
  __u16 target_in_flight;
  __u8 group_id;
  __u8 padding;
  __u64 buf_desc_va;

Purpose: provide a GPU VA for a buffer descriptor that the firmware uses
to reclaim chunks from the tiler heap. Reclaims happen in place rather
than requiring the entire heap to be terminated and recreated.

The legacy ioctl is preserved on the same number (48):

  KBASE_IOCTL_CS_TILER_HEAP_INIT_1_13

A client that does not provide a buf_desc_va must use the legacy form.

## Relation to the earlier fork workaround

The earlier PanVK kbase fork solved the tiler heap OOM problem with two
measures:

  Retire the old heap context instead of destroying it immediately.
  Emit only VERTEX_TILER_STARTED in the graphics stream.

These measures avoid kernel-side statistics validation failures. They
work on kernel versions that do not support the newer reclaim mechanism.

With version 1.12 and later, the kernel provides native support:

  Set csi_handlers = BASE_CSF_TILER_OOM_EXCEPTION_FLAG at group creation.
  Provide a valid buf_desc_va at heap initialization.
  Handle the TILER_HEAP_OOM notification when it arrives.

This is the preferred approach for Manvil on kernels that support it.
The earlier workaround remains available as a fallback for older kernels.

## New ioctl: READ_USER_PAGE

Added in version 1.13 as ioctl 60.

union kbase_ioctl_read_user_page {
    struct {
        __u32 offset;    register offset in USER page
        __u32 padding;
    } in;
    struct {
        __u32 val_lo;
        __u32 val_hi;
    } out;
};

The USER page is the page returned by CS_QUEUE_BIND for a given queue.
It contains status registers that the kernel and firmware update during
execution. Reading a register does not require the page to be mapped.

Typical registers include CS_INSERT, CS_EXTRACT, CS_STATUS, and the
queue error state. The exact offsets are defined in the kernel
implementation of the CSF driver, not in the UAPI headers.

## Sync object sizing

Two synchronization object layouts are now defined explicitly.

Sync32:
  size 8 bytes, alignment 8 bytes
  value at offset 0 (u32)
  error at offset 4 (u32)

Sync64:
  size 16 bytes, alignment 16 bytes
  value at offset 0 (u64)
  error at offset 8 (u64)

These replace the older BASEP_EVENT_VAL_INDEX and BASEP_EVENT_ERR_INDEX
definitions which only described a single layout. The two-layout system
allows a client to choose between compact counters (u32) and full-range
counters (u64).

## Context flags

BASE_CONTEXT_CSF_EVENT_THREAD is preserved (bit 2). It creates a kernel
thread that delivers CSF notifications to the process.

The earlier BASEP_CONTEXT_CREATE_ALLOWED_FLAGS macro is removed, but the
flag itself is unchanged.

## Unchanged CSF protocol

The following CSF protocol elements are identical between 1.10 and 1.14:

  KCPU command types (all 13)
  CQS data types and operations
  CQS wait and set operation semantics
  Group priority encoding
  Error types delivered through notifications
  Notification structure layout
  Group terminate ioctl
  Queue terminate ioctl
  Queue kick ioctl
  Event signal ioctl
