/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * GPU memory implementation.
 */

#include "mem.h"

#include <errno.h>
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
    uint64_t      gpu_va;
    uint64_t      size_bytes;
    void         *cpu_ptr;
    uint64_t      flags;
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
        return -1;
    }

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

    /*
     * If the caller requested a GPU only region, the kernel may still
     * create the mapping. Use PROT_READ at minimum so that the region
     * can be inspected during debugging without segfaulting.
     */
    if (prot == 0) {
        prot = PROT_READ;
    }

    void *addr = mmap(NULL, size_bytes, prot, MAP_SHARED,
                      manvil_kbase_fd(kbase), (off_t)cookie);
    if (addr == MAP_FAILED) {
        return NULL;
    }
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
         * Undo the allocation to avoid leaking GPU memory.
         */
        struct manvil_kbase_ioctl_mem_free free_args;
        memset(&free_args, 0, sizeof(free_args));
        free_args.gpu_addr = cookie;
        (void)manvil_kbase_ioctl(kbase, MANVIL_KBASE_IOCTL_MEM_FREE,
                                 &free_args, "MEM_FREE (rollback)");
        return NULL;
    }

    manvil_mem *mem = calloc(1, sizeof(*mem));
    if (mem == NULL) {
        struct manvil_kbase_ioctl_mem_free free_args;
        memset(&free_args, 0, sizeof(free_args));
        free_args.gpu_addr = cookie;
        (void)manvil_kbase_ioctl(kbase, MANVIL_KBASE_IOCTL_MEM_FREE,
                                 &free_args, "MEM_FREE (rollback)");
        errno = ENOMEM;
        return NULL;
    }

    mem->kbase      = kbase;
    mem->gpu_va     = cookie;
    mem->size_bytes = aligned;
    mem->cpu_ptr    = cpu_ptr;
    mem->flags      = flags;

    return mem;
}

manvil_mem *manvil_mem_alloc_rw(manvil_kbase *kbase, uint64_t size_bytes)
{
    return manvil_mem_alloc(kbase, size_bytes, MANVIL_MEM_FLAGS_RW);
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

    /*
     * Freeing the GPU region also tears down the userspace mapping.
     * Do not munmap explicitly, because on SAME_VA allocations the
     * munmap would destroy the GPU mapping before the region is
     * released, leaving the kernel in an inconsistent state.
     */
    struct manvil_kbase_ioctl_mem_free free_args;
    memset(&free_args, 0, sizeof(free_args));
    free_args.gpu_addr = mem->gpu_va;

    (void)manvil_kbase_ioctl(mem->kbase, MANVIL_KBASE_IOCTL_MEM_FREE,
                             &free_args, "MEM_FREE");

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

    sync_args.handle    = mem->gpu_va;
    sync_args.user_addr = (uint64_t)(uintptr_t)mem->cpu_ptr;
    sync_args.size      = mem->size_bytes;
    sync_args.type      = (uint8_t)dir;

    int rc = manvil_kbase_ioctl(mem->kbase, MANVIL_KBASE_IOCTL_MEM_SYNC,
                                &sync_args, "MEM_SYNC");
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
