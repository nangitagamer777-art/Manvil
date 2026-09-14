# Tiler Heap

The tiler heap is a chunked memory pool used by the firmware when rendering
large geometry workloads that do not fit in on-chip tiler memory.

## Initialization

KBASE_IOCTL_CS_TILER_HEAP_INIT takes:

chunk_size        size of each chunk in bytes
initial_chunks    number of chunks allocated at creation
max_chunks        upper limit on total chunks
target_in_flight  number of render passes the driver should keep in flight
group_id          physical memory group id
padding

Returns:

gpu_heap_va       GPU VA of the heap context block
first_chunk_va    GPU VA of the first chunk

The heap context must be re-armed by the firmware through a HEAP_SET
command in the command stream whenever the renderer switches heaps.

## Termination

KBASE_IOCTL_CS_TILER_HEAP_TERM takes the gpu_heap_va and releases the heap.

## Empirical findings

From the earlier fork implementation on the target hardware:

Chunk size is 1 MiB.

Max chunks is 200. Larger values risk triggering the Android OOM killer
under sustained load.

Renewal interval: the heap is renewed by creating a new heap context and
retiring the old one every 8 graphics submissions. This is controlled at
runtime and can be disabled.

Retirement rather than immediate destruction is required. Destroying the
old context right after the last submission leaves a dangling reference in
the firmware, which can then read arbitrary memory as a chunk list,
producing a wild-pointer fault at exception code 0xc0.

Heap statistics are validated by the kernel on every chunk grow request.
The kernel rejects requests where the reported statistics are inconsistent
or where nothing is in flight. The only shape that always validates is
emitting VERTEX_TILER_STARTED without the complementary COMPLETED events.
