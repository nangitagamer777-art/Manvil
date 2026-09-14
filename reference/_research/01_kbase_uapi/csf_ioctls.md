# CSF Ioctls

CSF ioctls are defined in csf/mali_kbase_csf_ioctl.h. They use the same
KBASE_IOCTL_TYPE byte (0x80).

## Complete table

| Nr | Name | Dir | Struct | Purpose |
|----|------|-----|--------|---------|
| 36 | CS_QUEUE_REGISTER | W | kbase_ioctl_cs_queue_register | Register a ring buffer as a queue |
| 37 | CS_QUEUE_KICK | W | kbase_ioctl_cs_queue_kick | Notify scheduler of new work |
| 39 | CS_QUEUE_BIND | WR | kbase_ioctl_cs_queue_bind | Bind queue to group and CSI |
| 40 | CS_QUEUE_REGISTER_EX | W | kbase_ioctl_cs_queue_register_ex | Register with tracing support |
| 41 | CS_QUEUE_TERMINATE | W | kbase_ioctl_cs_queue_terminate | Destroy a queue |
| 42 | CS_QUEUE_GROUP_CREATE_1_6 | WR | ..._1_6 | Legacy ABI (pre-1.7) |
| 43 | CS_QUEUE_GROUP_TERMINATE | W | kbase_ioctl_cs_queue_group_term | Destroy a group |
| 44 | CS_EVENT_SIGNAL | - | (no struct) | Manual event signal |
| 45 | KCPU_QUEUE_CREATE | R | kbase_ioctl_kcpu_queue_new | Create KCPU queue, returns id |
| 46 | KCPU_QUEUE_DELETE | W | kbase_ioctl_kcpu_queue_delete | Destroy KCPU queue |
| 47 | KCPU_QUEUE_ENQUEUE | W | kbase_ioctl_kcpu_queue_enqueue | Enqueue KCPU commands |
| 48 | CS_TILER_HEAP_INIT | WR | kbase_ioctl_cs_tiler_heap_init | Initialize tiler heap |
| 49 | CS_TILER_HEAP_TERM | W | kbase_ioctl_cs_tiler_heap_term | Terminate tiler heap |
| 51 | CS_GET_GLB_IFACE | WR | kbase_ioctl_cs_get_glb_iface | Global CSF capabilities |
| 52 | VERSION_CHECK | WR | kbase_ioctl_version_check | UAPI version negotiation |
| 53 | CS_CPU_QUEUE_DUMP | W | kbase_ioctl_cs_cpu_queue_info | Dump KCPU queue |
| 58 | CS_QUEUE_GROUP_CREATE | WR | kbase_ioctl_cs_queue_group_create | Current ABI |

## Version history

From the header changelog:

1.0  CSF ioctl header separated from JM
1.1  Added BASE_QUEUE_GROUP_PRIORITY_REALTIME, ioctl 54
1.2  Added CSF GPU_FEATURES register to GET_GPUPROPS
1.3  Added __u32 group_uid to QUEUE_GROUP_CREATE output
1.4  Replaced padding in GET_GLB_IFACE with instr_features
1.5  Added ioctl 40, QUEUE_REGISTER_EX
1.6  Added new HW performance counters interface to all GPUs
1.7  Added reserved field to QUEUE_GROUP_CREATE
1.8  Removed Kernel legacy HWC interface
1.9  Reorganized GPU-VA memory zones, added FIXED_VA, auto-init EXEC_VA
1.10 New HW performance counters interface

Current header version: BASE_UK_VERSION_MAJOR=1, BASE_UK_VERSION_MINOR=10.
Target device reports UAPI 1.20, so the header set needs to be verified
against a newer source (kernel tree of the MediaTek device or CVE-2023-6241
headers).

## Notes

CS_QUEUE_GROUP_CREATE is ioctl 58 in the current ABI. Ioctl 42 is the
legacy 1.6 form. Both exist so that older clients can still run.

CS_QUEUE_BIND returns a mmap_handle in its out struct. This handle is used
to mmap the CS input/output pages and the doorbell page.

CSI index for CS_QUEUE_BIND: the csi_index field selects which Command
Stream Interface within the group the queue binds to. Empirical observation
on the target device shows CSI0 executes work, while CSI1 and CSI2 can be
registered but stay idle.
