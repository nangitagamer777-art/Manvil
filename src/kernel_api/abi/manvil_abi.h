/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * Unified entry point for the Kbase UAPI definitions.
 *
 * This header includes the common definitions and the delta for the
 * target UAPI version. The target version is selected at compile time
 * with the MANVIL_TARGET_UAPI macro. The default is the newest
 * supported version (currently 1.20).
 *
 * Runtime version handling is a separate concern. The kernel reports
 * the version it implements at device open. Manvil stores that value
 * and uses feature flags (declared in manvil_features.h) to decide
 * whether specific features are available.
 *
 * A program compiled with MANVIL_TARGET_UAPI=20 can still run on a
 * kernel that reports an earlier minor version. The kernel will
 * reject operations that use newer features, and Manvil code must
 * check the feature flags before calling them.
 */

#ifndef MANVIL_KBASE_ABI_H
#define MANVIL_KBASE_ABI_H

/*
 * Default target version if not set by the build system.
 */
#ifndef MANVIL_TARGET_UAPI
#define MANVIL_TARGET_UAPI 20
#endif

/*
 * The common definitions are always included.
 */
#include "manvil_abi_common.h"

/*
 * Select the delta for the target version.
 *
 * Each delta includes the previous one, so including the delta for
 * the target version is sufficient. The chain is:
 *
 *   1_20 -> 1_14 -> 1_10 -> common
 */
#if MANVIL_TARGET_UAPI >= 20
#include "manvil_abi_1_20.h"
#elif MANVIL_TARGET_UAPI >= 14
#include "manvil_abi_1_14.h"
#elif MANVIL_TARGET_UAPI >= 10
#include "manvil_abi_1_10.h"
#else
#error "MANVIL_TARGET_UAPI must be at least 10 (the first CSF version)."
#endif

/*
 * Sanity check: the selected headers must have defined the flags and
 * types that Manvil expects to be available.
 */
#ifndef MANVIL_BASE_MEM_PROT_CPU_RD
#error "manvil_abi_common.h was not included correctly."
#endif

#ifndef MANVIL_KBASE_IOCTL_VERSION_CHECK
#error "manvil_abi_common.h is missing the version check ioctl."
#endif

#ifndef MANVIL_KBASE_IOCTL_CS_QUEUE_GROUP_CREATE
#error "No CS_QUEUE_GROUP_CREATE variant was selected."
#endif

#ifndef MANVIL_KBASE_IOCTL_CS_TILER_HEAP_INIT
#error "No CS_TILER_HEAP_INIT variant was selected."
#endif

/*
 * UAPI version this compilation targets.
 *
 * This is a compile time constant. The runtime version reported by
 * the kernel may differ and is stored in the device structure.
 */
#define MANVIL_COMPILED_UAPI_VERSION MANVIL_TARGET_UAPI

#endif /* MANVIL_KBASE_ABI_H */
