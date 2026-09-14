# Tiler Heap Model

The kernel side representation of chunked tiler heaps.

## Heap structure

struct kbase_csf_tiler_heap {
    struct kbase_context *kctx;
    struct list_head link;
    u32 chunk_size;
    u32 chunk_count;
    u32 max_chunks;
    u16 target_in_flight;
    u64 gpu_va;
    u64 heap_id;
    struct list_head chunks_list;
};

The heap has a chunk size in bytes, a current chunk count, and a maximum
chunk count. The gpu_va field is the GPU virtual address of the heap
context structure allocated for the firmware. The heap_id is a unique
identifier.

## Chunk structure

struct kbase_csf_tiler_heap_chunk {
    struct list_head link;
    struct kbase_va_region *region;
    u64 gpu_va;
};

Each chunk has a GPU memory region and a GPU virtual address. The
address points to the chunk header, not to the free area.

## Chunk header layout

Each chunk starts with a 64-byte header. The header contains the
location and size of the next chunk in a single 64-bit value.

Bit layout of the header field:
  bits 0-11   size of next chunk in 4 KiB units
  bits 12-63  address of next chunk in 4 KiB units

CHUNK_HDR_SIZE = 64 bytes
CHUNK_HDR_NEXT_SIZE_POS = 0
CHUNK_HDR_NEXT_ADDR_POS = 12
CHUNK_HDR_NEXT_SIZE_ENCODE_SHIFT = 12
CHUNK_HDR_NEXT_ADDR_ENCODE_SHIFT = 12

The maximum chunk size is 4095 pages (about 16 MiB). The maximum
addressable offset is 2^52 pages (about 16 EiB).

Chunks are linked in a singly linked list. The last chunk in the chain
has an invalid size or address to mark the end.

## Chunk allocation

Chunks are fully backed by physical memory. They do not use page faults
or demand paging. This simplifies firmware operation at the cost of
higher memory usage.

Initial chunks are allocated when the heap is created. Additional
chunks are allocated in response to out-of-memory events from the
firmware.

## Heap context allocator

struct kbase_csf_heap_context_allocator {
    struct kbase_context *kctx;
    struct kbase_va_region *region;
    u64 gpu_va;
    struct mutex lock;
    DECLARE_BITMAP(in_use, MAX_TILER_HEAPS);
};

The allocator manages a single GPU memory region subdivided into
fixed-size slots, one per heap context. Up to 128 heap contexts per
context (MAX_TILER_HEAPS = 128).

Heap contexts are allocated lazily. When a heap is created, a slot is
reserved. When the heap is terminated, the slot is freed.

The heap context contains firmware-specific metadata. Its layout is not
part of the UAPI and is defined by the firmware image.

## Out of memory handling

When the firmware cannot grow the heap further (either because
chunk_count reached max_chunks or because no free chunk is available),
it writes CS_REQ_TILER_OOM in the stream's request register.

The kernel observes this and calls kbase_csf_tiler_heap_alloc_new_chunk.

int kbase_csf_tiler_heap_alloc_new_chunk(
    struct kbase_context *kctx,
    u64 gpu_heap_va,
    u32 nr_in_flight,
    u32 pending_frag_count,
    u64 *new_chunk_ptr);

The firmware passes the current in-flight count and the count of
completed vertex/tiler operations. The kernel decides whether a new
chunk can be added based on:
  - chunk_count < max_chunks
  - nr_in_flight <= target_in_flight
  - pending_frag_count < nr_in_flight

If the conditions are not met, the kernel returns -EBUSY. The firmware
retries on the next chunk grow request.

## Heap renewal strategy

The earlier fork used a renewal strategy on kernels where the
firmware chunk recycling did not work reliably.

The strategy is:
  1. Every N graphics submissions, create a new heap.
  2. Program the new heap with HEAP_SET in the command stream.
  3. Retire the old heap but do not destroy it immediately.
  4. Destroy the old heap only when both graphics subqueues have
     reissued HEAP_SET with the new heap, proving that the old one
     is no longer referenced.

The renewal interval must be tuned. Larger intervals save memory and
reduce overhead. Shorter intervals avoid memory exhaustion.

A default of 8 submissions was used. 1 MiB chunks, max 200.

## Coherency with the firmware

The firmware reads the heap context and walks the chunk list. It writes
back progress counters (CS_HEAP_VT_START, CS_HEAP_VT_END, CS_HEAP_FRAG_END).

The kernel validates these counters on chunk grow. Inconsistent values
cause the group to be terminated.

The known valid pattern is:
  - Emit only VERTEX_TILER_STARTED in the graphics stream.
  - Do not emit VERTEX_TILER_COMPLETED or FRAGMENT_COMPLETED.

This ensures that started >= completed is always true and that the
frag_end counter never exceeds vt_end.

## Manvil recommendations

Use 1 MiB chunks. Max 200 chunks. Renew every 8 submissions.

Set csi_handlers = BASE_CSF_TILER_OOM_EXCEPTION_FLAG if the kernel
supports it (UAPI 1.12 and later). Provide a buffer descriptor via
buf_desc_va if the kernel supports it (UAPI 1.14 and later).

Always terminate the group before destroying the heap. Never destroy
a heap that the firmware might still reference.

Handle the TILER_HEAP_OOM notification promptly. If handling takes
longer than the progress timeout, the group will be terminated by the
kernel.
