/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * CSF notifications and errors implementation.
 */

/*
 * Enable POSIX 2008 features for poll and related definitions.
 */
#define _POSIX_C_SOURCE 200809L

#include "event.h"

#include <errno.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

/*
 * Poll the device descriptor for readability.
 *
 * The poll timeout is expressed as milliseconds at the syscall
 * level, but the API here takes nanoseconds for consistency with
 * the rest of Manvil. The conversion rounds up to the nearest
 * millisecond, except for MANVIL_CSF_POLL_NOWAIT which stays at
 * zero.
 */
static int do_poll(int fd, int64_t timeout_ns, short *revents_out)
{
    struct pollfd pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.fd     = fd;
    pfd.events = POLLIN;

    int timeout_ms;
    if (timeout_ns == MANVIL_CSF_POLL_FOREVER) {
        timeout_ms = -1;
    } else if (timeout_ns == MANVIL_CSF_POLL_NOWAIT) {
        timeout_ms = 0;
    } else if (timeout_ns <= 0) {
        errno = EINVAL;
        return -1;
    } else {
        /*
         * Convert nanoseconds to milliseconds, rounding up so that
         * a small positive timeout does not become zero.
         */
        int64_t ms = (timeout_ns + 999999) / 1000000;
        if (ms > 0x7fffffff) {
            ms = 0x7fffffff;
        }
        timeout_ms = (int)ms;
    }

    int rc;
    do {
        rc = poll(&pfd, 1, timeout_ms);
    } while (rc < 0 && errno == EINTR);

    if (rc < 0) {
        return -1;
    }

    *revents_out = pfd.revents;
    return rc;
}

/*
 * Read one raw notification record from the device.
 *
 * The kernel always returns exactly sizeof(base_csf_notification)
 * bytes on success. A short read or a zero read is treated as an
 * error, because the protocol does not define a partial record.
 */
static int do_read_raw(int fd,
                        struct manvil_base_csf_notification *out)
{
    ssize_t rd;
    do {
        rd = read(fd, out, MANVIL_CSF_NOTIFICATION_SIZE);
    } while (rd < 0 && errno == EINTR);

    if (rd < 0) {
        return -1;
    }

    if (rd != (ssize_t)MANVIL_CSF_NOTIFICATION_SIZE) {
        errno = EIO;
        return -1;
    }

    return 0;
}

void manvil_csf_parse_event(
    const struct manvil_base_csf_notification *raw,
    struct manvil_csf_event_info *out)
{
    memset(out, 0, sizeof(*out));

    if (raw == NULL) {
        out->kind = MANVIL_CSF_EVENT_NONE;
        return;
    }

    out->raw_type = raw->type;

    switch (raw->type) {
    case MANVIL_BASE_CSF_NOTIFICATION_EVENT:
        out->kind = MANVIL_CSF_EVENT_NONE;
        return;

    case MANVIL_BASE_CSF_NOTIFICATION_CPU_QUEUE_DUMP:
        out->kind = MANVIL_CSF_EVENT_CPU_QUEUE_DUMP;
        return;

    case MANVIL_BASE_CSF_NOTIFICATION_GPU_QUEUE_GROUP_ERROR: {
        const struct manvil_base_gpu_queue_group_error *err =
            &raw->payload.csg_error.error;

        out->group_handle = raw->payload.csg_error.handle;

        switch (err->error_type) {
        case MANVIL_BASE_GPU_QUEUE_GROUP_ERROR_FATAL: {
            const struct manvil_base_gpu_queue_group_error_fatal_payload *p =
                &err->payload.fatal_group;
            out->kind     = MANVIL_CSF_EVENT_GROUP_FATAL;
            out->status   = p->status;
            out->sideband = p->sideband;
            return;
        }

        case MANVIL_BASE_GPU_QUEUE_GROUP_QUEUE_ERROR_FATAL: {
            const struct manvil_base_gpu_queue_error_fatal_payload *p =
                &err->payload.fatal_queue;
            out->kind      = MANVIL_CSF_EVENT_QUEUE_FATAL;
            out->csi_index = p->csi_index;
            out->status    = p->status;
            out->sideband  = p->sideband;
            return;
        }

        case MANVIL_BASE_GPU_QUEUE_GROUP_ERROR_TIMEOUT:
            out->kind = MANVIL_CSF_EVENT_TIMEOUT;
            return;

        case MANVIL_BASE_GPU_QUEUE_GROUP_ERROR_TILER_HEAP_OOM:
            out->kind = MANVIL_CSF_EVENT_TILER_HEAP_OOM;
            return;

        default:
            out->kind = MANVIL_CSF_EVENT_UNKNOWN;
            return;
        }
    }

    default:
        out->kind = MANVIL_CSF_EVENT_UNKNOWN;
        return;
    }
}

int manvil_csf_poll(manvil_kbase *kbase, int64_t timeout_ns)
{
    if (kbase == NULL) {
        errno = EINVAL;
        return -1;
    }

    short revents = 0;
    int rc = do_poll(manvil_kbase_fd(kbase), timeout_ns, &revents);
    if (rc < 0) {
        return -1;
    }

    if (rc == 0) {
        return 0;
    }

    if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
        /*
         * The device descriptor reported an error condition. This
         * usually means the device has been closed or the kernel
         * has torn down the context. Report it as a fatal error.
         */
        errno = EIO;
        return -1;
    }

    if (revents & POLLIN) {
        return 1;
    }

    /*
     * poll returned with a positive count but neither POLLIN nor an
     * error flag. This should not happen. Treat as no data.
     */
    return 0;
}

int manvil_csf_read(manvil_kbase *kbase,
                     struct manvil_csf_event_info *out)
{
    if (kbase == NULL || out == NULL) {
        errno = EINVAL;
        return -1;
    }

    struct manvil_base_csf_notification raw;
    if (do_read_raw(manvil_kbase_fd(kbase), &raw) < 0) {
        return -1;
    }

    manvil_csf_parse_event(&raw, out);

    /*
     * The kernel returns a synthetic EVENT when nothing else is
     * pending. Report that as "nothing to read" rather than as a
     * real notification.
     */
    if (out->kind == MANVIL_CSF_EVENT_NONE) {
        return 0;
    }

    return 1;
}

int manvil_csf_wait_notification(manvil_kbase *kbase,
                                  struct manvil_csf_event_info *out,
                                  int64_t timeout_ns)
{
    if (kbase == NULL || out == NULL) {
        errno = EINVAL;
        return -1;
    }

    int poll_rc = manvil_csf_poll(kbase, timeout_ns);
    if (poll_rc <= 0) {
        /*
         * poll_rc == 0: timeout
         * poll_rc == -1: error, errno already set
         */
        return poll_rc;
    }

    return manvil_csf_read(kbase, out);
}

const char *manvil_csf_event_kind_str(enum manvil_csf_event_kind kind)
{
    switch (kind) {
    case MANVIL_CSF_EVENT_NONE:            return "none";
    case MANVIL_CSF_EVENT_KERNEL:          return "kernel_event";
    case MANVIL_CSF_EVENT_CPU_QUEUE_DUMP:  return "cpu_queue_dump";
    case MANVIL_CSF_EVENT_GROUP_FATAL:     return "group_fatal";
    case MANVIL_CSF_EVENT_QUEUE_FATAL:     return "queue_fatal";
    case MANVIL_CSF_EVENT_TIMEOUT:         return "timeout";
    case MANVIL_CSF_EVENT_TILER_HEAP_OOM:  return "tiler_heap_oom";
    case MANVIL_CSF_EVENT_UNKNOWN:         return "unknown";
    default:                               return "invalid";
    }
}
