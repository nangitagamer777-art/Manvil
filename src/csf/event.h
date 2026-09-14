/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * CSF notifications and errors.
 *
 * The kernel delivers notifications to userspace through the device
 * file descriptor. Two mechanisms cooperate:
 *
 *   poll()   waits until at least one notification is pending
 *   read()   returns one notification as a 64-byte record
 *
 * The record layout is defined by struct base_csf_notification in
 * the Kbase UAPI. It is exactly 64 bytes: a one byte type tag, seven
 * bytes of padding, and a 56-byte payload that depends on the type.
 *
 * The kernel read path never blocks. It always returns a 64-byte
 * record. When nothing is pending, the kernel returns a synthetic
 * BASE_CSF_NOTIFICATION_EVENT with a zero payload. This makes the
 * poll() call the only reliable way to know whether a real
 * notification is waiting.
 *
 * Manvil enforces this contract:
 *
 *   manvil_csf_wait_notification polls first, then reads only if
 *   the poll indicated readiness. Callers that prefer to split the
 *   two steps can use manvil_csf_poll and manvil_csf_read directly.
 *
 * Notification types:
 *
 *   BASE_CSF_NOTIFICATION_EVENT
 *     Synthetic record used by the kernel when nothing else is
 *     pending. Never delivered after a successful poll that
 *     reported readability, but the read path can still return it
 *     in unusual races. Manvil treats it as "nothing to report".
 *
 *   BASE_CSF_NOTIFICATION_GPU_QUEUE_GROUP_ERROR
 *     A queue group or a queue inside a group reported a fatal
 *     error, a progress timeout, or a tiler heap out of memory
 *     condition. The payload is a base_gpu_queue_group_error.
 *
 *   BASE_CSF_NOTIFICATION_CPU_QUEUE_DUMP
 *     The kernel produced a dump of the KCPU queue for diagnostic
 *     purposes. The payload is a base_csf_notification with a
 *     special layout.
 *
 * The parser in this module converts the raw record into a small
 * tagged structure with only the fields that higher layers need.
 */

#ifndef MANVIL_CSF_EVENT_H
#define MANVIL_CSF_EVENT_H

#include <stdbool.h>
#include <stdint.h>

#include "kernel_api/manvil_kbase.h"
#include "kernel_api/abi/manvil_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Size of one notification record in bytes. Matches the STATIC_ASSERT
 * in the reference implementation.
 */
#define MANVIL_CSF_NOTIFICATION_SIZE 64u

/*
 * Timeout constants for the poll and wait functions.
 *
 * A negative value means "block indefinitely". Zero means "do not
 * block at all". A positive value is a timeout in nanoseconds.
 */
#define MANVIL_CSF_POLL_FOREVER (-1)
#define MANVIL_CSF_POLL_NOWAIT   0

/*
 * High level classification of a notification.
 *
 * Higher layers usually only care about the kind. The additional
 * fields in manvil_csf_event_info provide context for logging and
 * for choosing a recovery strategy.
 */
enum manvil_csf_event_kind {
    /*
     * The record was a synthetic event or an empty payload. Nothing
     * to report.
     */
    MANVIL_CSF_EVENT_NONE = 0,

    /*
     * Kernel event, no user visible meaning beyond the fact that
     * something happened on the kernel side.
     */
    MANVIL_CSF_EVENT_KERNEL,

    /*
     * A CPU queue dump was produced.
     */
    MANVIL_CSF_EVENT_CPU_QUEUE_DUMP,

    /*
     * A queue group reported a fatal error.
     */
    MANVIL_CSF_EVENT_GROUP_FATAL,

    /*
     * A queue inside a group reported a fatal error.
     */
    MANVIL_CSF_EVENT_QUEUE_FATAL,

    /*
     * A queue group did not make progress within the firmware side
     * timeout.
     */
    MANVIL_CSF_EVENT_TIMEOUT,

    /*
     * A queue group exhausted the tiler heap.
     */
    MANVIL_CSF_EVENT_TILER_HEAP_OOM,

    /*
     * The record type was not recognized. Higher layers should log
     * the raw record and continue.
     */
    MANVIL_CSF_EVENT_UNKNOWN,
};

/*
 * Parsed notification.
 *
 * Only the fields that are meaningful for the given kind are filled.
 * Others are zero.
 */
struct manvil_csf_event_info {
    enum manvil_csf_event_kind kind;

    /*
     * Handle of the affected queue group, when applicable.
     */
    uint8_t group_handle;

    /*
     * Index of the affected CSI within the group, when applicable.
     * Only meaningful for MANVIL_CSF_EVENT_QUEUE_FATAL.
     */
    uint8_t csi_index;

    /*
     * Status word as reported by the firmware. The low byte is the
     * exception type. The remaining bits carry additional data
     * whose meaning depends on the exception.
     */
    uint32_t status;

    /*
     * Additional sideband information. Opaque to Manvil, useful in
     * logs.
     */
    uint64_t sideband;

    /*
     * For MANVIL_CSF_EVENT_UNKNOWN, the raw type byte.
     */
    uint8_t raw_type;
};

/*
 * Wait for a notification.
 *
 * timeout_ns   MANVIL_CSF_POLL_FOREVER for an unbounded wait,
 *              MANVIL_CSF_POLL_NOWAIT for an immediate check, or a
 *              positive nanosecond value.
 *
 * out          Filled with the parsed notification when a real one
 *              is received. Not modified on timeout.
 *
 * Returns:
 *    1   a real notification was received and parsed
 *    0   the timeout expired, or POLL_NOWAIT was used and nothing
 *        was pending
 *   -1   an error occurred. errno is set to the value reported by
 *        poll or read.
 *
 * The implementation polls first. When the poll reports
 * readability, it reads one record. A record that turns out to be
 * the synthetic event is treated as "nothing to report" and the
 * function returns 0. This is rare but possible during a race with
 * a notification being consumed by another thread.
 */
int manvil_csf_wait_notification(manvil_kbase *kbase,
                                  struct manvil_csf_event_info *out,
                                  int64_t timeout_ns);

/*
 * Poll the device for readability without reading.
 *
 * Returns 1 if a notification is pending, 0 on timeout, -1 on
 * error. Useful for event loops that want to multiplex the device
 * descriptor with other descriptors.
 */
int manvil_csf_poll(manvil_kbase *kbase, int64_t timeout_ns);

/*
 * Read one notification without waiting.
 *
 * The caller must have previously observed readability through
 * manvil_csf_poll. Calling this function without a preceding poll
 * may return a synthetic event that carries no information.
 *
 * out          Filled with the parsed notification.
 *
 * Returns:
 *    1   a real notification was received and parsed
 *    0   the record was a synthetic event
 *   -1   an error occurred. errno is set.
 */
int manvil_csf_read(manvil_kbase *kbase,
                     struct manvil_csf_event_info *out);

/*
 * Parse a raw notification record.
 *
 * This function is exposed for callers that obtained a raw record
 * through other means, for example a debug capture. It does not
 * perform any IO.
 */
void manvil_csf_parse_event(
    const struct manvil_base_csf_notification *raw,
    struct manvil_csf_event_info *out);

/*
 * Return a short string describing the kind, for logging.
 */
const char *manvil_csf_event_kind_str(enum manvil_csf_event_kind kind);

#ifdef __cplusplus
}
#endif

#endif /* MANVIL_CSF_EVENT_H */
