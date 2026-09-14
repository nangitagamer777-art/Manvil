/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * KCPU command queue.
 *
 * The KCPU is the kernel side command channel that runs in parallel
 * with the GPU command queues. It carries operations that the kernel
 * itself must perform, either immediately or when a dependency is
 * satisfied. The KCPU is the channel used for synchronization
 * primitives, memory imports, and the just in time memory allocator.
 *
 * The kernel exposes three ioctls for KCPU:
 *   KCPU_QUEUE_CREATE  returns a small identifier
 *   KCPU_QUEUE_ENQUEUE submits a batch of commands
 *   KCPU_QUEUE_DELETE  releases the queue
 *
 * A KCPU queue is per context. Manvil typically creates one and uses
 * it for all synchronization and ancillary work.
 *
 * Command submission works as follows:
 *   1. The caller appends one or more commands to an internal batch.
 *   2. The batch is copied into a GPU buffer allocated by Manvil.
 *   3. KCPU_QUEUE_ENQUEUE is called with a pointer to the buffer and
 *      the number of commands.
 *   4. The kernel reads the commands and processes them in order.
 *      Commands that produce a result write it to a shared memory
 *      location that the caller provided when the command was added.
 *
 * The current version of this module supports the most commonly used
 * command types. Additional types will be added as higher layers need
 * them.
 */

#ifndef MANVIL_KCPU_H
#define MANVIL_KCPU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel_api/manvil_kbase.h"
#include "mem/mem.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Opaque handle to a KCPU queue.
 */
typedef struct manvil_kcpu manvil_kcpu;

/*
 * Maximum number of commands that can be queued in a single submit.
 *
 * The kernel stores 256 entries in its own command array. Manvil
 * matches that limit so that no submit is ever partially accepted.
 */
#define MANVIL_KCPU_MAX_COMMANDS 256

/*
 * Default size of the shared result buffer.
 *
 * The result buffer is GPU memory that the kernel can write to when a
 * command needs to return a value. It is read by the CPU after the
 * kernel signals completion. Manvil allocates one page by default,
 * which is enough for dozens of concurrent results.
 */
#define MANVIL_KCPU_DEFAULT_RESULT_PAGES 1u

/*
 * Fence wait timeout constant.
 *
 * A value of INT64_MAX means "wait indefinitely". Any other positive
 * value is interpreted as a timeout in nanoseconds. Zero means
 * "do not wait" (used for polling style waits).
 */
#define MANVIL_KCPU_FENCE_WAIT_FOREVER INT64_MAX
#define MANVIL_KCPU_FENCE_NO_WAIT      0

/*
 * Create a KCPU queue.
 *
 * Returns a handle on success, NULL on failure. On failure, errno is
 * left at the value reported by the failing ioctl whenever possible.
 *
 * The creation sequence is:
 *   1. KCPU_QUEUE_CREATE to obtain the queue identifier
 *   2. allocate the command buffer and the result buffer
 *   3. initialize the batch tracking
 */
manvil_kcpu *manvil_kcpu_create(manvil_kbase *kbase);

/*
 * Destroy a KCPU queue.
 *
 * Passing NULL is a no-op.
 *
 * The handle is invalidated first. Any pending command batch is
 * discarded. The ioctl KCPU_QUEUE_DELETE is issued if the queue was
 * created. The command and result buffers are freed.
 */
void manvil_kcpu_destroy(manvil_kcpu *kcpu);

/*
 * Reset the command batch.
 *
 * Discards any commands that were added but not yet submitted. The
 * command buffer is left in place, only the count is reset.
 *
 * This is called automatically by manvil_kcpu_submit after a
 * successful submission.
 */
void manvil_kcpu_reset_batch(manvil_kcpu *kcpu);

/*
 * Number of commands currently in the batch.
 */
uint32_t manvil_kcpu_batch_count(const manvil_kcpu *kcpu);

/*
 * Accessors for the underlying ioctl identifier.
 */
uint8_t manvil_kcpu_id(const manvil_kcpu *kcpu);
bool    manvil_kcpu_is_valid(const manvil_kcpu *kcpu);

/*
 * Add commands to the batch.
 *
 * Each function appends one command to the internal buffer and
 * returns 0 on success, -1 on failure. Failure modes:
 *   EINVAL  arguments invalid
 *   ENOSPC  batch is full
 *   ENOMEM  internal allocation for the command payload failed
 *
 * The CQS forms accept the GPU virtual address of a CQS object,
 * previously allocated by the caller with manvil_mem_alloc. The
 * object is a u32 or u64 value in GPU memory.
 */

/*
 * Signal a CQS object with a new value.
 *
 * Sets the object to value using a plain assignment.
 */
int manvil_kcpu_add_cqs_set(manvil_kcpu *kcpu,
                             uint64_t cqs_gpu_va,
                             uint32_t value);

int manvil_kcpu_add_cqs_set64(manvil_kcpu *kcpu,
                               uint64_t cqs_gpu_va,
                               uint64_t value);

/*
 * Signal a CQS object using an operation.
 *
 * op       MANVIL_BASEP_CQS_SET_OPERATION_ADD or _SET
 * data_type MANVIL_BASEP_CQS_DATA_TYPE_U32 or _U64
 */
int manvil_kcpu_add_cqs_set_operation(manvil_kcpu *kcpu,
                                       uint64_t cqs_gpu_va,
                                       uint64_t value,
                                       uint8_t op,
                                       uint8_t data_type);

/*
 * Wait on a CQS object until it equals value.
 *
 * The wait is satisfied when the object is greater than or equal to
 * value. The exact semantics depend on the kernel implementation; for
 * compatibility, prefer the operation form below with an explicit
 * comparison.
 */
int manvil_kcpu_add_cqs_wait(manvil_kcpu *kcpu,
                              uint64_t cqs_gpu_va,
                              uint32_t value);

/*
 * Wait on a CQS object using an operation.
 *
 * op       MANVIL_BASEP_CQS_WAIT_OPERATION_LE or _GT
 * data_type MANVIL_BASEP_CQS_DATA_TYPE_U32 or _U64
 */
int manvil_kcpu_add_cqs_wait_operation(manvil_kcpu *kcpu,
                                        uint64_t cqs_gpu_va,
                                        uint64_t value,
                                        uint8_t op,
                                        uint8_t data_type);

/*
 * Signal a fence.
 *
 * The fence value is a GPU address that will be written by the
 * kernel when the fence is signaled. Higher layers allocate it with
 * manvil_mem_alloc and read it after synchronization.
 */
int manvil_kcpu_add_fence_signal(manvil_kcpu *kcpu,
                                  uint64_t fence_va);

/*
 * Wait on a fence.
 *
 * timeout_ns   Nanoseconds to wait. Use MANVIL_KCPU_FENCE_WAIT_FOREVER
 *              for an unbounded wait, or MANVIL_KCPU_FENCE_NO_WAIT for
 *              a non-blocking check.
 *
 * The fence is a value at the given GPU address. The kernel treats
 * the wait as satisfied when the value at that address becomes
 * non-zero.
 */
int manvil_kcpu_add_fence_wait(manvil_kcpu *kcpu,
                                int64_t timeout_ns);

/*
 * Error barrier.
 *
 * The kernel stops processing commands after an error until the
 * barrier is reached. No payload.
 */
int manvil_kcpu_add_error_barrier(manvil_kcpu *kcpu);

/*
 * Submit the accumulated batch to the kernel.
 *
 * On success, the batch is reset and all commands are in flight.
 * On failure, the batch is preserved so that the caller may inspect
 * or retry. Failure modes:
 *   EINVAL  the queue is invalid or the batch is empty
 *   EIO     the kernel rejected the enqueue
 *
 * The kernel processes commands in order. Commands that block do so
 * without holding the CPU; later commands in the same batch wait for
 * earlier ones to complete.
 */
int manvil_kcpu_submit(manvil_kcpu *kcpu);

/*
 * Access the shared result buffer.
 *
 * Higher layers that need to read a value written by the kernel use
 * these functions. The buffer is GPU memory, mapped into the process
 * address space, so its CPU pointer is directly readable.
 */
manvil_mem *manvil_kcpu_result_mem(const manvil_kcpu *kcpu);
void       *manvil_kcpu_result_cpu_ptr(const manvil_kcpu *kcpu);
uint64_t    manvil_kcpu_result_gpu_va(const manvil_kcpu *kcpu);
uint64_t    manvil_kcpu_result_size(const manvil_kcpu *kcpu);

#ifdef __cplusplus
}
#endif

#endif /* MANVIL_KCPU_H */
