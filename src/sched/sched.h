/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * Submission orchestration.
 *
 * The scheduler is a thin layer on top of the queue, sync, and
 * command modules. It turns a complete description of a submission
 * into a sequence of command entries written to a ring, optionally
 * preceded by waits and followed by signals, and commits the batch
 * with a single kick.
 *
 * The description is modeled on the Vulkan submission API. A
 * submission consists of:
 *
 *   - zero or more wait operations on sync objects, which must be
 *     satisfied before any command in the batch executes
 *   - zero or more command entries supplied by the caller
 *   - zero or more signal operations on sync objects, which take
 *     effect after all commands in the batch have been executed
 *
 * The waits and signals are emitted as SYNC_WAIT64 and SYNC_ADD64
 * commands in the ring. This makes the ordering enforcement happen
 * in the firmware, not in the kernel and not in the CPU. The queue
 * blocks in hardware until the wait conditions are satisfied, then
 * executes the commands, then signals the outputs.
 *
 * The kernel-level KCPU synchronization channel is used for other
 * purposes (memory imports, JIT allocations, fence signaling for
 * external observers). It is not used for intra-queue ordering
 * because the firmware side wait is more efficient.
 *
 * The scheduler does not perform any allocation. All buffers are
 * owned by the caller. The scheduler does not block. Completion is
 * observed through the sync objects that the caller passed in.
 */

#ifndef MANVIL_SCHED_H
#define MANVIL_SCHED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "csf/queue.h"
#include "sync/sync.h"
#include "cmd/cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Maximum number of waits or signals accepted in a single
 * submission.
 *
 * The limit is imposed by the ring space available for the framing
 * commands. Each wait or signal consumes one command entry, so a
 * submission with many dependencies uses a larger fraction of the
 * ring. The chosen limit is conservative and covers every realistic
 * Vulkan case.
 */
#define MANVIL_SCHED_MAX_WAITS   32u
#define MANVIL_SCHED_MAX_SIGNALS 32u

/*
 * Description of a single submission.
 *
 * All arrays are optional. A submission with no waits, no signals,
 * and no commands is a no-op and is rejected.
 *
 * The arrays are read but never modified. The caller retains
 * ownership of every pointer and every buffer.
 */
struct manvil_submit_desc {
    /*
     * Waits.
     *
     * Each wait names a sync object, a target value, and a
     * comparison operation. The operation is one of the
     * MANVIL_BASEP_CQS_WAIT_OPERATION_* constants.
     *
     * The waits are emitted in the order given. The firmware
     * processes them in that order and does not start the commands
     * until all of them are satisfied.
     */
    const manvil_sync *const *wait_syncs;
    const uint64_t            *wait_values;
    const uint8_t             *wait_ops;
    size_t                     num_waits;

    /*
     * Commands.
     *
     * A contiguous array of manvil_cmd entries. May be NULL when
     * num_cmds is zero.
     */
    const manvil_cmd *cmds;
    size_t            num_cmds;

    /*
     * Signals.
     *
     * Each signal names a sync object and the value to set it to.
     * The signals are emitted in the order given, after all the
     * commands have been executed.
     *
     * A signal value of zero clears the sync. Signal values are
     * usually monotonically increasing across submissions of the
     * same queue.
     */
    manvil_sync *const *signal_syncs;
    const uint64_t     *signal_values;
    size_t              num_signals;
};

/*
 * Submit a batch described by manvil_submit_desc to a single queue.
 *
 * The function:
 *   1. Validates the description.
 *   2. Computes the total number of command entries that will be
 *      written, including the framing waits and signals.
 *   3. Checks that the ring has enough free space. If not, fails
 *      with ENOSPC before writing anything.
 *   4. Writes the wait entries, the caller commands, and the signal
 *      entries in order.
 *   5. Publishes CS_INSERT and rings the doorbell once.
 *
 * Returns 0 on success, -1 on failure with errno set:
 *   EINVAL  a required argument is missing or invalid
 *   ENOSPC  the ring does not have enough free space
 *   EIO     the kick failed
 *
 * The call does not block.
 */
int manvil_sched_submit(manvil_queue *queue,
                         const struct manvil_submit_desc *desc);

/*
 * Description of a multi-queue submission.
 *
 * A multi-queue submission groups several single-queue submissions
 * so that the caller does not have to manage the ring space and the
 * kick ordering by hand. The submits are written to their respective
 * queues and then kicked in order.
 *
 * The queues array and the descs array must have the same length.
 */
struct manvil_submit_many_desc {
    manvil_queue *const *queues;
    const struct manvil_submit_desc *const *descs;
    size_t num_submits;
};

/*
 * Submit several batches across several queues.
 *
 * The function performs validation and ring space checks for every
 * submit first, then writes all of them, then kicks each queue once.
 * If any step fails, the already written submits remain in their
 * rings and will be executed by the firmware. The caller must treat
 * partial failure as a fatal condition for the affected queues.
 *
 * The current implementation is intended for use by the future
 * Vulkan ICD, where vkQueueSubmit can carry multiple submits that
 * must be observed atomically from the application's perspective.
 * Manvil exposes it now so that the ring space accounting can be
 * shared across submits when the ICD is added. Nothing in the
 * current code calls it.
 *
 * Returns 0 on success, -1 on failure with errno set to the error
 * reported by the first failing step.
 */
int manvil_sched_submit_many(const struct manvil_submit_many_desc *desc);

#ifdef __cplusplus
}
#endif

#endif /* MANVIL_SCHED_H */
