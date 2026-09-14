/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * Internal Kbase backend interface.
 *
 * This header declares the low level API that Manvil uses to talk to
 * the Kbase kernel driver. It sits directly above the raw ioctl
 * numbers and structures defined in manvil_abi.h. Higher layers
 * (device, CSF, KCPU) use this API instead of calling ioctl directly.
 *
 * The backend is deliberately small. It opens the device node, keeps
 * the file descriptor, remembers the UAPI version and the feature
 * bitmask, and provides a single dispatch function for ioctls that
 * handles tracing and error classification.
 */

#ifndef MANVIL_KBASE_H
#define MANVIL_KBASE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "abi/manvil_abi.h"
#include "abi/manvil_features.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Opaque handle to the Kbase backend.
 */
typedef struct manvil_kbase manvil_kbase;

/*
 * Error codes returned by the backend.
 *
 * These are Manvil specific and do not necessarily match errno values.
 * The backend translates errno into these codes when possible.
 */
enum manvil_kbase_error {
    MANVIL_KBASE_OK = 0,
    MANVIL_KBASE_ERR_OPEN,
    MANVIL_KBASE_ERR_IOCTL,
    MANVIL_KBASE_ERR_VERSION,
    MANVIL_KBASE_ERR_NOT_CSF,
    MANVIL_KBASE_ERR_NO_MEMORY,
    MANVIL_KBASE_ERR_INVALID_ARG,
    MANVIL_KBASE_ERR_IO,
    MANVIL_KBASE_ERR_UNKNOWN,
};

/*
 * Open a Kbase device.
 *
 * path        Path to the device node, typically "/dev/mali0".
 * err         Buffer for an error message on failure. May be NULL.
 * errsz       Size of the error buffer.
 *
 * Performs the initial handshake:
 *   1. open(path, O_RDWR)
 *   2. ioctl(VERSION_CHECK) with a zeroed struct to obtain the version.
 *   3. Reject kernels that do not report UAPI major version 1.
 *   4. Store the reported minor version and compute the feature bitmask.
 *
 * The handshake does not call SET_FLAGS or map the tracking page. Those
 * steps are done by the device layer once the caller decides to use the
 * device. This keeps the backend focused on communication only.
 *
 * Returns a handle on success, NULL on failure. On failure, if err is
 * not NULL, it contains a NUL terminated description.
 */
manvil_kbase *manvil_kbase_open(const char *path, char *err, size_t errsz);

/*
 * Close a Kbase device and release all resources.
 *
 * Passing NULL is allowed and is a no-op.
 */
void manvil_kbase_close(manvil_kbase *k);

/*
 * Accessors.
 */
int      manvil_kbase_fd(const manvil_kbase *k);
uint16_t manvil_kbase_uapi_major(const manvil_kbase *k);
uint16_t manvil_kbase_uapi_minor(const manvil_kbase *k);
uint64_t manvil_kbase_features(const manvil_kbase *k);

/*
 * Query whether a feature flag is set on this device.
 *
 * Returns true if the feature is available, false otherwise. Also
 * returns false if k is NULL.
 */
bool manvil_kbase_has_feature(const manvil_kbase *k, uint64_t feature);

/*
 * Raw ioctl dispatch.
 *
 * cmd      The ioctl number, as defined in manvil_abi.h.
 * arg      Pointer to the argument structure, or NULL if the ioctl
 *          does not take one.
 * name     A short label for tracing and error messages. May be NULL.
 *
 * The function calls ioctl(k->fd, cmd, arg) and returns:
 *   0        on success
 *  -1        on failure, with errno set to the value reported by the
 *            kernel
 *
 * The function does not modify errno on success. If name is not NULL
 * and Manvil tracing is enabled at runtime (through the environment
 * variable MANVIL_TRACE), the call and its result are logged.
 *
 * Higher level helpers below wrap this function for each specific
 * operation.
 */
int manvil_kbase_ioctl(manvil_kbase *k, unsigned long cmd, void *arg,
                       const char *name);

/*
 * UAPI version encoding helpers.
 *
 * MANVIL_KBASE_API_VERSION packs a major and minor into a single
 * uint32_t. These helpers convert back and forth and are useful for
 * logging and comparison.
 */
static inline uint32_t manvil_kbase_version_u32(const manvil_kbase *k)
{
    return MANVIL_KBASE_API_VERSION(manvil_kbase_uapi_major(k),
                                    manvil_kbase_uapi_minor(k));
}

/*
 * Helper macro for checking the UAPI version reported by the kernel.
 *
 * MANVIL_KBASE_VERSION_AT_LEAST(k, 14) is true if the minor version
 * reported by the kernel is 14 or greater. It is a convenience for
 * cases where a feature was introduced at a specific version and the
 * feature bitmask is not yet populated (for example, during the
 * handshake itself).
 */
#define MANVIL_KBASE_VERSION_AT_LEAST(k, minor) \
    (manvil_kbase_uapi_minor(k) >= (minor))

#ifdef __cplusplus
}
#endif

#endif /* MANVIL_KBASE_H */
