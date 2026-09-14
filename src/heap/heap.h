/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * Tiler heap.
 *
 * The tiler heap is the memory pool that the firmware uses for
 * polygon binning when rendering geometry. It is a chain of chunks,
 * each starting with a 64-byte header that points to the next chunk.
 * The heap context block, whose GPU virtual address is returned by
 * this module, holds the firmware side metadata for the heap.
 *
 * The heap is created by the CS_TILER_HEAP_INIT ioctl. From UAPI
 * 1.14 onward the ioctl also carries a buf_desc_va field, which lets
 * the application provide a buffer descriptor that the firmware uses
 * to reclaim chunks in place. When the kernel does not advertise the
 * corresponding feature, the field is ignored and the heap uses the
 * older semantics where the firmware never reclaims in place.
 *
 * Manvil does not implement heap renewal automatically. The renewal
 * strategy used by the reference implementation in the PanVK kbase
 * fork is documented in reference/_research/04_kernel_internals/
 * tiler_heap_model.md and is the responsibility of the graphics
 * pipeline that consumes the heap. The heap module only creates and
 * destroys heap objects.
 *
 * The three sizes that matter:
 *
 *   chunk_size        bytes per chunk. Must be page aligned and
 *                     must not exceed 4095 pages (the maximum the
 *                     chunk header can encode).
 *
 *   initial_chunks    number of chunks allocated at creation. Must
 *                     be at least 1 and no more than max_chunks.
 *
 *   max_chunks        upper bound on the number of chunks. When the
 *                     firmware needs more than this, the kernel
 *                     cannot grow the heap and the tiler OOM path
 *                     is triggered.
 *
 * The heap object returned by this module is small: it holds the
 * handles and parameters, but the actual memory lives inside the
 * kernel. Freeing the heap releases all chunks and the context.
 */

#ifndef MANVIL_HEAP_H
#define MANVIL_HEAP_H

#include <stdbool.h>
#include <stdint.h>

#include "kernel_api/manvil_kbase.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Opaque handle to a tiler heap.
 */
typedef struct manvil_heap manvil_heap;

/*
 * Default parameters, chosen to match the configuration used by the
 * reference implementation and confirmed on the target hardware.
 *
 *   chunk size   1 MiB
 *   initial      4 chunks
 *   maximum      200 chunks
 *   in flight    8 render passes
 *
 * The 200 chunk limit is deliberate. Larger values risk triggering
 * the Android low memory killer under sustained load.
 */
#define MANVIL_HEAP_DEFAULT_CHUNK_SIZE      (1u * 1024u * 1024u)
#define MANVIL_HEAP_DEFAULT_INITIAL_CHUNKS  4u
#define MANVIL_HEAP_DEFAULT_MAX_CHUNKS      200u
#define MANVIL_HEAP_DEFAULT_TARGET_IN_FLIGHT 8u

/*
 * Chunk size limits.
 *
 * The chunk header encodes the size of the next chunk in 12 bits,
 * in units of pages. The maximum encodable size is therefore 4095
 * pages. Manvil exposes the value as a byte count limit.
 */
#define MANVIL_HEAP_MAX_CHUNK_SIZE_BYTES \
    ((uint32_t)(4095u << MANVIL_KBASE_PAGE_SHIFT))

/*
 * Heap descriptor.
 *
 * All fields map directly to the input of CS_TILER_HEAP_INIT. The
 * buf_desc_va field is optional. When the kernel does not advertise
 * the MANVIL_FEAT_BUF_DESC_VA feature, the field is ignored and a
 * zero value is sent to the kernel.
 */
struct manvil_heap_desc {
    uint32_t chunk_size;
    uint32_t initial_chunks;
    uint32_t max_chunks;
    uint16_t target_in_flight;
    uint64_t buf_desc_va;
};

/*
 * Initialize a descriptor with the default parameters and a zero
 * buffer descriptor.
 *
 * Higher layers that want to use a buffer descriptor set the field
 * after calling this function. The value must be the GPU virtual
 * address of a buffer descriptor allocated by the caller.
 */
void manvil_heap_desc_default(struct manvil_heap_desc *desc);

/*
 * Create a tiler heap.
 *
 * On success, returns a handle. On failure, returns NULL and errno
 * is left at the value reported by the failing ioctl whenever
 * possible.
 *
 * The function performs its own validation before calling the
 * kernel:
 *   - chunk_size must be page aligned and at most
 *     MANVIL_HEAP_MAX_CHUNK_SIZE_BYTES
 *   - initial_chunks must be at least 1 and no more than max_chunks
 *   - target_in_flight must be at least 1
 *
 * The kernel may impose additional constraints that the ioctl will
 * reject with EINVAL.
 */
manvil_heap *manvil_heap_create(manvil_kbase *kbase,
                                 const struct manvil_heap_desc *desc);

/*
 * Destroy a tiler heap.
 *
 * Passing NULL is a no-op.
 *
 * The caller must ensure that no command stream is currently using
 * the heap when this function is called. In particular, the group
 * that owns the command stream referencing the heap must be idle or
 * already terminated. Violating this is undefined behavior; the
 * kernel does not synchronize with the firmware when the heap is
 * destroyed.
 *
 * The handle is invalidated first. Then the ioctl is issued. If the
 * ioctl fails, the userspace structure is still freed. The kernel
 * releases its own state when the context is closed.
 */
void manvil_heap_destroy(manvil_heap *heap);

/*
 * Accessors. All return zero or false if the argument is NULL.
 *
 * gpu_va            GPU virtual address of the heap context block.
 *                   This is the value that must be emitted in a
 *                   HEAP_SET command in the command stream.
 *
 * first_chunk_va    GPU virtual address of the first chunk. Points
 *                   to the chunk header, not to the free area. Used
 *                   for debugging and for higher layers that want
 *                   to inspect the chain.
 */
uint64_t manvil_heap_gpu_va(const manvil_heap *heap);
uint64_t manvil_heap_first_chunk_va(const manvil_heap *heap);
bool     manvil_heap_is_valid(const manvil_heap *heap);

/*
 * Read back the parameters that were used to create the heap.
 *
 * Returns a pointer to an internal structure that remains valid for
 * the lifetime of the heap. Callers that want a copy should memcpy
 * it.
 */
const struct manvil_heap_desc *manvil_heap_desc_of(const manvil_heap *heap);

#ifdef __cplusplus
}
#endif

#endif /* MANVIL_HEAP_H */
