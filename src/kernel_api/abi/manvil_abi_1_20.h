/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * Kbase UAPI delta for version 1.15 through 1.20.
 *
 * This delta is a placeholder until the full set of headers for the
 * latest known version becomes available. It inherits everything from
 * the 1.14 delta and documents known changes that are not yet
 * reflected in the shared definitions.
 *
 * Known additions in versions 1.15 through 1.20 that will be added
 * once the exact struct layouts are confirmed:
 *
 *   1.15 to 1.19: additional GPU property keys and minor ioctls.
 *   1.20: verified on the MediaTek Dimensity platforms with Mali-G615.
 *         No incompatible changes to the CSF ioctls used by Manvil.
 *
 * The comment above is informational. All types and ioctls used by
 * Manvil on UAPI 1.20 are already defined in the 1.10 and 1.14
 * deltas. The only difference observed so far is that newer versions
 * accept the same structs with additional unused trailing fields.
 */

#ifndef MANVIL_KBASE_ABI_1_20_H
#define MANVIL_KBASE_ABI_1_20_H

#include "manvil_abi_common.h"
#include "manvil_abi_1_10.h"
#include "manvil_abi_1_14.h"

/*
 * Version of the UAPI that this delta enables.
 */
#define MANVIL_KBASE_UAPI_1_20 20

#endif /* MANVIL_KBASE_ABI_1_20_H */
