# UAPI Version Delta: 1.10 to 1.14

The mali-kbase-src headers ship with UAPI version 1.10. The CVE-2023-6241
headers ship with version 1.14. This document summarizes what changed.

## Version history (from the CSF ioctl header changelog)

1.0  CSF ioctl header separated from JM
1.1  Added BASE_QUEUE_GROUP_PRIORITY_REALTIME, ioctl 54
1.2  Added CSF GPU_FEATURES register to GET_GPUPROPS
1.3  Added group_uid to QUEUE_GROUP_CREATE output
1.4  Replaced padding in GET_GLB_IFACE with instr_features
1.5  Added ioctl 40, QUEUE_REGISTER_EX
1.6  Added new HW performance counters interface to all GPUs
1.7  Added reserved field to QUEUE_GROUP_CREATE
1.8  Removed Kernel legacy HWC interface
1.9  Reorganized GPU-VA memory zones, added FIXED_VA zone
1.10 New HW performance counters interface
1.11 Dummy model backend clears HWC values after each sample
1.12 Added incremental rendering flag in CSG create call
1.13 Added ioctl to query a register of USER page
1.14 Added buffer descriptor VA in tiler heap init

## Structural changes

### Memory flags moved

BASE_MEM_FIXED                  moved from common to CSF header
BASE_MEM_FIXABLE                moved from common to CSF header
BASE_MEM_CSF_EVENT              moved from common to CSF header
BASE_MEM_RESERVED_BIT_20        moved from common to CSF header
BASE_CONTEXT_CSF_EVENT_THREAD   moved from common to CSF header

The flags themselves are unchanged. Only their location in the header
tree changed. The purpose is header organization.

### Removed macros

BASEP_MEM_FLAGS_KERNEL_ONLY            expanded into its two components
BASE_MEM_FLAGS_RESERVED                removed, bit 20 is unnamed
BASEP_CONTEXT_CREATE_ALLOWED_FLAGS     removed

### Simplification

LOCAL_PAGE_SHIFT is hardcoded to 12. The conditional derivation based on
PAGE_SHIFT and OSU_CONFIG_CPU_PAGE_SIZE_LOG2 is gone.

### Header split

mali_base_kernel.h (1.10) is split into:
  mali_base_common_kernel.h   memory flags, context flags, tracepoints
  mali_base_kernel.h          GPU properties, coherency information

## New features

### Sync32 and Sync64 objects

Two synchronization object layouts are now defined:

Sync32 (8 bytes total):
  value at offset 0 (u32)
  error at offset 4 (u32)

Sync64 (16 bytes total):
  value at offset 0 (u64)
  error at offset 8 (u64)

Alignment matches size.

### CSI exception handlers

The CS_QUEUE_GROUP_CREATE input gains a csi_handlers field, taken from
the former padding.

#define BASE_CSF_TILER_OOM_EXCEPTION_FLAG (1u << 0)

When set, the application takes responsibility for handling tiler out of
memory conditions in linear buffers instead of letting the kernel
terminate the group.

### Tiler heap buffer descriptor

The CS_TILER_HEAP_INIT input gains a buf_desc_va field at the end:

  __u64 buf_desc_va

This is the GPU VA of a buffer descriptor used for tiler heap reclaims.
The firmware uses it to recover chunks without terminating the heap.

The legacy form without this field is preserved as
KBASE_IOCTL_CS_TILER_HEAP_INIT_1_13 on the same ioctl number 48.

### New ioctl: READ_USER_PAGE

KBASE_IOCTL_READ_USER_PAGE is ioctl 60.

union kbase_ioctl_read_user_page {
    struct {
        __u32 offset;
        __u32 padding;
    } in;
    struct {
        __u32 val_lo;
        __u32 val_hi;
    } out;
};

Reads a 32-bit or 64-bit register from the bound queue USER page by
offset. Replaces the earlier test-only cs_event_memory_read ioctl.

## Unchanged definitions

The following structures and enums are identical between 1.10 and 1.14:

  struct base_kcpu_command
  enum base_kcpu_command_type (13 entries)
  enum base_queue_group_priority
  struct basep_cs_stream_control
  struct basep_cs_group_control
  struct base_gpu_queue_group_error
  enum base_gpu_queue_group_error_type
  struct base_csf_notification
  enum base_csf_notification_type
  CQS data types and operations
