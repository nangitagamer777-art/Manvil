/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * GPU memory management.
 *
 * A manvil_mem represents a region of GPU memory that has been
 * allocated through the Kbase MEM_ALLOC ioctl and mapped into the
 * process address space with mmap. On modern kernels the mapping uses
 * the SAME_VA model: the CPU virtual address of the mapping equals
 * the GPU virtual address of the region. This makes the CPU pointer
 * directly usable as the GPU address, with no translation step.
 *
 * Cache coherency between CPU and GPU is not automatic for every
 * allocation. The MEM_SYNC ioctl is used to clean or invalidate the
 * CPU cache lines for the region when needed. Higher layers call
 * manvil_mem_sync before submitting work that will read CPU writes, or
 * after receiving work that wrote to memory the CPU will read.
 *
 * The memory module is intentionally minimal. It does not manage
 * pools, suballocate blocks, or track reference counts. Those
 * concerns belong to higher layers built on top of manvil_mem. This
 * keeps the interface to the kernel small and predictable.
 */

#ifndef MANVIL_MEM_H
#define MANVIL_MEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel_api/manvil_kbase.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Opaque handle to an allocation.
 */
typedef struct manvil_mem manvil_mem;

/*
 * Flags accepted by manvil_mem_alloc.
 *
 * These are the flags defined by the Kbase UAPI. Higher layers should
 * prefer the named helpers below unless they need something specific.
 */
#define MANVIL_MEM_CPU_READ    MANVIL_BASE_MEM_PROT_CPU_RD
#define MANVIL_MEM_CPU_WRITE   MANVIL_BASE_MEM_PROT_CPU_WR
#define MANVIL_MEM_GPU_READ    MANVIL_BASE_MEM_PROT_GPU_RD
#define MANVIL_MEM_GPU_WRITE   MANVIL_BASE_MEM_PROT_GPU_WR
#define MANVIL_MEM_GPU_EXEC    MANVIL_BASE_MEM_PROT_GPU_EX

/*
 * Common flag combinations.
 */
#define MANVIL_MEM_FLAGS_RW \
    (MANVIL_MEM_CPU_READ | MANVIL_MEM_CPU_WRITE | \
     MANVIL_MEM_GPU_READ | MANVIL_MEM_GPU_WRITE)

#define MANVIL_MEM_FLAGS_RO \
    (MANVIL_MEM_CPU_READ | MANVIL_MEM_GPU_READ)

#define MANVIL_MEM_FLAGS_EXEC \
    (MANVIL_MEM_CPU_READ | MANVIL_MEM_CPU_WRITE | \
     MANVIL_MEM_GPU_READ | MANVIL_MEM_GPU_EXEC)

/*
 * Direction for manvil_mem_sync.
 *
 * MANVIL_MEM_SYNC_TO_DEVICE
 *   Clean the CPU cache lines so that writes performed on the CPU are
 *   visible to the GPU. Use this before submitting work that reads
 *   memory the CPU has written.
 *
 * MANVIL_MEM_SYNC_FROM_DEVICE
 *   Invalidate the CPU cache lines so that reads on the CPU observe
 *   the latest GPU writes. Use this after the GPU has written to
 *   memory the CPU is about to read.
 */
typedef enum manvil_mem_sync_dir {
    MANVIL_MEM_SYNC_TO_DEVICE = 0,
    MANVIL_MEM_SYNC_FROM_DEVICE = 1,
} manvil_mem_sync_dir;

/*
 * Allocate GPU memory and map it into the process address space.
 *
 * kbase       Backend handle from manvil_device_kbase().
 * size_bytes  Requested size in bytes. Must be page aligned and
 *             greater than zero.
 * flags       Combination of MANVIL_MEM_* flags. The kernel forces
 *             SAME_VA on non executable allocations from 64 bit
 *             processes, so the CPU mapping always exists for those.
 *
 * On success, returns a handle. On failure, returns NULL and does not
 * touch errno any more than the underlying ioctl does.
 *
 * The allocation is fully committed: physical pages are reserved up
 * front, without relying on GPU page faults. Higher layers that want
 * on demand growth should use the JIT allocator in the KCPU module
 * instead.
 */
manvil_mem *manvil_mem_alloc(manvil_kbase *kbase,
                             uint64_t size_bytes,
                             uint32_t flags);

/*
 * Convenience wrappers for the most common flag sets.
 */
manvil_mem *manvil_mem_alloc_rw(manvil_kbase *kbase, uint64_t size_bytes);
manvil_mem *manvil_mem_alloc_ro(manvil_kbase *kbase, uint64_t size_bytes);
manvil_mem *manvil_mem_alloc_exec(manvil_kbase *kbase, uint64_t size_bytes);

/*
 * Free a GPU memory allocation.
 *
 * Passing NULL is a no-op.
 *
 * The kernel removes the userspace mapping as part of MEM_FREE. Manvil
 * does not call munmap explicitly, because doing so on a SAME_VA
 * region would destroy the GPU mapping before the region is freed.
 */
void manvil_mem_free(manvil_mem *mem);

/*
 * Synchronize cache state for a region.
 *
 * dir    Direction of the sync, see manvil_mem_sync_dir.
 *
 * Returns 0 on success, or a negative value on failure. The specific
 * errno is left untouched for callers that want to inspect it.
 */
int manvil_mem_sync(manvil_mem *mem, manvil_mem_sync_dir dir);

/*
 * Accessors. All of them return zero or NULL if the argument is NULL.
 */
uint64_t manvil_mem_gpu_va(const manvil_mem *mem);
void    *manvil_mem_cpu_ptr(const manvil_mem *mem);
uint64_t manvil_mem_size(const manvil_mem *mem);
uint64_t manvil_mem_flags(const manvil_mem *mem);

/*
 * Query helpers. These call MEM_QUERY on demand and return the result.
 *
 * The kernel supports three query types:
 *   MANVIL_MEM_QUERY_COMMIT_SIZE  number of physical pages committed
 *   MANVIL_MEM_QUERY_VA_SIZE      number of virtual pages reserved
 *   MANVIL_MEM_QUERY_FLAGS        current flags
 *
 * The three wrappers below return the value as a uint64_t, or 0 on
 * failure. Callers that need to distinguish failure from a legitimate
 * zero should call the underlying ioctl directly.
 */
uint64_t manvil_mem_commit_size(const manvil_mem *mem);
uint64_t manvil_mem_va_size(const manvil_mem *mem);
uint64_t manvil_mem_current_flags(const manvil_mem *mem);

#ifdef __cplusplus
}
#endif

#endif /* MANVIL_MEM_H */
