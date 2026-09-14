/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * CSF command queues.
 *
 * A queue is the channel through which userspace sends commands to
 * the firmware. Each queue is bound to one Command Stream Interface
 * (CSI) within a group. The kernel maps three pages into the process
 * address space when the queue is bound: the input page, the output
 * page, and the doorbell page.
 *
 * The ring buffer lives in GPU memory. Userspace writes command
 * entries into the ring and advances CS_INSERT to tell the firmware
 * how much new data is available. The firmware reads entries between
 * CS_EXTRACT and CS_INSERT, updates CS_EXTRACT on the output page as
 * it consumes them, and rings the doorbell when a new entry is ready
 * to be processed.
 *
 * Manvil hides the ring bookkeeping behind a small API. Higher layers
 * write opaque command bytes and call kick. The queue layer takes
 * care of the circular buffer, the wrap-around, and the doorbell.
 *
 * Scope of this module:
 *   - allocation of the ring buffer
 *   - register, bind, and unregister of the queue with the kernel
 *   - mapping of the input and output pages
 *   - append semantics for command entries
 *   - kick through the doorbell
 *
 * Out of scope for now:
 *   - synchronization objects (handled in csf/sync)
 *   - command entry construction (handled in cmd)
 */

#ifndef MANVIL_CSF_QUEUE_H
#define MANVIL_CSF_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel_api/manvil_kbase.h"
#include "csf/group.h"
#include "mem/mem.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Opaque handle to a command queue.
 */
typedef struct manvil_queue manvil_queue;

/*
 * Default ring size in bytes.
 *
 * The ring is circular and one byte of capacity is reserved to
 * distinguish full from empty. The usable capacity is therefore
 * ring_size - 1 bytes.
 *
 * 64 KiB is a reasonable default for batch submissions. Larger rings
 * waste GPU memory, smaller rings fill up too quickly when a shader
 * batch is queued.
 */
#define MANVIL_QUEUE_DEFAULT_RING_SIZE (64u * 1024u)

/*
 * Minimum ring size accepted by the kernel, in bytes.
 *
 * The kernel requires at least one page for the ring. Manvil uses the
 * same lower bound as a sanity check.
 */
#define MANVIL_QUEUE_MIN_RING_SIZE (4u * 1024u)

/*
 * Number of pages mapped per bound queue. The kernel maps three:
 *   page 0  CS_USER_INPUT   written by userspace, read by firmware
 *   page 1  CS_USER_OUTPUT  written by firmware, read by userspace
 *   page 2  doorbell        written by userspace to signal work
 */
#define MANVIL_QUEUE_USER_IO_PAGES 3

/*
 * Layout of the three pages mapped at queue bind time.
 *
 * The order is fixed by the kernel: the first page holds the
 * hardware doorbell, the second holds the CS input registers, the
 * third holds the CS output registers. An earlier version of this
 * header had the pages in the wrong order, which made the firmware
 * never observe the published insert offset.
 *
 * +0x0000  doorbell   hardware page, written to signal new work
 * +0x1000  input      CS_INSERT and CS_EXTRACT_INIT
 * +0x2000  output     CS_EXTRACT and CS_ACTIVE
 */
#define MANVIL_QUEUE_OFFSET_DOORBELL (0u * 0x1000u)
#define MANVIL_QUEUE_OFFSET_INPUT    (1u * 0x1000u)
#define MANVIL_QUEUE_OFFSET_OUTPUT   (2u * 0x1000u)

/*
 * Register offsets inside the input and output pages.
 *
 * Both CS_INSERT and CS_EXTRACT are 64-bit values. CS_ACTIVE is a
 * 32-bit value. These definitions match the public UAPI header
 * distributed with Mesa 3D.
 */
#define MANVIL_CS_INSERT_OFFSET        0x0000u
#define MANVIL_CS_EXTRACT_INIT_OFFSET  0x0008u
#define MANVIL_CS_EXTRACT_OFFSET       0x0000u
#define MANVIL_CS_ACTIVE_OFFSET        0x0008u

/*
 * Create a queue, register it, bind it to a group, and map the user
 * IO pages.
 *
 * kbase            Backend handle.
 * group            Group the queue will be bound to. Must be valid.
 * ring_size_bytes  Size of the ring buffer. Zero selects the default
 *                  size (64 KiB). The value is rounded up to a page
 *                  boundary and clamped to the minimum.
 * csi_index        Command Stream Interface within the group. On the
 *                  supported hardware only index 0 is known to
 *                  execute, but the value is passed through unchanged
 *                  so that future hardware can use additional CSIs.
 *
 * On success, returns a handle. On failure, returns NULL and errno is
 * left at the value reported by the failing ioctl whenever possible.
 *
 * The ring buffer is allocated with read-write flags and cleared to
 * zero. The user IO pages are mapped with read-write access and both
 * CS_INSERT and CS_EXTRACT are read from the input and output pages.
 */
manvil_queue *manvil_queue_create(manvil_kbase *kbase,
                                   manvil_group *group,
                                   uint32_t ring_size_bytes,
                                   uint8_t csi_index);

/*
 * Destroy a queue.
 *
 * Passing NULL is a no-op.
 *
 * The queue is invalidated first, so that no caller can use the
 * handle after destroy begins. Then the kernel terminate ioctl is
 * issued, the user IO mapping is unmapped, and the ring buffer is
 * freed.
 *
 * The user IO mapping is unmapped explicitly because the kernel does
 * not tear it down when the queue is terminated. The ring buffer
 * mapping is not unmapped explicitly; the kernel releases it as part
 * of MEM_FREE, and an early munmap on a SAME_VA region would
 * invalidate the GPU mapping before the region is released.
 */
void manvil_queue_destroy(manvil_queue *queue);

/*
 * Append a command entry to the ring.
 *
 * data   Pointer to the entry bytes.
 * size   Number of bytes. Must be a multiple of 8.
 *
 * The entry is copied into the ring in a way that handles
 * wrap-around. CS_INSERT is updated in the local cache but is not
 * published to the firmware until manvil_queue_kick is called. This
 * allows a batch of entries to be built and committed with a single
 * doorbell write.
 *
 * Returns 0 on success, or -1 with errno set:
 *   EINVAL   if the arguments are invalid
 *   ENOSPC   if the ring does not have enough free space
 */
int manvil_queue_write(manvil_queue *queue, const void *data, size_t size);

/*
 * Number of bytes currently in flight, between CS_EXTRACT and the
 * local CS_INSERT.
 */
uint64_t manvil_queue_space_used(const manvil_queue *queue);

/*
 * Number of bytes available for new entries. This is the usable
 * capacity minus the space currently used.
 */
uint64_t manvil_queue_space_free(const manvil_queue *queue);

/*
 * Publish the current CS_INSERT and ring the doorbell.
 *
 * The local CS_INSERT is written to the input page, then a non-zero
 * value is written to the doorbell page. The firmware observes the
 * doorbell and starts reading entries from CS_EXTRACT up to
 * CS_INSERT.
 *
 * Returns 0 on success, -1 on failure.
 */
int manvil_queue_kick(manvil_queue *queue);

/*
 * Accessors. All return zero or NULL if the argument is NULL.
 */
manvil_mem *manvil_queue_ring_mem(const manvil_queue *queue);
uint64_t    manvil_queue_ring_gpu_va(const manvil_queue *queue);
void       *manvil_queue_ring_cpu_ptr(const manvil_queue *queue);
uint32_t    manvil_queue_ring_size(const manvil_queue *queue);
uint8_t     manvil_queue_csi_index(const manvil_queue *queue);
manvil_group *manvil_queue_group(const manvil_queue *queue);
bool        manvil_queue_is_valid(const manvil_queue *queue);

/*
 * Read the current CS_INSERT and CS_EXTRACT values as seen by
 * userspace. CS_INSERT reflects the local cache, which is ahead of
 * the kernel until kick is called. CS_EXTRACT reflects the firmware
 * progress.
 */
uint64_t manvil_queue_cs_insert(const manvil_queue *queue);
uint64_t manvil_queue_cs_extract(const manvil_queue *queue);

/*
 * True if the queue has no entries in flight, meaning CS_EXTRACT has
 * caught up to CS_INSERT.
 */
bool manvil_queue_is_idle(const manvil_queue *queue);

/*
 * Raw access to the user IO mapping, for diagnostics and for
 * specialized layers that need to read the individual registers.
 *
 * These pointers refer to the mapping created at bind time. They
 * remain valid until the queue is destroyed.
 */
void *manvil_queue_user_input_ptr(const manvil_queue *queue);
void *manvil_queue_user_output_ptr(const manvil_queue *queue);
void *manvil_queue_doorbell_ptr(const manvil_queue *queue);

#ifdef __cplusplus
}
#endif

#endif /* MANVIL_CSF_QUEUE_H */
