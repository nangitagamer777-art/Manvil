/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * Synchronization objects for the CSF.
 *
 * A manvil_sync is a 64-bit value in GPU memory that both the CPU
 * and the firmware can read and write. It is the primitive used for
 * fences, timeline semaphores, and any place where the CPU and the
 * GPU need to agree on a state.
 *
 * The memory layout follows the Sync64 object defined by the Kbase
 * UAPI:
 *
 *   offset 0   value   64-bit unsigned integer
 *   offset 8   error   64-bit unsigned integer
 *
 * The value is monotonic. Signalers add to it, waiters check against
 * it. The error field is written by the firmware when a signal
 * operation fails, and can be checked by the CPU after a wait.
 *
 * Coherency:
 *
 *   A sync object used within a single command stream group is
 *   allocated with plain read-write memory. Because the CPU and the
 *   firmware share the same physical memory and the firmware
 *   performs cache maintenance on the accesses it cares about,
 *   plain memory is sufficient.
 *
 *   A sync object used across groups (for example a semaphore shared
 *   between a vertex/tiler group and a fragment group) must be
 *   allocated with the CSF_EVENT flag. That flag forces the kernel
 *   to map the region as uncached, which is required for
 *   cross-group visibility. CSF_EVENT regions are permanently
 *   mapped and cannot be freed until the context is closed, so
 *   they should be used sparingly.
 *
 * Manvil creates one manvil_sync per synchronization primitive the
 * higher layers need. The underlying manvil_mem is owned by the sync
 * and released when the sync is destroyed.
 */

#ifndef MANVIL_SYNC_H
#define MANVIL_SYNC_H

#include <stdbool.h>
#include <stdint.h>

#include "kernel_api/manvil_kbase.h"
#include "mem/mem.h"
#include "kcpu/kcpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Opaque handle to a sync object.
 */
typedef struct manvil_sync manvil_sync;

/*
 * Size of the sync object in bytes. Matches Sync64.
 */
#define MANVIL_SYNC_SIZE_BYTES 16u

/*
 * Offset of the value field.
 */
#define MANVIL_SYNC_VALUE_OFFSET 0u

/*
 * Offset of the error field.
 */
#define MANVIL_SYNC_ERROR_OFFSET 8u

/*
 * Create a sync object initialized to value 0 and error 0.
 *
 * kbase         Backend handle.
 * cross_group   When true, the sync object is allocated with the
 *               CSF_EVENT flag so it is visible across command stream
 *               groups. When false, the sync object uses plain
 *               read-write memory, which is cheaper and sufficient
 *               for intra-group use.
 *
 * Returns a handle on success, NULL on failure. On failure, errno is
 * left at the value reported by the failing allocation whenever
 * possible.
 */
manvil_sync *manvil_sync_create(manvil_kbase *kbase, bool cross_group);

/*
 * Destroy a sync object.
 *
 * Passing NULL is a no-op.
 *
 * The handle is invalidated first, then the underlying memory is
 * released. Any signal or wait operation in flight on the kernel
 * side will be discarded by the kernel when it notices the memory is
 * going away.
 */
void manvil_sync_destroy(manvil_sync *sync);

/*
 * Accessors. All return zero or NULL if the argument is NULL.
 */
uint64_t    manvil_sync_gpu_va(const manvil_sync *sync);
uint64_t    manvil_sync_value(const manvil_sync *sync);
uint64_t    manvil_sync_error(const manvil_sync *sync);
manvil_mem *manvil_sync_mem(const manvil_sync *sync);
bool        manvil_sync_is_valid(const manvil_sync *sync);
bool        manvil_sync_is_cross_group(const manvil_sync *sync);

/*
 * Check whether the value has reached or passed a target.
 *
 * This is a CPU-side read of the mapped value. It does not block and
 * does not perform cache maintenance. For uncached or CSF_EVENT
 * objects the read reflects the latest firmware write immediately.
 * For cached objects the value may be stale until a sync from
 * device is performed.
 */
bool manvil_sync_is_signaled(const manvil_sync *sync, uint64_t target);

/*
 * Check whether the error field is non-zero.
 *
 * A non-zero error indicates that a signal or wait operation on this
 * object failed. Higher layers should abort the operation that
 * depended on the sync and report the failure.
 */
bool manvil_sync_has_error(const manvil_sync *sync);

/*
 * Clear the error field to zero.
 *
 * The error field is normally only written by the firmware. Manvil
 * clears it manually when reusing an object for a new round of
 * work, so that stale errors do not cause spurious failures.
 */
void manvil_sync_clear_error(manvil_sync *sync);

/*
 * Signal the sync object from the kernel side.
 *
 * Adds value to the object's current value using a KCPU command. The
 * command is appended to the KCPU batch and is executed by the
 * kernel when the batch is submitted. The caller is responsible for
 * calling manvil_kcpu_submit afterwards.
 *
 * Returns 0 on success, -1 on failure.
 */
int manvil_sync_signal_from_kcpu(manvil_kcpu *kcpu,
                                  manvil_sync *sync,
                                  uint64_t value);

/*
 * Wait on the sync object from the kernel side.
 *
 * Appends a KCPU wait command that blocks the KCPU queue until the
 * object satisfies the given operation against the target value.
 *
 * operation   MANVIL_BASEP_CQS_WAIT_OPERATION_LE or _GT.
 * value       target value to compare against.
 *
 * The caller is responsible for calling manvil_kcpu_submit afterwards.
 *
 * Returns 0 on success, -1 on failure.
 */
int manvil_sync_wait_from_kcpu(manvil_kcpu *kcpu,
                                manvil_sync *sync,
                                uint64_t value,
                                uint8_t operation);

/*
 * Wait on the sync object from the CPU side.
 *
 * Blocks the calling thread until the value reaches or passes
 * target_value, or the timeout expires.
 *
 * timeout_ns   Nanoseconds to wait. Use a negative value for an
 *              unbounded wait. Use 0 for a single check.
 *
 * The implementation polls the mapped value with a short sleep
 * between checks. This avoids the need for a kernel side wait queue
 * and keeps the operation simple. For uncached and CSF_EVENT objects
 * the polled value is always fresh. For cached objects the value may
 * be stale; callers should use the coherency helpers below.
 *
 * Returns:
 *    0   if the value reached or passed the target
 *    1   if the timeout expired before that happened
 *   -1   on error (invalid arguments, or a non-zero error field)
 */
int manvil_sync_wait_cpu(manvil_sync *sync,
                          uint64_t target_value,
                          int64_t timeout_ns);

/*
 * Cache maintenance helpers.
 *
 * These are only needed for sync objects allocated without the
 * CSF_EVENT flag and without CPU cache coherence, which is the
 * default for plain read-write memory on some configurations. They
 * issue a MEM_SYNC ioctl on the underlying memory.
 *
 * Higher layers that use cross-group syncs (CSF_EVENT) do not need
 * to call these. Higher layers that use intra-group syncs on
 * coherent memory do not need to call these either. They are
 * provided for completeness and for configurations where the CPU
 * cache is not coherent with the GPU.
 */
int manvil_sync_flush_to_device(manvil_sync *sync);
int manvil_sync_invalidate_from_device(manvil_sync *sync);

#ifdef __cplusplus
}
#endif

#endif /* MANVIL_SYNC_H */
