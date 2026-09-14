# Miscellaneous Notes

## Chat formatting hazard

Large header files pasted through chat interfaces can have their content
corrupted. Content should always be read directly from disk with scripts
rather than copied from a conversation.

## Header version vs kernel version

The UAPI header in mali-kbase-src declares BASE_UK_VERSION_MAJOR=1 and
BASE_UK_VERSION_MINOR=10. The target device reports UAPI 1.20. The
difference is expected and reflects incremental ABI additions. The
authoritative header set for the target device is either the kernel tree
shipped by MediaTek or the headers published alongside the relevant CVEs.

## Priority encoding

BASE_QUEUE_GROUP_PRIORITY_HIGH     = 0
BASE_QUEUE_GROUP_PRIORITY_MEDIUM   = 1
BASE_QUEUE_GROUP_PRIORITY_LOW      = 2
BASE_QUEUE_GROUP_PRIORITY_REALTIME = 3
BASE_QUEUE_GROUP_PRIORITY_COUNT    = 4

BASE_QUEUE_MAX_PRIORITY = 15

Lower numeric value means higher priority.

## CSI selection

On the target hardware only CSI0 executes. The kernel accepts bind
requests for CSI1 and CSI2 without error, but work submitted on those
interfaces does not complete. This matches the observation that the
firmware may not schedule non-zero CSIs on certain configurations.

## Sync object memory

Synchronization objects shared across groups must be allocated with the
BASE_MEM_CSF_EVENT flag. This flag ensures the kernel sets up the mapping
with the coherence required for cross-group visibility.

## Notification types

CSF delivers notifications to user space through the kernel:

BASE_CSF_NOTIFICATION_EVENT                 kernel event
BASE_CSF_NOTIFICATION_GPU_QUEUE_GROUP_ERROR fatal error
BASE_CSF_NOTIFICATION_CPU_QUEUE_DUMP        KCPU queue dump

Fatal errors carry a payload of type base_gpu_queue_group_error with one
of these subtypes:

BASE_GPU_QUEUE_GROUP_ERROR_FATAL            CSG fault
BASE_GPU_QUEUE_GROUP_QUEUE_ERROR_FATAL      queue fault
BASE_GPU_QUEUE_GROUP_ERROR_TIMEOUT          progress timeout
BASE_GPU_QUEUE_GROUP_ERROR_TILER_HEAP_OOM   tiler heap exhausted
