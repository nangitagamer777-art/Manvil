# Memory Model

## Allocation flags

Full flag list from csf/mali_base_csf_kernel.h and mali_base_kernel.h:

Bit 0   BASE_MEM_PROT_CPU_RD
Bit 1   BASE_MEM_PROT_CPU_WR
Bit 2   BASE_MEM_PROT_GPU_RD
Bit 3   BASE_MEM_PROT_GPU_WR
Bit 4   BASE_MEM_PROT_GPU_EX               executable
Bit 5   BASEP_MEM_PERMANENT_KERNEL_MAPPING kernel-only
Bit 6   BASE_MEM_GPU_VA_SAME_4GB_PAGE
Bit 7   BASEP_MEM_NO_USER_FREE             kernel-only
Bit 8   BASE_MEM_FIXED
Bit 9   BASE_MEM_GROW_ON_GPF
Bit 10  BASE_MEM_COHERENT_SYSTEM
Bit 11  BASE_MEM_COHERENT_LOCAL
Bit 12  BASE_MEM_CACHED_CPU
Bit 13  BASE_MEM_SAME_VA
Bit 14  BASE_MEM_NEED_MMAP                 output
Bit 15  BASE_MEM_COHERENT_SYSTEM_REQUIRED
Bit 16  BASE_MEM_PROTECTED
Bit 17  BASE_MEM_DONT_NEED
Bit 18  BASE_MEM_IMPORT_SHARED
Bit 19  BASE_MEM_CSF_EVENT                 synchronization objects
Bit 20  BASE_MEM_RESERVED_BIT_20
Bit 21  BASE_MEM_UNCACHED_GPU
22-25   BASE_MEM_GROUP_ID_MASK             memory group id (0-15)
Bit 26  BASE_MEM_IMPORT_SYNC_ON_MAP_UNMAP
Bit 28  BASE_MEM_KERNEL_SYNC
Bit 29  BASE_MEM_FIXABLE

BASE_MEM_FLAGS_NR_BITS = 30

## Special handles

These are pre-reserved GPU VAs. They are computed as (N << PAGE_SHIFT).

0  BASEP_MEM_INVALID_HANDLE
1  BASE_MEM_MMU_DUMP_HANDLE
2  BASE_MEM_TRACE_BUFFER_HANDLE
3  BASE_MEM_MAP_TRACKING_HANDLE
4  BASEP_MEM_WRITE_ALLOC_PAGES_HANDLE
47 BASEP_MEM_CSF_USER_REG_PAGE_HANDLE
48 BASEP_MEM_CSF_USER_IO_PAGES_HANDLE
64 BASE_MEM_COOKIE_BASE

KBASE_CSF_NUM_USER_IO_PAGES_HANDLE is (64 - 48) = 16, meaning handles 48
through 63 are available for CS input/output page mappings.

## Cookie and SAME_VA model

MEM_ALLOC returns a cookie in the gpu_va output field. When the SAME_VA
flag is set (which the kernel forces for non-executable allocations from
64-bit clients), the cookie is also the GPU VA. Calling mmap on the device
fd with the cookie as offset creates a CPU mapping whose virtual address
equals the GPU virtual address.

Executable allocations use a separate zone initialized through
MEM_EXEC_INIT. The same ioctl returns a real GPU VA that can be used as the
mmap offset.

## Import types

BASE_MEM_IMPORT_TYPE_UMM          = 2, handle is a dma-buf fd
BASE_MEM_IMPORT_TYPE_USER_BUFFER  = 3, handle is {ptr, length}

## Coherency

COHERENCY_ACE_LITE = 0
COHERENCY_ACE      = 1
COHERENCY_NONE     = 31

The active mode is reported in GET_GPUPROPS. Up to 16 coherent groups.

## Allocation limit

KBASE_MEM_ALLOC_MAX_SIZE is 8 GiB per call.

## Alias limit

BASE_MEM_ALIAS_MAX_ENTS is 24576, matching the maximum cube map array size.

## Cache maintenance

MEM_SYNC performs clean or invalidate operations. The direction is selected
through the type field: 0 = clean (sync to memory), 1 = invalidate (sync
from memory).
