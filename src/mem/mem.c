/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * GPU memory implementation.
 */

#include "mem.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "kernel_api/abi/manvil_abi.h"

/*
 * Query identifiers used by MEM_QUERY.
 */
#define MANVIL_MEM_QUERY_COMMIT_SIZE ((uint64_t)1)
#define MANVIL_MEM_QUERY_VA_SIZE     ((uint64_t)2)
#define MANVIL_MEM_QUERY_FLAGS       ((uint64_t)3)

/*
 * Internal allocation record.
 */
struct manvil_mem {
    manvil_kbase *kbase;

    /*
     * The GPU virtual address of the region.
     *
     * For SAME_VA allocations this is equal to the CPU mapping
     * address, because the kernel guarantees that CPU VA and GPU VA
     * are identical. The value returned by the MEM_ALLOC ioctl in
     * that case is a cookie, not a usable GPU VA, and becomes stale
     * as soon as the mmap succeeds.
     *
     * For non-SAME_VA allocations (executable regions and similar)
     * this is the value returned by MEM_ALLOC and the mmap offset is
     * the same value.
     */
    uint64_t      gpu_va;

    /*
     * Cookie as returned by MEM_ALLOC. Only meaningful for SAME_VA
     * allocations where it is used as the mmap offset. Kept for
     * diagnostic and for error paths.
     */
    uint64_t      cookie;

    uint64_t      size_bytes;
    void         *cpu_ptr;
    uint64_t      flags;

    /*
     * True if the allocation used the SAME_VA model. In that case the
     * lifetime is managed through munmap instead of MEM_FREE.
     */
    bool          same_va;
};

/*
 * Round a size up to the next page boundary.
 */
static uint64_t page_align_up(uint64_t value)
{
    const uint64_t page = 1ull << MANVIL_KBASE_PAGE_SHIFT;
    return (value + page - 1ull) & ~(page - 1ull);
}

/*
 * Convert a byte count to a page count.
 */
static uint64_t bytes_to_pages(uint64_t bytes)
{
    return bytes >> MANVIL_KBASE_PAGE_SHIFT;
}

/*
 * Perform MEM_ALLOC for the given size and flags.
 *
 * Fills in the cookie returned in out.gpu_va. The cookie is the value
 * that must be used both as the GPU virtual address of the region and
 * as the offset for the mmap call.
 */
static int mem_do_alloc(manvil_kbase *kbase,
                        uint64_t size_bytes,
                        uint32_t flags,
                        uint64_t *out_cookie)
{
    uint64_t pages = bytes_to_pages(size_bytes);

    union manvil_kbase_ioctl_mem_alloc alloc;
    memset(&alloc, 0, sizeof(alloc));

    alloc.in.va_pages     = pages;
    alloc.in.commit_pages = pages;
    alloc.in.extension    = 0;
    alloc.in.flags        = flags;

    int rc = manvil_kbase_ioctl(kbase, MANVIL_KBASE_IOCTL_MEM_ALLOC,
                                &alloc, "MEM_ALLOC");
    if (rc < 0) {
        fprintf(stderr, "[manvil] MEM_ALLOC args: "
                        "va_pages=%llu commit_pages=%llu extension=%llu "
                        "flags=0x%llx\n",
                        (unsigned long long)alloc.in.va_pages,
                        (unsigned long long)alloc.in.commit_pages,
                        (unsigned long long)alloc.in.extension,
                        (unsigned long long)alloc.in.flags);
        return -1;
    }

    fprintf(stderr, "[manvil] MEM_ALLOC result: "
                    "gpu_va=0x%016llx flags=0x%llx\n",
                    (unsigned long long)alloc.out.gpu_va,
                    (unsigned long long)alloc.out.flags);

    *out_cookie = alloc.out.gpu_va;
    return 0;
}

/*
 * Map the cookie returned by MEM_ALLOC into the process address space.
 *
 * The size must be the page aligned size used in the allocation. The
 * PROT flags are derived from the Manvil memory flags. Note that the
 * kernel may grant different effective permissions than requested,
 * but the user visible permissions are always a subset.
 */
static void *mem_do_mmap(manvil_kbase *kbase,
                         uint64_t cookie,
                         uint64_t size_bytes,
                         uint32_t flags)
{
    int prot = 0;
    if (flags & MANVIL_MEM_CPU_READ)  prot |= PROT_READ;
    if (flags & MANVIL_MEM_CPU_WRITE) prot |= PROT_WRITE;

    if (prot == 0) {
        prot = PROT_READ;
    }

    void *addr = mmap(NULL, size_bytes, prot, MAP_SHARED,
                      manvil_kbase_fd(kbase), (off_t)cookie);
    if (addr == MAP_FAILED) {
        fprintf(stderr, "[manvil] mmap(cookie=0x%016llx, size=%llu) FAILED: %s\n",
                (unsigned long long)cookie,
                (unsigned long long)size_bytes,
                strerror(errno));
        return NULL;
    }
    fprintf(stderr, "[manvil] mmap(cookie=0x%016llx, size=%llu) -> %p\n",
            (unsigned long long)cookie,
            (unsigned long long)size_bytes,
            addr);
    return addr;
}

manvil_mem *manvil_mem_alloc(manvil_kbase *kbase,
                             uint64_t size_bytes,
                             uint32_t flags)
{
    if (kbase == NULL || size_bytes == 0) {
        errno = EINVAL;
        return NULL;
    }

    uint64_t aligned = page_align_up(size_bytes);

    uint64_t cookie = 0;
    if (mem_do_alloc(kbase, aligned, flags, &cookie) < 0) {
        return NULL;
    }

    void *cpu_ptr = mem_do_mmap(kbase, cookie, aligned, flags);
    if (cpu_ptr == NULL) {
        /*
         * The mmap failed, so the cookie is still the only handle
         * for the region. Release it with MEM_FREE, which is the
         * correct operation for a pending cookie that was never
         * mapped.
         */
        struct manvil_kbase_ioctl_mem_free free_args;
        memset(&free_args, 0, sizeof(free_args));
        free_args.gpu_addr = cookie;
        (void)manvil_kbase_ioctl(kbase, MANVIL_KBASE_IOCTL_MEM_FREE,
                                 &free_args, "MEM_FREE (pending cookie rollback)");
        return NULL;
    }

    manvil_mem *mem = calloc(1, sizeof(*mem));
    if (mem == NULL) {
        /*
         * The mmap succeeded, so we must undo the mapping. Using
         * munmap is the correct way to release a SAME_VA region.
         */
        munmap(cpu_ptr, aligned);
        errno = ENOMEM;
        return NULL;
    }

    /*
     * Determine whether this allocation uses the SAME_VA model. On
     * SAME_VA, the kernel guarantees that the CPU mapping address
     * equals the GPU virtual address. That address is the one to
     * pass in every subsequent ioctl (register queue, sync, etc.).
     *
     * On non-SAME_VA the kernel returned a real GPU VA in the cookie
     * field, and the mmap offset was the same value.
     */
    bool same_va = (flags & MANVIL_BASE_MEM_SAME_VA) != 0;

    mem->kbase      = kbase;
    mem->cookie     = cookie;
    mem->size_bytes = aligned;
    mem->cpu_ptr    = cpu_ptr;
    mem->flags      = flags;
    mem->same_va    = same_va;
    mem->gpu_va     = same_va ? (uint64_t)(uintptr_t)cpu_ptr : cookie;

    return mem;
}

manvil_mem *manvil_mem_alloc_rw(manvil_kbase *kbase, uint64_t size_bytes)
{
    return manvil_mem_alloc(kbase, size_bytes, MANVIL_MEM_FLAGS_RW);
}

/*
 * Allocate read-write GPU memory with system-wide coherence.
 *
 * On platforms where the CPU and the GPU share a coherent view of
 * memory (most modern Mali GPUs), this allows the CPU to write data
 * that the GPU reads without an explicit cache flush. The kernel
 * silently drops the coherent flag if the platform does not support
 * it, in which case the caller must still perform an explicit sync.
 *
 * Use this for small allocations like shader code or command
 * descriptors that are written from the CPU and read by the GPU
 * shortly afterwards.
 */
manvil_mem *manvil_mem_alloc_coherent(manvil_kbase *kbase,
                                       uint64_t size_bytes)
{
    uint32_t flags = MANVIL_MEM_CPU_READ |
                     MANVIL_MEM_CPU_WRITE |
                     MANVIL_MEM_GPU_READ |
                     MANVIL_MEM_GPU_WRITE |
                     MANVIL_MEM_SAME_VA |
                     MANVIL_BASE_MEM_COHERENT_SYSTEM |
                     MANVIL_BASE_MEM_COHERENT_SYSTEM_REQUIRED;
    return manvil_mem_alloc(kbase, size_bytes, flags);
}

manvil_mem *manvil_mem_alloc_ro(manvil_kbase *kbase, uint64_t size_bytes)
{
    return manvil_mem_alloc(kbase, size_bytes, MANVIL_MEM_FLAGS_RO);
}

manvil_mem *manvil_mem_alloc_exec(manvil_kbase *kbase, uint64_t size_bytes)
{
    return manvil_mem_alloc(kbase, size_bytes, MANVIL_MEM_FLAGS_EXEC);
}

void manvil_mem_free(manvil_mem *mem)
{
    if (mem == NULL) {
        return;
    }

    if (mem->same_va) {
        /*
         * SAME_VA regions are released with munmap. The kernel tears
         * down the GPU mapping as part of the munmap, so no MEM_FREE
         * ioctl is issued. Calling MEM_FREE on a SAME_VA region that
         * has already been mapped returns EINVAL, because the cookie
         * has been consumed by the mmap.
         */
        if (mem->cpu_ptr != NULL) {
            munmap(mem->cpu_ptr, mem->size_bytes);
        }
    } else {
        /*
         * Non-SAME_VA regions (executable and zone allocations) are
         * released with MEM_FREE on the GPU VA. The mmap, if any, is
         * torn down by the kernel as part of the ioctl.
         */
        struct manvil_kbase_ioctl_mem_free free_args;
        memset(&free_args, 0, sizeof(free_args));
        free_args.gpu_addr = mem->gpu_va;

        (void)manvil_kbase_ioctl(mem->kbase, MANVIL_KBASE_IOCTL_MEM_FREE,
                                 &free_args, "MEM_FREE");
    }

    memset(mem, 0, sizeof(*mem));
    free(mem);
}

int manvil_mem_sync(manvil_mem *mem, manvil_mem_sync_dir dir)
{
    if (mem == NULL) {
        errno = EINVAL;
        return -1;
    }

    struct manvil_kbase_ioctl_mem_sync sync_args;
    memset(&sync_args, 0, sizeof(sync_args));

    /*
     * Empirical result on the tested MediaTek Mali-G615: the kernel
     * accepts only a small set of parameter combinations for
     * MEM_SYNC.
     *
     *   handle = CPU VA of the mapping
     *   user_addr = anything (the kernel does not strictly check it)
     *   type = 1 (invalidate / FROM_DEVICE)
     *
     * Anything else returns EINVAL. In particular:
     *
     *   - passing the cookie as the handle fails
     *   - passing type=0 (clean / TO_DEVICE) always fails
     *
     * The type=1 restriction means we cannot force a clean of the
     * CPU cache to make CPU writes visible to the GPU. Callers that
     * need that property must allocate coherent memory instead (see
     * manvil_mem_alloc_coherent).
     *
     * The dir argument is kept for API symmetry but the kernel
     * rejects TO_DEVICE, so this function only issues invalidate
     * operations. Callers that need a clean are expected to use
     * coherent memory.
     */
    (void)dir;
    sync_args.handle    = (uint64_t)(uintptr_t)mem->cpu_ptr;
    sync_args.user_addr = (uint64_t)(uintptr_t)mem->cpu_ptr;
    sync_args.size      = mem->size_bytes;
    sync_args.type      = 1;  /* invalidate */

    fprintf(stderr,
            "[manvil] MEM_SYNC: handle=0x%llx user_addr=0x%llx size=%llu type=%u\n",
            (unsigned long long)sync_args.handle,
            (unsigned long long)sync_args.user_addr,
            (unsigned long long)sync_args.size,
            sync_args.type);

    int rc = manvil_kbase_ioctl(mem->kbase, MANVIL_KBASE_IOCTL_MEM_SYNC,
                                &sync_args, "MEM_SYNC");
    if (rc < 0) {
        fprintf(stderr, "[manvil] MEM_SYNC failed: errno=%d (%s)\n",
                errno, strerror(errno));
    }
    return rc < 0 ? -1 : 0;
}

uint64_t manvil_mem_gpu_va(const manvil_mem *mem)
{
    return mem != NULL ? mem->gpu_va : 0;
}

void *manvil_mem_cpu_ptr(const manvil_mem *mem)
{
    return mem != NULL ? mem->cpu_ptr : NULL;
}

uint64_t manvil_mem_size(const manvil_mem *mem)
{
    return mem != NULL ? mem->size_bytes : 0;
}

uint64_t manvil_mem_flags(const manvil_mem *mem)
{
    return mem != NULL ? mem->flags : 0;
}

/*
 * Internal helper for the MEM_QUERY based accessors.
 *
 * Returns 0 on success and stores the result in *out_value. Returns -1
 * on failure.
 */
static int mem_do_query(const manvil_mem *mem,
                        uint64_t query_id,
                        uint64_t *out_value)
{
    if (mem == NULL) {
        errno = EINVAL;
        return -1;
    }

    union {
        struct {
            uint64_t gpu_addr;
            uint64_t query;
        } in;
        struct {
            uint64_t value;
        } out;
    } args;

    /*
     * This local union mirrors the layout of
     * manvil_kbase_ioctl_mem_query. It is declared inline to keep the
     * header free of an unnecessary typedef for a private helper.
     */
    memset(&args, 0, sizeof(args));
    args.in.gpu_addr = mem->gpu_va;
    args.in.query    = query_id;

    int rc = manvil_kbase_ioctl(mem->kbase, MANVIL_KBASE_IOCTL_MEM_QUERY,
                                &args, "MEM_QUERY");
    if (rc < 0) {
        return -1;
    }

    *out_value = args.out.value;
    return 0;
}

uint64_t manvil_mem_commit_size(const manvil_mem *mem)
{
    uint64_t v = 0;
    (void)mem_do_query(mem, MANVIL_MEM_QUERY_COMMIT_SIZE, &v);
    return v;
}

uint64_t manvil_mem_va_size(const manvil_mem *mem)
{
    uint64_t v = 0;
    (void)mem_do_query(mem, MANVIL_MEM_QUERY_VA_SIZE, &v);
    return v;
}

uint64_t manvil_mem_current_flags(const manvil_mem *mem)
{
    uint64_t v = 0;
    (void)mem_do_query(mem, MANVIL_MEM_QUERY_FLAGS, &v);
    return v;
}
