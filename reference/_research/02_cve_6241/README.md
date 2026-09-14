# CVE-2023-6241 Research

Analysis of the Google SecurityLab disclosure for CVE-2023-6241, a
vulnerability in the Arm Mali Kbase kernel driver reported by Man Yue Mo
in November 2023.

## Why this is relevant

The disclosure package contains the most recent set of CSF UAPI headers
publicly available (version 1.14), plus reference implementations of the
KCPU command flow, JIT memory allocation, and memory pool management.
These are the pieces Manvil needs to reproduce the behavior of the
official driver.

The exploit payload itself is not used. Only the interface definitions
and usage patterns are extracted.

## Package contents

mali_kbase_csf_ioctl.h        556 lines   UAPI ioctls for CSF, version 1.14
mali_base_csf_kernel.h        608 lines   CSF data structures
mali_base_common_kernel.h     228 lines   Common memory and context flags
mali_kbase_ioctl.h            894 lines   Core UAPI ioctls
mali_base_kernel.h            287 lines   GPU properties
mali_jit_csf.c                435 lines   Full CSF usage example
mem_read_write.c/h            306 lines   GPU memory access example
mempool_utils.c/h              80 lines   Memory pool example
firmware_offsets.h             16 lines   Kernel offsets (Pixel 8 specific)

## Scope

The headers and interface definitions are extracted. The exploit logic
(privilege escalation, page table manipulation, SELinux bypass) is not
relevant to Manvil and is not documented here.
