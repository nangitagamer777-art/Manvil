/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * CSF queue groups.
 *
 * A queue group is the unit of scheduling on the CSF. Groups are
 * created with a set of resource masks that determine what hardware
 * endpoints they are allowed to use, and a priority that determines
 * how the kernel scheduler arbitrates between them when there are
 * fewer slots than runnable groups.
 *
 * Each group binds one or more queues. The queues carry command
 * streams that the firmware reads and executes. The kernel
 * multiplexes groups onto the hardware CSG slots according to
 * priority and fairness rules.
 *
 * Manvil uses four logical group types:
 *
 *   vertex-tiler  graphics front end, uses tiler and fragment cores
 *   fragment      graphics back end, uses fragment cores
 *   compute       compute work, uses shader cores through the compute
 *                 endpoint
 *   async         transfers and background work, low priority
 *
 * Higher layers can create additional groups with custom descriptors
 * when the workload demands it. The four helpers are convenience
 * constructors for the common cases.
 */

#ifndef MANVIL_CSF_GROUP_H
#define MANVIL_CSF_GROUP_H

#include <stdbool.h>
#include <stdint.h>

#include "kernel_api/manvil_kbase.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Opaque handle to a queue group.
 */
typedef struct manvil_group manvil_group;

/*
 * Value meaning "all cores allowed" for a mask field.
 *
 * The kernel intersects this with the actual set of cores present, so
 * passing the all-ones value simply selects everything available.
 */
#define MANVIL_GROUP_ALL_CORES 0xFFFFFFFFFFFFFFFFull

/*
 * Maximum values accepted by the kernel for the endpoint counts.
 *
 * The kernel accepts up to 255 for compute and fragment and up to 15
 * for tiler. Values larger than these are clamped by the kernel, but
 * Manvil prefers to pass conservative values so that the requests are
 * predictable.
 */
#define MANVIL_GROUP_MAX_COMPUTE_ENDPOINTS  255u
#define MANVIL_GROUP_MAX_FRAGMENT_ENDPOINTS 255u
#define MANVIL_GROUP_MAX_TILER_ENDPOINTS    15u

/*
 * Group descriptor.
 *
 * All fields map directly to the input of the CS_QUEUE_GROUP_CREATE
 * ioctl. The csi_handlers field is only honored on UAPI 1.12 and
 * later. On earlier versions it is ignored and the corresponding
 * kernel behavior (kernel handles tiler OOM events) applies.
 *
 * Defaults:
 *   cs_min          minimum number of streams the group must receive
 *   priority        one of the MANVIL_BASE_QUEUE_GROUP_PRIORITY_*
 *   tiler_max       maximum number of tiler endpoints allowed
 *   fragment_max    maximum number of fragment endpoints allowed
 *   compute_max     maximum number of compute endpoints allowed
 *   csi_handlers    combination of MANVIL_BASE_CSF_*_EXCEPTION_FLAG
 */
struct manvil_group_desc {
    uint64_t tiler_mask;
    uint64_t fragment_mask;
    uint64_t compute_mask;
    uint8_t  cs_min;
    uint8_t  priority;
    uint8_t  tiler_max;
    uint8_t  fragment_max;
    uint8_t  compute_max;
    uint8_t  csi_handlers;
};

/*
 * Create a queue group with a custom descriptor.
 *
 * On success, returns a handle. On failure, returns NULL. On failure,
 * errno is left at the value reported by the kernel ioctl whenever
 * possible.
 */
manvil_group *manvil_group_create(manvil_kbase *kbase,
                                   const struct manvil_group_desc *desc);

/*
 * Convenience constructors.
 *
 * Each of these creates a group with a descriptor tuned for the
 * corresponding workload class. They are the intended entry points
 * for the four logical group types that Manvil uses.
 *
 * vertex_tiler  tiler and fragment endpoints, high priority,
 *               tiler OOM exception handled by the application when
 *               the kernel supports it
 * fragment      fragment endpoints only, high priority
 * compute       compute endpoint, medium priority
 * async         compute endpoint, low priority, smaller endpoint
 *               budget, used for background transfers
 */
manvil_group *manvil_group_create_vertex_tiler(manvil_kbase *kbase);
manvil_group *manvil_group_create_fragment(manvil_kbase *kbase);
manvil_group *manvil_group_create_compute(manvil_kbase *kbase);
manvil_group *manvil_group_create_async(manvil_kbase *kbase);

/*
 * Destroy a queue group.
 *
 * The group must have no bound queues when this is called. Higher
 * layers are expected to unbind their queues before destroying the
 * group. If any queues are still bound, the kernel will tear them
 * down as part of the terminate operation.
 *
 * Passing NULL is a no-op.
 *
 * The function marks the group as invalid before issuing the ioctl so
 * that no caller can use the handle after the call begins, even if
 * the ioctl itself fails. The userspace allocation is always freed.
 */
void manvil_group_destroy(manvil_group *group);

/*
 * Accessors. All return zero or false if the argument is NULL.
 */
uint8_t  manvil_group_handle(const manvil_group *group);
uint32_t manvil_group_uid(const manvil_group *group);
bool     manvil_group_is_valid(const manvil_group *group);

/*
 * Read back the descriptor that was used to create the group.
 *
 * Returns a pointer to an internal structure that remains valid for
 * the lifetime of the group. Callers that want a copy should memcpy
 * it.
 */
const struct manvil_group_desc *manvil_group_desc_of(const manvil_group *group);

/*
 * Initialize a descriptor with sensible defaults for the given
 * workload class. These are the same descriptors used by the
 * convenience constructors. Exposed so that callers can start from
 * the defaults and then tweak specific fields.
 *
 * The csi_handlers field is set to zero in the defaults. The
 * constructors set it according to feature availability. Callers
 * that use the defaults directly and want the OOM handler enabled
 * should set it manually if the feature is available.
 */
void manvil_group_desc_default_vertex_tiler(struct manvil_group_desc *desc);
void manvil_group_desc_default_fragment(struct manvil_group_desc *desc);
void manvil_group_desc_default_compute(struct manvil_group_desc *desc);
void manvil_group_desc_default_async(struct manvil_group_desc *desc);

#ifdef __cplusplus
}
#endif

#endif /* MANVIL_CSF_GROUP_H */
