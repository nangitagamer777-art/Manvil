/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * Internal Kbase backend implementation.
 */

#include "manvil_kbase.h"

#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/*
 * Internal state of the backend.
 */
struct manvil_kbase {
    int      fd;
    uint16_t uapi_major;
    uint16_t uapi_minor;
    uint64_t features;

    /*
     * Tracing flag. When true, ioctls are logged to stderr.
     * Initialized from the MANVIL_TRACE environment variable at open.
     */
    bool     trace;
};

/*
 * Compose an error message in the caller provided buffer.
 */
static void set_error(char *err, size_t errsz, const char *fmt, ...)
{
    if (err == NULL || errsz == 0) {
        return;
    }

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, errsz, fmt, ap);
    va_end(ap);
}

/*
 * Map an errno value to a manvil_kbase_error code.
 *
 * Used internally by the higher level helpers when they need to
 * distinguish classes of failures. The raw dispatch function returns
 * the errno value directly through errno.
 */
static enum manvil_kbase_error errno_to_error(int e)
{
    switch (e) {
    case 0:      return MANVIL_KBASE_OK;
    case ENOMEM: return MANVIL_KBASE_ERR_NO_MEMORY;
    case EINVAL: return MANVIL_KBASE_ERR_INVALID_ARG;
    case EIO:    return MANVIL_KBASE_ERR_IO;
    case EPERM:  return MANVIL_KBASE_ERR_NOT_CSF;
    default:     return MANVIL_KBASE_ERR_UNKNOWN;
    }
}

/*
 * Silent variant of errno_to_error, unused for now but kept for
 * symmetry with the error enum. Some callers may want a code that
 * does not depend on a live errno value.
 */
__attribute__((unused))
static enum manvil_kbase_error classify_error(int e, int rc)
{
    if (rc >= 0) {
        return MANVIL_KBASE_OK;
    }
    return errno_to_error(e);
}

/*
 * Read the MANVIL_TRACE environment variable.
 *
 * Returns true if tracing is enabled. The value must be "1" or a
 * non-empty string other than "0" or "false".
 */
static bool trace_from_env(void)
{
    const char *v = getenv("MANVIL_TRACE");
    if (v == NULL || v[0] == '\0') {
        return false;
    }
    if (strcmp(v, "0") == 0 || strcmp(v, "false") == 0) {
        return false;
    }
    return true;
}

/*
 * Compute the feature bitmask from the UAPI minor version.
 *
 * This is used at open time because the backend does not yet know
 * anything else about the kernel. Later, when the device layer has
 * read the GPU properties and the CSF global interface, it may refine
 * the bitmask.
 */
static uint64_t features_from_minor(uint16_t minor)
{
    return manvil_features_from_version(minor);
}

manvil_kbase *manvil_kbase_open(const char *path, char *err, size_t errsz)
{
    if (path == NULL) {
        set_error(err, errsz, "invalid path");
        return NULL;
    }

    int fd = open(path, O_RDWR);
    if (fd < 0) {
        set_error(err, errsz, "cannot open %s: %s", path, strerror(errno));
        return NULL;
    }

    /*
     * Version check with a zeroed struct. The kernel fills in the
     * version it implements. The CSF variant uses ioctl number 52.
     * If the kernel rejects this ioctl with EPERM, the device is a
     * JM kernel that Manvil does not support.
     */
    struct manvil_kbase_ioctl_version_check vc;
    memset(&vc, 0, sizeof(vc));

    if (ioctl(fd, MANVIL_KBASE_IOCTL_VERSION_CHECK, &vc) < 0) {
        int e = errno;
        if (e == EPERM) {
            set_error(err, errsz,
                      "kernel at %s is not a CSF device (EPERM on "
                      "VERSION_CHECK); JM is not supported", path);
        } else {
            set_error(err, errsz,
                      "VERSION_CHECK failed at %s: %s",
                      path, strerror(e));
        }
        close(fd);
        return NULL;
    }

    /*
     * The CSF UAPI major version is 1. Anything else means the
     * kernel speaks a language Manvil does not understand.
     */
    if (vc.major != 1) {
        set_error(err, errsz,
                  "unsupported UAPI major version %u (need 1)",
                  (unsigned)vc.major);
        close(fd);
        return NULL;
    }

    manvil_kbase *k = calloc(1, sizeof(*k));
    if (k == NULL) {
        set_error(err, errsz, "out of memory");
        close(fd);
        return NULL;
    }

    k->fd         = fd;
    k->uapi_major = vc.major;
    k->uapi_minor = vc.minor;
    k->features   = features_from_minor(vc.minor);
    k->trace      = trace_from_env();

    if (k->trace) {
        fprintf(stderr,
                "[manvil] kbase open: %s fd=%d uapi=%u.%u features=0x%llx\n",
                path, fd,
                (unsigned)k->uapi_major, (unsigned)k->uapi_minor,
                (unsigned long long)k->features);
    }

    return k;
}

void manvil_kbase_close(manvil_kbase *k)
{
    if (k == NULL) {
        return;
    }

    if (k->trace) {
        fprintf(stderr, "[manvil] kbase close: fd=%d\n", k->fd);
    }

    if (k->fd >= 0) {
        close(k->fd);
    }

    memset(k, 0, sizeof(*k));
    free(k);
}

int manvil_kbase_fd(const manvil_kbase *k)
{
    return k != NULL ? k->fd : -1;
}

uint16_t manvil_kbase_uapi_major(const manvil_kbase *k)
{
    return k != NULL ? k->uapi_major : 0;
}

uint16_t manvil_kbase_uapi_minor(const manvil_kbase *k)
{
    return k != NULL ? k->uapi_minor : 0;
}

uint64_t manvil_kbase_features(const manvil_kbase *k)
{
    return k != NULL ? k->features : 0;
}

bool manvil_kbase_has_feature(const manvil_kbase *k, uint64_t feature)
{
    if (k == NULL) {
        return false;
    }
    return (k->features & feature) == feature;
}

int manvil_kbase_ioctl(manvil_kbase *k, unsigned long cmd, void *arg,
                       const char *name)
{
    if (k == NULL) {
        errno = EINVAL;
        return -1;
    }

    int rc = ioctl(k->fd, cmd, arg);

    if (k->trace) {
        int e = errno;
        fprintf(stderr,
                "[manvil] ioctl %s: cmd=0x%lx arg=%p rc=%d errno=%d (%s)\n",
                name != NULL ? name : "(unnamed)",
                cmd, arg, rc, rc < 0 ? e : 0,
                rc < 0 ? strerror(e) : "ok");
    }

    return rc;
}
