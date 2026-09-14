# Core Kbase Ioctls

Type byte: KBASE_IOCTL_TYPE = 0x80

All ioctls use the standard _IOW / _IOR / _IOWR macros from asm-generic/ioctl.h.

## Complete table

| Nr | Name | Dir | Struct | Priority |
|----|------|-----|--------|----------|
| 1  | SET_FLAGS | W | kbase_ioctl_set_flags | High (handshake) |
| 3  | GET_GPUPROPS | W | kbase_ioctl_get_gpuprops | High (detection) |
| 5  | MEM_ALLOC | WR | kbase_ioctl_mem_alloc | High |
| 6  | MEM_QUERY | WR | kbase_ioctl_mem_query | Medium |
| 7  | MEM_FREE | W | kbase_ioctl_mem_free | High |
| 8  | HWCNT_READER_SETUP | W | kbase_ioctl_hwcnt_reader_setup | Low |
| 12 | DISJOINT_QUERY | R | kbase_ioctl_disjoint_query | Low |
| 13 | GET_DDK_VERSION | W | kbase_ioctl_get_ddk_version | High |
| 14 | MEM_JIT_INIT | W | kbase_ioctl_mem_jit_init | Medium |
| 15 | MEM_SYNC | W | kbase_ioctl_mem_sync | High |
| 16 | MEM_FIND_CPU_OFFSET | WR | kbase_ioctl_mem_find_cpu_offset | Medium |
| 17 | GET_CONTEXT_ID | R | kbase_ioctl_get_context_id | Medium |
| 18 | TLSTREAM_ACQUIRE | W | kbase_ioctl_tlstream_acquire | Low |
| 19 | TLSTREAM_FLUSH | - | (no struct) | Low |
| 20 | MEM_COMMIT | W | kbase_ioctl_mem_commit | Medium |
| 21 | MEM_ALIAS | WR | kbase_ioctl_mem_alias | Medium |
| 22 | MEM_IMPORT | WR | kbase_ioctl_mem_import | Medium |
| 23 | MEM_FLAGS_CHANGE | W | kbase_ioctl_mem_flags_change | Medium |
| 24 | STREAM_CREATE | W | kbase_ioctl_stream_create | Medium |
| 25 | FENCE_VALIDATE | W | kbase_ioctl_fence_validate | Medium |
| 27 | MEM_PROFILE_ADD | W | kbase_ioctl_mem_profile_add | Low |
| 29 | STICKY_RESOURCE_MAP | W | kbase_ioctl_sticky_resource_map | Low |
| 30 | STICKY_RESOURCE_UNMAP | W | kbase_ioctl_sticky_resource_unmap | Low |
| 31 | MEM_FIND_GPU_START_AND_OFFSET | WR | (union) | Medium |
| 32 | HWCNT_SET | W | kbase_ioctl_hwcnt_values | Low |
| 33 | CINSTR_GWT_START | - | (no struct) | Low |
| 34 | CINSTR_GWT_STOP | - | (no struct) | Low |
| 35 | CINSTR_GWT_DUMP | WR | (union) | Low |
| 38 | MEM_EXEC_INIT | W | kbase_ioctl_mem_exec_init | High (exec VA) |
| 50 | GET_CPU_GPU_TIMEINFO | WR | (union) | Medium |
| 54 | CONTEXT_PRIORITY_CHECK | WR | kbase_ioctl_context_priority_check | Medium |
| 55 | SET_LIMITED_CORE_COUNT | W | kbase_ioctl_set_limited_core_count | Medium |
| 56 | KINSTR_PRFCNT_ENUM_INFO | WR | kbase_ioctl_kinstr_prfcnt_enum_info | Low |
| 57 | KINSTR_PRFCNT_SETUP | WR | kbase_ioctl_kinstr_prfcnt_setup | Low |
| 66 | APC_REQUEST | W | kbase_ioctl_apc_request | Medium |

## Notes

GET_GPUPROPS uses a key/value encoded buffer. Keys are u32; the low 2 bits
of the key indicate the size of the following value: 00=u8, 01=u16,
10=u32, 11=u64. Values are little-endian and tightly packed.

MEM_ALLOC_MAX_SIZE limits a single allocation to 8 GiB.
