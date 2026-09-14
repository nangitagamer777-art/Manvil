/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * Tiler heap implementation.
 */

#include "heap.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "kernel_api/abi/manvil_abi.h"

/*
 * Internal heap structure.
 *
 * The kernel owns the actual chunks and the context block. Manvil
 * only stores the handles it needs to reference the heap and the
 * parameters used at creation.
 */
struct manvil_heap {
    manvil_kbase *kbase;

    /*
     * Values returned by the CS_TILER_HEAP_INIT ioctl.
     */
    uint64_t gpu_va;          /* heap context block */
    uint64_t first_chunk_va;  /* first chunk header */

    /*
     * Copy of the descriptor used at creation.
     */
    struct manvil_heap_desc desc;

    /*
     * Validity flag.
     */
    bool is_valid;
};

/*
 * Check that a value is a multiple of the page size.
 */
static bool is_page_aligned(uint64_t value)
{
    return (value & ((1ull << MANVIL_KBASE_PAGE_SHIFT) - 1ull)) == 0;
}

/*
 * Validate a descriptor before issuing the ioctl.
 *
 * Returns 0 if valid, -1 with errno set otherwise.
 */
static int desc_validate(const struct manvil_heap_desc *desc)
{
    if (desc->chunk_size == 0) {
        errno = EINVAL;
        return -1;
    }

    if (!is_page_aligned(desc->chunk_size)) {
        errno = EINVAL;
        return -1;
    }

    if (desc->chunk_size > MANVIL_HEAP_MAX_CHUNK_SIZE_BYTES) {
        errno = EINVAL;
        return -1;
    }

    if (desc->initial_chunks == 0) {
        errno = EINVAL;
        return -1;
    }

    if (desc->initial_chunks > desc->max_chunks) {
        errno = EINVAL;
        return -1;
    }

    if (desc->target_in_flight == 0) {
        errno = EINVAL;
        return -1;
    }

    return 0;
}

void manvil_heap_desc_default(struct manvil_heap_desc *desc)
{
    if (desc == NULL) {
        return;
    }

    memset(desc, 0, sizeof(*desc));
    desc->chunk_size       = MANVIL_HEAP_DEFAULT_CHUNK_SIZE;
    desc->initial_chunks   = MANVIL_HEAP_DEFAULT_INITIAL_CHUNKS;
    desc->max_chunks       = MANVIL_HEAP_DEFAULT_MAX_CHUNKS;
    desc->target_in_flight = MANVIL_HEAP_DEFAULT_TARGET_IN_FLIGHT;
    desc->buf_desc_va      = 0;
}

/*
 * Issue the CS_TILER_HEAP_INIT ioctl.
 *
 * Selects the v2 variant when the kernel supports the buf_desc_va
 * feature, and the v1 variant otherwise. The two variants share the
 * ioctl number and the first fields; the v2 form adds an eight byte
 * buffer descriptor field at the end of the input.
 */
static int heap_do_init(manvil_heap *heap)
{
    bool has_buf_desc =
        manvil_kbase_has_feature(heap->kbase, MANVIL_FEAT_BUF_DESC_VA);

    if (has_buf_desc) {
        union manvil_kbase_ioctl_cs_tiler_heap_init_v2 args;
        memset(&args, 0, sizeof(args));

        args.in.chunk_size       = heap->desc.chunk_size;
        args.in.initial_chunks   = heap->desc.initial_chunks;
        args.in.max_chunks       = heap->desc.max_chunks;
        args.in.target_in_flight = heap->desc.target_in_flight;
        args.in.group_id         = 0;
        args.in.buf_desc_va      = heap->desc.buf_desc_va;

        int rc = manvil_kbase_ioctl(heap->kbase,
                                    MANVIL_KBASE_IOCTL_CS_TILER_HEAP_INIT_V2,
                                    &args,
                                    "CS_TILER_HEAP_INIT(v2)");
        if (rc < 0) {
            return -1;
        }

        heap->gpu_va         = args.out.gpu_heap_va;
        heap->first_chunk_va = args.out.first_chunk_va;
    } else {
        union manvil_kbase_ioctl_cs_tiler_heap_init_v1 args;
        memset(&args, 0, sizeof(args));

        args.in.chunk_size       = heap->desc.chunk_size;
        args.in.initial_chunks   = heap->desc.initial_chunks;
        args.in.max_chunks       = heap->desc.max_chunks;
        args.in.target_in_flight = heap->desc.target_in_flight;
        args.in.group_id         = 0;

        int rc = manvil_kbase_ioctl(heap->kbase,
                                    MANVIL_KBASE_IOCTL_CS_TILER_HEAP_INIT_V1,
                                    &args,
                                    "CS_TILER_HEAP_INIT(v1)");
        if (rc < 0) {
            return -1;
        }

        heap->gpu_va         = args.out.gpu_heap_va;
        heap->first_chunk_va = args.out.first_chunk_va;
    }

    return 0;
}

manvil_heap *manvil_heap_create(manvil_kbase *kbase,
                                 const struct manvil_heap_desc *desc)
{
    if (kbase == NULL || desc == NULL) {
        errno = EINVAL;
        return NULL;
    }

    if (desc_validate(desc) < 0) {
        return NULL;
    }

    manvil_heap *heap = calloc(1, sizeof(*heap));
    if (heap == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    heap->kbase      = kbase;
    heap->desc       = *desc;
    heap->is_valid   = false;

    if (heap_do_init(heap) < 0) {
        free(heap);
        return NULL;
    }

    heap->is_valid = true;
    return heap;
}

void manvil_heap_destroy(manvil_heap *heap)
{
    if (heap == NULL) {
        return;
    }

    /*
     * Invalidate first so that no caller can use the handle after
     * destroy begins.
     */
    bool was_valid = heap->is_valid;
    heap->is_valid = false;

    if (was_valid) {
        struct manvil_kbase_ioctl_cs_tiler_heap_term args;
        memset(&args, 0, sizeof(args));
        args.gpu_heap_va = heap->gpu_va;

        int rc = manvil_kbase_ioctl(heap->kbase,
                                    MANVIL_KBASE_IOCTL_CS_TILER_HEAP_TERM,
                                    &args,
                                    "CS_TILER_HEAP_TERM");
        if (rc < 0) {
            /*
             * The ioctl may fail if the heap was already released
             * by the kernel, for example after a GPU reset. In any
             * case the userspace structure is freed below. The
             * kernel releases its own state when the context is
             * closed.
             */
        }
    }

    memset(heap, 0, sizeof(*heap));
    free(heap);
}

uint64_t manvil_heap_gpu_va(const manvil_heap *heap)
{
    return heap != NULL ? heap->gpu_va : 0;
}

uint64_t manvil_heap_first_chunk_va(const manvil_heap *heap)
{
    return heap != NULL ? heap->first_chunk_va : 0;
}

bool manvil_heap_is_valid(const manvil_heap *heap)
{
    return heap != NULL && heap->is_valid;
}

const struct manvil_heap_desc *manvil_heap_desc_of(const manvil_heap *heap)
{
    return heap != NULL ? &heap->desc : NULL;
}
