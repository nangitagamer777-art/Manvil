/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * Submission orchestration implementation.
 */

#include "sched.h"

#include <errno.h>
#include <string.h>

#include "kernel_api/abi/manvil_abi.h"

/*
 * Compute the number of command entries that a description will
 * expand to.
 *
 * Each wait or signal is one command entry. The caller supplied
 * commands contribute their own count. The result is used to
 * pre-check ring space before any write is performed.
 */
static size_t desc_total_entries(const struct manvil_submit_desc *desc)
{
    return desc->num_waits + desc->num_cmds + desc->num_signals;
}

/*
 * Validate a submission description.
 *
 * Returns 0 if the description is well formed, -1 with errno set
 * otherwise.
 */
static int desc_validate(const struct manvil_submit_desc *desc)
{
    if (desc == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (desc->num_waits > MANVIL_SCHED_MAX_WAITS) {
        errno = EINVAL;
        return -1;
    }
    if (desc->num_signals > MANVIL_SCHED_MAX_SIGNALS) {
        errno = EINVAL;
        return -1;
    }

    if (desc->num_waits > 0) {
        if (desc->wait_syncs == NULL ||
            desc->wait_values == NULL ||
            desc->wait_ops == NULL) {
            errno = EINVAL;
            return -1;
        }
    }

    if (desc->num_cmds > 0 && desc->cmds == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (desc->num_signals > 0) {
        if (desc->signal_syncs == NULL ||
            desc->signal_values == NULL) {
            errno = EINVAL;
            return -1;
        }
    }

    /*
     * A description that expands to nothing is a bug in the caller.
     */
    if (desc_total_entries(desc) == 0) {
        errno = EINVAL;
        return -1;
    }

    /*
     * Validate sync objects. A wait or signal entry that names an
     * invalid sync is rejected up front so that the ring is not
     * left in a half written state.
     */
    for (size_t i = 0; i < desc->num_waits; i++) {
        if (desc->wait_syncs[i] == NULL ||
            !manvil_sync_is_valid(desc->wait_syncs[i])) {
            errno = EINVAL;
            return -1;
        }
        uint8_t op = desc->wait_ops[i];
        if (op != MANVIL_BASEP_CQS_WAIT_OPERATION_LE &&
            op != MANVIL_BASEP_CQS_WAIT_OPERATION_GT) {
            errno = EINVAL;
            return -1;
        }
    }

    for (size_t i = 0; i < desc->num_signals; i++) {
        if (desc->signal_syncs[i] == NULL ||
            !manvil_sync_is_valid(desc->signal_syncs[i])) {
            errno = EINVAL;
            return -1;
        }
    }

    return 0;
}

/*
 * Build a SYNC_WAIT64 command from a wait entry.
 *
 * The condition is chosen from the operation requested by the
 * caller:
 *   GE -> the firmware treats this as "greater or equal", which
 *         corresponds to CS_COND_GEQUAL.
 *   LE -> the firmware treats this as "less or equal", which
 *         corresponds to CS_COND_LEQUAL.
 *
 * The other conditions (equal, not equal, always) are not exposed
 * by the current API because they are not needed for Vulkan
 * timeline semantics.
 */
static void build_wait_cmd(manvil_cmd cmd,
                            uint64_t sync_va,
                            uint64_t value,
                            uint8_t  op)
{
    uint8_t condition;

    if (op == MANVIL_BASEP_CQS_WAIT_OPERATION_LE) {
        condition = MANVIL_CS_COND_LEQUAL;
    } else {
        condition = MANVIL_CS_COND_GEQUAL;
    }

    manvil_cmd_sync_wait64(cmd,
                            value,
                            sync_va,
                            condition,
                            false);   /* error_reject */
}

/*
 * Build a SYNC_ADD64 command from a signal entry.
 *
 * The signal sets the sync value directly using SYNC_SET64-style
 * semantics (the value is written, not added). This matches what
 * Vulkan timeline semaphores expect.
 *
 * Scope is System so that the signal is observable outside the CSG,
 * including from the CPU and from other groups.
 *
 * No wait mask, signal slot 0, defer immediate, no error
 * propagation.
 */
static void build_signal_cmd(manvil_cmd cmd,
                              uint64_t sync_va,
                              uint64_t value)
{
    /*
     * The command uses SYNC_SET64 semantics. That opcode was not
     * wrapped as a builder function in cmd.h because the low level
     * layout is the same as SYNC_ADD64 with the operation chosen by
     * the opcode. Manvil's cmd module currently only exposes
     * manvil_cmd_sync_add64 and manvil_cmd_sync_set64 as separate
     * functions.
     */
    manvil_cmd_sync_set64(cmd,
                           value,
                           sync_va,
                           MANVIL_CS_SYNC_SCOPE_SYSTEM,
                           0,              /* wait_mask */
                           0,              /* signal_slot */
                           MANVIL_CS_DEFER_IMMEDIATE,
                           false);         /* error_propagate */
}

/*
 * Write the framing commands and caller commands for a submit
 * description into the target queue.
 *
 * On failure the ring may contain a partial batch. The caller
 * treats that as fatal for the affected queue; the firmware will
 * execute whatever was written when the doorbell rings, and the
 * application state is considered inconsistent.
 *
 * Pre-validation in manvil_sched_submit ensures that partial writes
 * do not occur in practice. This function is only called after the
 * space check has passed.
 */
static int write_desc(manvil_queue *queue,
                       const struct manvil_submit_desc *desc)
{
    manvil_cmd scratch;

    /*
     * Waits first.
     */
    for (size_t i = 0; i < desc->num_waits; i++) {
        build_wait_cmd(scratch,
                        manvil_sync_gpu_va(desc->wait_syncs[i]),
                        desc->wait_values[i],
                        desc->wait_ops[i]);
        if (manvil_queue_write(queue, scratch, MANVIL_CMD_SIZE_BYTES) < 0) {
            return -1;
        }
    }

    /*
     * Caller commands.
     */
    if (desc->num_cmds > 0) {
        size_t bytes = desc->num_cmds * MANVIL_CMD_SIZE_BYTES;
        if (manvil_queue_write(queue, desc->cmds, bytes) < 0) {
            return -1;
        }
    }

    /*
     * Signals last.
     */
    for (size_t i = 0; i < desc->num_signals; i++) {
        build_signal_cmd(scratch,
                          manvil_sync_gpu_va(desc->signal_syncs[i]),
                          desc->signal_values[i]);
        if (manvil_queue_write(queue, scratch, MANVIL_CMD_SIZE_BYTES) < 0) {
            return -1;
        }
    }

    return 0;
}

int manvil_sched_submit(manvil_queue *queue,
                         const struct manvil_submit_desc *desc)
{
    if (queue == NULL || !manvil_queue_is_valid(queue)) {
        errno = EINVAL;
        return -1;
    }

    if (desc_validate(desc) < 0) {
        return -1;
    }

    /*
     * Pre-check ring space so that the write below cannot fail
     * halfway through.
     */
    size_t entries = desc_total_entries(desc);
    size_t bytes   = entries * MANVIL_CMD_SIZE_BYTES;

    if (bytes > manvil_queue_space_free(queue)) {
        errno = ENOSPC;
        return -1;
    }

    if (write_desc(queue, desc) < 0) {
        return -1;
    }

    if (manvil_queue_kick(queue) < 0) {
        return -1;
    }

    return 0;
}

/*
 * Multi-queue submission.
 */

static int many_desc_validate(const struct manvil_submit_many_desc *desc)
{
    if (desc == NULL || desc->num_submits == 0) {
        errno = EINVAL;
        return -1;
    }

    if (desc->queues == NULL || desc->descs == NULL) {
        errno = EINVAL;
        return -1;
    }

    for (size_t i = 0; i < desc->num_submits; i++) {
        if (desc->queues[i] == NULL ||
            !manvil_queue_is_valid(desc->queues[i])) {
            errno = EINVAL;
            return -1;
        }
        if (desc->descs[i] == NULL) {
            errno = EINVAL;
            return -1;
        }
        if (desc_validate(desc->descs[i]) < 0) {
            return -1;
        }
    }

    return 0;
}

int manvil_sched_submit_many(const struct manvil_submit_many_desc *desc)
{
    if (many_desc_validate(desc) < 0) {
        return -1;
    }

    /*
     * Pre-check ring space for every submit before writing any of
     * them. This guarantees that either all the batches fit or none
     * of them is attempted.
     */
    for (size_t i = 0; i < desc->num_submits; i++) {
        size_t entries = desc_total_entries(desc->descs[i]);
        size_t bytes   = entries * MANVIL_CMD_SIZE_BYTES;

        if (bytes > manvil_queue_space_free(desc->queues[i])) {
            errno = ENOSPC;
            return -1;
        }
    }

    /*
     * Write all batches. If a write fails here it is an internal
     * error because the space check above already passed. Return
     * without kicking, so the ring content is not yet visible to
     * the firmware. The application must treat the affected queues
     * as broken and recreate them.
     */
    for (size_t i = 0; i < desc->num_submits; i++) {
        if (write_desc(desc->queues[i], desc->descs[i]) < 0) {
            return -1;
        }
    }

    /*
     * Kick each queue once. Ordering across queues is not
     * guaranteed by this call; the firmware resolves cross-queue
     * dependencies through the sync objects in the submissions.
     */
    for (size_t i = 0; i < desc->num_submits; i++) {
        if (manvil_queue_kick(desc->queues[i]) < 0) {
            return -1;
        }
    }

    return 0;
}
