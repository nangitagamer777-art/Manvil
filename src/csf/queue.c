/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * CSF command queue implementation.
 */

#include "queue.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "kernel_api/abi/manvil_abi.h"

/*
 * Internal queue structure.
 *
 * cs_insert_value is the local cache of the CS_INSERT register. It is
 * ahead of the kernel until kick publishes it. Reads of CS_EXTRACT
 * always go to the mapped output page, because the firmware updates
 * that field independently.
 */
struct manvil_queue {
    manvil_kbase *kbase;
    manvil_group *group;

    /*
     * Configuration.
     */
    uint32_t ring_size;
    uint8_t  csi_index;

    /*
     * Ring buffer. Allocated internally, freed on destroy.
     */
    manvil_mem *ring_mem;

    /*
     * User IO mapping, three pages.
     */
    void  *user_io_ptr;
    size_t user_io_size;

    /*
     * Cached pointers into the mapping.
     *
     * cs_insert and cs_extract point to u64 fields in the input and
     * output pages. The doorbell is a u32 hardware register. All
     * three are volatile because the firmware and the hardware
     * update or observe them without going through the kernel.
     */
    volatile uint64_t *cs_insert;
    volatile uint64_t *cs_extract;
    volatile uint32_t *doorbell;

    /*
     * Local copy of CS_INSERT. Not published until kick.
     *
     * The value is a 64-bit unsigned integer to match the width of
     * the CS_INSERT register itself. The ring size is much smaller,
     * but keeping the same width avoids casts throughout the code
     * and makes the arithmetic consistent with the register.
     */
    uint64_t cs_insert_value;

    /*
     * State flags.
     */
    bool is_registered;
    bool is_bound;
    bool is_valid;
};

/*
 * Round a size up to the next multiple of the page size.
 */
static uint32_t page_align_up_u32(uint32_t value)
{
    const uint32_t page = 1u << MANVIL_KBASE_PAGE_SHIFT;
    return (value + page - 1u) & ~(page - 1u);
}

/*
 * Select the ring size from the caller supplied value.
 *
 * Zero means "use the default". Non-zero values are rounded up to a
 * page boundary and clamped to the minimum size.
 */
static uint32_t select_ring_size(uint32_t requested)
{
    uint32_t size = requested == 0 ? MANVIL_QUEUE_DEFAULT_RING_SIZE
                                   : requested;

    if (size < MANVIL_QUEUE_MIN_RING_SIZE) {
        size = MANVIL_QUEUE_MIN_RING_SIZE;
    }

    size = page_align_up_u32(size);
    return size;
}

/*
 * Issue CS_QUEUE_REGISTER for the ring buffer.
 *
 * The kernel takes the GPU virtual address of the ring buffer and its
 * size in bytes. Priority is zero because Manvil does not use the
 * per queue priority mechanism; the group priority governs scheduling.
 */
static int queue_do_register(manvil_queue *queue)
{
    struct manvil_kbase_ioctl_cs_queue_register args;
    memset(&args, 0, sizeof(args));

    args.buffer_gpu_addr = manvil_mem_gpu_va(queue->ring_mem);
    args.buffer_size     = queue->ring_size;
    args.priority        = 0;

    fprintf(stderr, "[manvil] CS_QUEUE_REGISTER args: "
                    "buffer_gpu_addr=0x%016llx buffer_size=%u priority=%u\n",
                    (unsigned long long)args.buffer_gpu_addr,
                    args.buffer_size, args.priority);

    int rc = manvil_kbase_ioctl(queue->kbase,
                                MANVIL_KBASE_IOCTL_CS_QUEUE_REGISTER,
                                &args,
                                "CS_QUEUE_REGISTER");
    if (rc < 0) {
        return -1;
    }

    queue->is_registered = true;
    return 0;
}

/*
 * Issue CS_QUEUE_BIND for the queue, then map the user IO pages.
 *
 * The bind call returns an mmap handle. Mapping that handle with the
 * device file descriptor gives three contiguous pages: input, output,
 * and doorbell.
 */
static int queue_do_bind(manvil_queue *queue)
{
    union manvil_kbase_ioctl_cs_queue_bind args;
    memset(&args, 0, sizeof(args));

    args.in.buffer_gpu_addr = manvil_mem_gpu_va(queue->ring_mem);
    args.in.group_handle    = manvil_group_handle(queue->group);
    args.in.csi_index       = queue->csi_index;

    int rc = manvil_kbase_ioctl(queue->kbase,
                                MANVIL_KBASE_IOCTL_CS_QUEUE_BIND,
                                &args,
                                "CS_QUEUE_BIND");
    if (rc < 0) {
        return -1;
    }

    uint64_t mmap_handle = args.out.mmap_handle;
    size_t   mmap_size   = (size_t)MANVIL_QUEUE_USER_IO_PAGES * 0x1000u;

    void *addr = mmap(NULL, mmap_size,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED,
                      manvil_kbase_fd(queue->kbase),
                      (off_t)mmap_handle);
    if (addr == MAP_FAILED) {
        /*
         * The kernel has recorded the bind but the mapping failed.
         * Terminate the queue so that the kernel releases the bind
         * state. The caller sees the failure.
         */
        struct manvil_kbase_ioctl_cs_queue_terminate term;
        memset(&term, 0, sizeof(term));
        term.buffer_gpu_addr = manvil_mem_gpu_va(queue->ring_mem);
        (void)manvil_kbase_ioctl(queue->kbase,
                                 MANVIL_KBASE_IOCTL_CS_QUEUE_TERMINATE,
                                 &term,
                                 "CS_QUEUE_TERMINATE (bind rollback)");
        queue->is_registered = false;
        return -1;
    }

    queue->user_io_ptr  = addr;
    queue->user_io_size = mmap_size;

    /*
     * Cache the pointers to the individual registers.
     *
     * The three pages of the user IO mapping are, in order: the
     * doorbell, the input page, and the output page. The input page
     * holds CS_INSERT as a 64-bit value. The output page holds
     * CS_EXTRACT as a 64-bit value. The doorbell is a 32-bit
     * hardware register.
     */
    uint8_t *base = (uint8_t *)addr;
    queue->cs_insert = (volatile uint64_t *)
        (base + MANVIL_QUEUE_OFFSET_INPUT + MANVIL_CS_INSERT_OFFSET);
    queue->cs_extract = (volatile uint64_t *)
        (base + MANVIL_QUEUE_OFFSET_OUTPUT + MANVIL_CS_EXTRACT_OFFSET);
    queue->doorbell = (volatile uint32_t *)
        (base + MANVIL_QUEUE_OFFSET_DOORBELL);

    /*
     * Initialize CS_INSERT to zero, so that the ring starts empty
     * from the firmware perspective.
     */
    *queue->cs_insert = 0;
    queue->cs_insert_value = 0;

    queue->is_bound = true;
    return 0;
}

/*
 * Issue CS_QUEUE_TERMINATE for the queue.
 *
 * Safe to call when the queue is registered but not bound. No-op when
 * neither flag is set.
 */
static void queue_do_terminate(manvil_queue *queue)
{
    if (!queue->is_registered && !queue->is_bound) {
        return;
    }

    struct manvil_kbase_ioctl_cs_queue_terminate args;
    memset(&args, 0, sizeof(args));
    args.buffer_gpu_addr = manvil_mem_gpu_va(queue->ring_mem);

    (void)manvil_kbase_ioctl(queue->kbase,
                             MANVIL_KBASE_IOCTL_CS_QUEUE_TERMINATE,
                             &args,
                             "CS_QUEUE_TERMINATE");

    queue->is_registered = false;
    queue->is_bound      = false;
}

/*
 * Reset a region of the ring to zero.
 */
static void ring_zero(manvil_queue *queue)
{
    void *ring = manvil_mem_cpu_ptr(queue->ring_mem);
    if (ring != NULL) {
        memset(ring, 0, queue->ring_size);
    }
}

manvil_queue *manvil_queue_create(manvil_kbase *kbase,
                                   manvil_group *group,
                                   uint32_t ring_size_bytes,
                                   uint8_t csi_index)
{
    if (kbase == NULL || !manvil_group_is_valid(group)) {
        errno = EINVAL;
        return NULL;
    }

    /*
     * Allocate the tracking structure first so that cleanup is easy
     * if any of the subsequent steps fail.
     */
    manvil_queue *queue = calloc(1, sizeof(*queue));
    if (queue == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    queue->kbase     = kbase;
    queue->group     = group;
    queue->ring_size = select_ring_size(ring_size_bytes);
    queue->csi_index = csi_index;
    queue->is_valid  = false;

    /*
     * Allocate the ring buffer as a plain read-write GPU memory
     * region. The kernel forces SAME_VA on 64 bit processes for non
     * executable allocations, so the CPU pointer is available for
     * userspace writes and matches the GPU address.
     */
    queue->ring_mem = manvil_mem_alloc(kbase, queue->ring_size,
                                       MANVIL_MEM_FLAGS_RW);
    if (queue->ring_mem == NULL) {
        free(queue);
        return NULL;
    }

    /*
     * Clear the ring. The firmware will read entries starting from
     * CS_EXTRACT = 0, and stale bytes from a previous allocation
     * would confuse it if it ever advanced past them.
     */
    ring_zero(queue);

    /*
     * Register the ring with the kernel.
     */
    if (queue_do_register(queue) < 0) {
        manvil_mem_free(queue->ring_mem);
        free(queue);
        return NULL;
    }

    /*
     * Bind the queue to the group and map the user IO pages.
     */
    if (queue_do_bind(queue) < 0) {
        manvil_mem_free(queue->ring_mem);
        free(queue);
        return NULL;
    }

    queue->is_valid = true;
    return queue;
}

/*
 * Compute the number of bytes currently in flight.
 *
 * The ring is circular. CS_INSERT is the local cache (ahead of the
 * kernel), CS_EXTRACT is read from the output page and reflects the
 * firmware progress.
 */
static uint64_t queue_bytes_in_flight(const manvil_queue *queue)
{
    uint64_t insert = queue->cs_insert_value;
    uint64_t extract = *queue->cs_extract;
    uint64_t size = queue->ring_size;

    if (insert >= extract) {
        return insert - extract;
    }

    /*
     * Wrap-around: the insert has passed the end of the buffer and
     * started over, while the extract is still near the end.
     */
    return size - extract + insert;
}

/*
 * Copy data into the ring at the current insert offset, handling
 * wrap-around.
 *
 * The destination is the ring memory mapped by manvil_mem. Its CPU
 * pointer equals the GPU address on SAME_VA allocations, so no
 * translation is required.
 *
 * The insert offset is taken modulo the ring size to locate the
 * actual byte position.
 */
static void queue_copy_into_ring(manvil_queue *queue,
                                  const uint8_t *src,
                                  size_t size)
{
    uint8_t *ring = (uint8_t *)manvil_mem_cpu_ptr(queue->ring_mem);
    uint32_t size_u32 = queue->ring_size;
    uint32_t pos = (uint32_t)(queue->cs_insert_value % size_u32);

    uint32_t first = size_u32 - pos;
    if (first > size) {
        first = (uint32_t)size;
    }

    memcpy(ring + pos, src, first);

    uint32_t remaining = (uint32_t)size - first;
    if (remaining > 0) {
        memcpy(ring, src + first, remaining);
    }
}

int manvil_queue_write(manvil_queue *queue, const void *data, size_t size)
{
    if (queue == NULL || !queue->is_valid || data == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (size == 0) {
        return 0;
    }

    /*
     * Entries are multiples of 8 bytes. The kernel expects the
     * CS_INSERT to advance by that amount and the firmware assumes
     * entries are aligned.
     */
    if ((size & 0x7u) != 0) {
        errno = EINVAL;
        return -1;
    }

    uint64_t used = queue_bytes_in_flight(queue);
    uint64_t capacity = queue->ring_size - 1u;

    if (used > capacity || size > capacity - used) {
        errno = ENOSPC;
        return -1;
    }

    queue_copy_into_ring(queue, (const uint8_t *)data, size);
    queue->cs_insert_value += (uint64_t)size;

    return 0;
}

uint64_t manvil_queue_space_used(const manvil_queue *queue)
{
    if (queue == NULL || !queue->is_valid) {
        return 0;
    }
    return queue_bytes_in_flight(queue);
}

uint64_t manvil_queue_space_free(const manvil_queue *queue)
{
    if (queue == NULL || !queue->is_valid) {
        return 0;
    }

    uint64_t used = queue_bytes_in_flight(queue);
    uint64_t capacity = queue->ring_size - 1u;
    return (used >= capacity) ? 0 : capacity - used;
}

int manvil_queue_kick(manvil_queue *queue)
{
    if (queue == NULL || !queue->is_valid || !queue->is_bound) {
        errno = EINVAL;
        return -1;
    }

    /*
     * Publish the local CS_INSERT so that the firmware sees the new
     * entries. The write must be ordered before the doorbell write
     * so that the firmware does not observe a doorbell without data.
     *
     * The volatile qualifier handles compiler reordering. The barrier
     * handles CPU reordering on weakly ordered architectures such as
     * AArch64.
     */
    *queue->cs_insert = queue->cs_insert_value;

    __sync_synchronize();

    /*
     * Ring the doorbell. Any non-zero value is sufficient; the
     * firmware does not interpret the value, only the transition
     * from zero to non-zero. Writing 1 is the conventional choice.
     */
    *queue->doorbell = 1u;

    /*
     * Notify the kernel scheduler through the CS_QUEUE_KICK ioctl.
     *
     * The doorbell write is a direct signal to the firmware and is
     * sufficient when the queue is already resident on a CSG slot.
     * A freshly bound queue is not yet resident: the kernel must
     * move it to RUNNABLE and ask the scheduler to assign it a
     * slot. That notification happens through this ioctl.
     *
     * Manvil issues the ioctl on every kick. The cost is a single
     * system call, and the kernel treats a redundant kick as a
     * no-op when the queue is already scheduled.
     */
    struct manvil_kbase_ioctl_cs_queue_kick args;
    memset(&args, 0, sizeof(args));
    args.buffer_gpu_addr = manvil_mem_gpu_va(queue->ring_mem);

    int rc = manvil_kbase_ioctl(queue->kbase,
                                MANVIL_KBASE_IOCTL_CS_QUEUE_KICK,
                                &args,
                                "CS_QUEUE_KICK");
    if (rc < 0) {
        return -1;
    }

    return 0;
}

void manvil_queue_destroy(manvil_queue *queue)
{
    if (queue == NULL) {
        return;
    }

    /*
     * Invalidate first. Once this is false, no caller can use the
     * queue, even if any of the ioctls below fail.
     */
    bool was_valid = queue->is_valid;
    queue->is_valid = false;

    if (was_valid) {
        /*
         * Ask the kernel to terminate the queue. This detaches the
         * queue from its group and stops the firmware from reading
         * from the ring.
         */
        queue_do_terminate(queue);
    }

    /*
     * Unmap the user IO pages. This is done explicitly because the
     * kernel does not tear down the userspace mapping when the queue
     * is terminated. The mapping covers three pages and was created
     * with mmap at bind time.
     */
    if (queue->user_io_ptr != NULL) {
        munmap(queue->user_io_ptr, queue->user_io_size);
        queue->user_io_ptr  = NULL;
        queue->user_io_size = 0;
        queue->cs_insert    = NULL;
        queue->cs_extract   = NULL;
        queue->doorbell     = NULL;
    }

    /*
     * Free the ring buffer. The MEM_FREE ioctl takes care of removing
     * the userspace mapping for the SAME_VA region, so no explicit
     * munmap is needed here.
     */
    if (queue->ring_mem != NULL) {
        manvil_mem_free(queue->ring_mem);
        queue->ring_mem = NULL;
    }

    memset(queue, 0, sizeof(*queue));
    free(queue);
}

/*
 * Accessors.
 */

manvil_mem *manvil_queue_ring_mem(const manvil_queue *queue)
{
    return queue != NULL ? queue->ring_mem : NULL;
}

uint64_t manvil_queue_ring_gpu_va(const manvil_queue *queue)
{
    return queue != NULL ? manvil_mem_gpu_va(queue->ring_mem) : 0;
}

void *manvil_queue_ring_cpu_ptr(const manvil_queue *queue)
{
    return queue != NULL ? manvil_mem_cpu_ptr(queue->ring_mem) : NULL;
}

uint32_t manvil_queue_ring_size(const manvil_queue *queue)
{
    return queue != NULL ? queue->ring_size : 0;
}

uint8_t manvil_queue_csi_index(const manvil_queue *queue)
{
    return queue != NULL ? queue->csi_index : 0;
}

manvil_group *manvil_queue_group(const manvil_queue *queue)
{
    return queue != NULL ? queue->group : NULL;
}

bool manvil_queue_is_valid(const manvil_queue *queue)
{
    return queue != NULL && queue->is_valid;
}

uint64_t manvil_queue_cs_insert(const manvil_queue *queue)
{
    return queue != NULL ? queue->cs_insert_value : 0;
}

uint64_t manvil_queue_cs_extract(const manvil_queue *queue)
{
    if (queue == NULL || queue->cs_extract == NULL) {
        return 0;
    }
    return *queue->cs_extract;
}

bool manvil_queue_is_idle(const manvil_queue *queue)
{
    if (queue == NULL) {
        return true;
    }
    return queue->cs_insert_value == manvil_queue_cs_extract(queue);
}

void *manvil_queue_user_input_ptr(const manvil_queue *queue)
{
    if (queue == NULL || queue->user_io_ptr == NULL) {
        return NULL;
    }
    return (uint8_t *)queue->user_io_ptr + MANVIL_QUEUE_OFFSET_INPUT;
}

void *manvil_queue_user_output_ptr(const manvil_queue *queue)
{
    if (queue == NULL || queue->user_io_ptr == NULL) {
        return NULL;
    }
    return (uint8_t *)queue->user_io_ptr + MANVIL_QUEUE_OFFSET_OUTPUT;
}

void *manvil_queue_doorbell_ptr(const manvil_queue *queue)
{
    if (queue == NULL || queue->user_io_ptr == NULL) {
        return NULL;
    }
    return (uint8_t *)queue->user_io_ptr + MANVIL_QUEUE_OFFSET_DOORBELL;
}
