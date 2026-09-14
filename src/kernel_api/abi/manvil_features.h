/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * Feature flags for runtime dispatch.
 *
 * The UAPI evolves by adding fields to existing structures and by
 * introducing new ioctls. Manvil must decide at runtime which
 * operations are valid on the kernel it is talking to. Version
 * numbers alone are not sufficient because features sometimes appear
 * in a minor version and are corrected in a following one.
 *
 * The macros in this header name the features directly. Each has the
 * minimum minor version at which it is expected to be available. The
 * device layer probes the actual kernel at open time and sets a
 * bitmask of available features in the device structure.
 *
 * Code that depends on a feature queries the bitmask with
 * manvil_device_has_feature(). It does not compare version numbers
 * directly.
 */

#ifndef MANVIL_KBASE_ABI_FEATURES_H
#define MANVIL_KBASE_ABI_FEATURES_H

#include <stdint.h>

/*
 * Feature flag definitions.
 *
 * Each flag is a single bit. The comments give the minimum UAPI minor
 * version at which the feature is expected to be present.
 */
#define MANVIL_FEAT_REALTIME_PRIORITY       ((uint64_t)1u << 0)
    /* 1.1  BASE_QUEUE_GROUP_PRIORITY_REALTIME. */

#define MANVIL_FEAT_CSF_GPU_FEATURES        ((uint64_t)1u << 1)
    /* 1.2  CSF GPU features reported through GET_GPUPROPS. */

#define MANVIL_FEAT_GROUP_UID               ((uint64_t)1u << 2)
    /* 1.3  group_uid field in CS_QUEUE_GROUP_CREATE output. */

#define MANVIL_FEAT_INSTR_FEATURES          ((uint64_t)1u << 3)
    /* 1.4  instr_features field in CS_GET_GLB_IFACE output. */

#define MANVIL_FEAT_QUEUE_REGISTER_EX       ((uint64_t)1u << 4)
    /* 1.5  CS_QUEUE_REGISTER_EX ioctl (number 40). */

#define MANVIL_FEAT_HWC_PRFCNT              ((uint64_t)1u << 5)
    /* 1.6 and 1.10  Hardware performance counters interface. */

#define MANVIL_FEAT_RESERVED_IN_GC          ((uint64_t)1u << 6)
    /* 1.7  reserved field in CS_QUEUE_GROUP_CREATE input. */

#define MANVIL_FEAT_FIXED_VA                ((uint64_t)1u << 7)
    /* 1.9  FIXED_VA memory zone. */

#define MANVIL_FEAT_EXEC_VA_AUTO_INIT       ((uint64_t)1u << 8)
    /* 1.9  Automatic EXEC_VA zone initialization. */

#define MANVIL_FEAT_CSI_HANDLERS            ((uint64_t)1u << 9)
    /* 1.12 csi_handlers field in CS_QUEUE_GROUP_CREATE input. */

#define MANVIL_FEAT_READ_USER_PAGE          ((uint64_t)1u << 10)
    /* 1.13 KBASE_IOCTL_READ_USER_PAGE ioctl (number 60). */

#define MANVIL_FEAT_BUF_DESC_VA             ((uint64_t)1u << 11)
    /* 1.14 buf_desc_va field in CS_TILER_HEAP_INIT input. */

/*
 * Feature bits that are always expected to be present on a CSF
 * kernel. These are used by the device layer when populating the
 * feature bitmask so that callers do not need to special case them.
 */
#define MANVIL_FEAT_CSF_BASE \
    (MANVIL_FEAT_CSF_GPU_FEATURES | \
     MANVIL_FEAT_GROUP_UID | \
     MANVIL_FEAT_QUEUE_REGISTER_EX)

/*
 * Compute the feature bitmask for a given UAPI minor version.
 *
 * This is a convenience for cases where only the version is known.
 * In normal operation the device layer builds the bitmask from the
 * actual kernel reports, not from the version number.
 */
static inline uint64_t manvil_features_from_version(uint16_t minor)
{
    uint64_t mask = MANVIL_FEAT_CSF_BASE;

    if (minor >= 1)  mask |= MANVIL_FEAT_REALTIME_PRIORITY;
    if (minor >= 3)  mask |= MANVIL_FEAT_GROUP_UID;
    if (minor >= 4)  mask |= MANVIL_FEAT_INSTR_FEATURES;
    if (minor >= 5)  mask |= MANVIL_FEAT_QUEUE_REGISTER_EX;
    if (minor >= 6)  mask |= MANVIL_FEAT_HWC_PRFCNT;
    if (minor >= 7)  mask |= MANVIL_FEAT_RESERVED_IN_GC;
    if (minor >= 9)  mask |= MANVIL_FEAT_FIXED_VA;
    if (minor >= 9)  mask |= MANVIL_FEAT_EXEC_VA_AUTO_INIT;
    if (minor >= 12) mask |= MANVIL_FEAT_CSI_HANDLERS;
    if (minor >= 13) mask |= MANVIL_FEAT_READ_USER_PAGE;
    if (minor >= 14) mask |= MANVIL_FEAT_BUF_DESC_VA;

    return mask;
}

#endif /* MANVIL_KBASE_ABI_FEATURES_H */
