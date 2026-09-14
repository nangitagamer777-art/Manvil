/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * CSF queue group implementation.
 */

#include "group.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "kernel_api/abi/manvil_abi.h"

/*
 * Internal group structure.
 */
struct manvil_group {
    manvil_kbase *kbase;

    /*
     * Values reported by the kernel in the create call output.
     */
    uint8_t  group_handle;
    uint32_t group_uid;

    /*
     * Copy of the descriptor used at creation. Kept for two reasons:
     * callers may want to inspect it, and future code paths (rebind,
     * reset) may need to recreate the group with the same parameters.
     */
    struct manvil_group_desc desc;

    /*
     * Validity flag. Set to false as soon as destroy begins, so that
     * concurrent access from other code paths sees an invalid group.
     */
    bool is_valid;
};

/*
 * Fill a descriptor with the supplied values.
 *
 * Helper used by the four default constructors. It takes the fields
 * that actually vary between them and leaves the rest at zero.
 */
static void desc_fill(struct manvil_group_desc *desc,
                      uint64_t tiler_mask,
                      uint64_t fragment_mask,
                      uint64_t compute_mask,
                      uint8_t  priority,
                      uint8_t  tiler_max,
                      uint8_t  fragment_max,
                      uint8_t  compute_max)
{
    memset(desc, 0, sizeof(*desc));
    desc->tiler_mask    = tiler_mask;
    desc->fragment_mask = fragment_mask;
    desc->compute_mask  = compute_mask;
    desc->cs_min        = 1;
    desc->priority      = priority;
    desc->tiler_max     = tiler_max;
    desc->fragment_max  = fragment_max;
    desc->compute_max   = compute_max;
    desc->csi_handlers  = 0;
}

void manvil_group_desc_default_vertex_tiler(struct manvil_group_desc *desc)
{
    if (desc == NULL) {
        return;
    }
    desc_fill(desc,
              MANVIL_GROUP_ALL_CORES,   /* tiler_mask */
              MANVIL_GROUP_ALL_CORES,   /* fragment_mask */
              0,                        /* compute_mask */
              MANVIL_BASE_QUEUE_GROUP_PRIORITY_HIGH,
              8,                        /* tiler_max */
              8,                        /* fragment_max */
              0);                       /* compute_max */
}

void manvil_group_desc_default_fragment(struct manvil_group_desc *desc)
{
    if (desc == NULL) {
        return;
    }
    desc_fill(desc,
              0,
              MANVIL_GROUP_ALL_CORES,
              0,
              MANVIL_BASE_QUEUE_GROUP_PRIORITY_HIGH,
              0,
              8,
              0);
}

void manvil_group_desc_default_compute(struct manvil_group_desc *desc)
{
    if (desc == NULL) {
        return;
    }
    desc_fill(desc,
              0,
              0,
              MANVIL_GROUP_ALL_CORES,
              MANVIL_BASE_QUEUE_GROUP_PRIORITY_MEDIUM,
              0,
              0,
              8);
}

void manvil_group_desc_default_async(struct manvil_group_desc *desc)
{
    if (desc == NULL) {
        return;
    }
    desc_fill(desc,
              0,
              0,
              MANVIL_GROUP_ALL_CORES,
              MANVIL_BASE_QUEUE_GROUP_PRIORITY_LOW,
              0,
              0,
              4);
}

/*
 * Validate a descriptor before issuing the create ioctl.
 *
 * The kernel rejects several combinations, but catching them here
 * gives better error messages and avoids a round trip. This function
 * does not enforce every possible rule; it catches the ones that are
 * easy to get wrong.
 */
static int desc_validate(const struct manvil_group_desc *desc)
{
    if (desc->cs_min == 0) {
        errno = EINVAL;
        return -1;
    }

    if (desc->priority >= MANVIL_BASE_QUEUE_GROUP_PRIORITY_COUNT) {
        errno = EINVAL;
        return -1;
    }

    /*
     * A group must be able to do something. If all three masks are
     * zero the group cannot be scheduled usefully.
     */
    if (desc->tiler_mask == 0 &&
        desc->fragment_mask == 0 &&
        desc->compute_mask == 0) {
        errno = EINVAL;
        return -1;
    }

    /*
     * A group must have at least one endpoint type enabled with a
     * nonzero maximum.
     */
    if (desc->tiler_max == 0 &&
        desc->fragment_max == 0 &&
        desc->compute_max == 0) {
        errno = EINVAL;
        return -1;
    }

    return 0;
}

/*
 * Issue the CS_QUEUE_GROUP_CREATE ioctl.
 *
 * Selects the v2 variant when the kernel supports the csi_handlers
 * feature, and the v1 variant otherwise. The v1 and v2 inputs share
 * the same first nine fields; the v2 variant adds csi_handlers where
 * v1 has padding.
 *
 * On success, stores the handle and uid in the group and returns 0.
 * On failure, returns -1 with errno set.
 */
static int group_do_create(struct manvil_group *group)
{
    bool has_csi_handlers =
        manvil_kbase_has_feature(group->kbase, MANVIL_FEAT_CSI_HANDLERS);

    if (has_csi_handlers) {
        union manvil_kbase_ioctl_cs_queue_group_create_v2 args;
        memset(&args, 0, sizeof(args));

        args.in.tiler_mask    = group->desc.tiler_mask;
        args.in.fragment_mask = group->desc.fragment_mask;
        args.in.compute_mask  = group->desc.compute_mask;
        args.in.cs_min        = group->desc.cs_min;
        args.in.priority      = group->desc.priority;
        args.in.tiler_max     = group->desc.tiler_max;
        args.in.fragment_max  = group->desc.fragment_max;
        args.in.compute_max   = group->desc.compute_max;
        args.in.csi_handlers  = group->desc.csi_handlers;
        args.in.reserved      = 0;

        int rc = manvil_kbase_ioctl(group->kbase,
                                    MANVIL_KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_V2,
                                    &args,
                                    "CS_QUEUE_GROUP_CREATE(v2)");
        if (rc < 0) {
            return -1;
        }

        group->group_handle = args.out.group_handle;
        group->group_uid    = args.out.group_uid;
    } else {
        union manvil_kbase_ioctl_cs_queue_group_create_v1 args;
        memset(&args, 0, sizeof(args));

        args.in.tiler_mask    = group->desc.tiler_mask;
        args.in.fragment_mask = group->desc.fragment_mask;
        args.in.compute_mask  = group->desc.compute_mask;
        args.in.cs_min        = group->desc.cs_min;
        args.in.priority      = group->desc.priority;
        args.in.tiler_max     = group->desc.tiler_max;
        args.in.fragment_max  = group->desc.fragment_max;
        args.in.compute_max   = group->desc.compute_max;
        args.in.reserved      = 0;

        int rc = manvil_kbase_ioctl(group->kbase,
                                    MANVIL_KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_V1,
                                    &args,
                                    "CS_QUEUE_GROUP_CREATE(v1)");
        if (rc < 0) {
            return -1;
        }

        group->group_handle = args.out.group_handle;
        group->group_uid    = args.out.group_uid;
    }

    return 0;
}

manvil_group *manvil_group_create(manvil_kbase *kbase,
                                   const struct manvil_group_desc *desc)
{
    if (kbase == NULL || desc == NULL) {
        errno = EINVAL;
        return NULL;
    }

    if (desc_validate(desc) < 0) {
        return NULL;
    }

    manvil_group *group = calloc(1, sizeof(*group));
    if (group == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    group->kbase      = kbase;
    group->desc       = *desc;
    group->is_valid   = false;

    if (group_do_create(group) < 0) {
        free(group);
        return NULL;
    }

    group->is_valid = true;
    return group;
}

manvil_group *manvil_group_create_vertex_tiler(manvil_kbase *kbase)
{
    struct manvil_group_desc desc;
    manvil_group_desc_default_vertex_tiler(&desc);

    /*
     * Enable the application side tiler OOM handler when the kernel
     * supports it. This lets the driver react to tiler heap
     * exhaustion instead of having the kernel terminate the group.
     */
    if (manvil_kbase_has_feature(kbase, MANVIL_FEAT_CSI_HANDLERS)) {
        desc.csi_handlers = MANVIL_BASE_CSF_TILER_OOM_EXCEPTION_FLAG;
    }

    return manvil_group_create(kbase, &desc);
}

manvil_group *manvil_group_create_fragment(manvil_kbase *kbase)
{
    struct manvil_group_desc desc;
    manvil_group_desc_default_fragment(&desc);
    return manvil_group_create(kbase, &desc);
}

manvil_group *manvil_group_create_compute(manvil_kbase *kbase)
{
    struct manvil_group_desc desc;
    manvil_group_desc_default_compute(&desc);
    return manvil_group_create(kbase, &desc);
}

manvil_group *manvil_group_create_async(manvil_kbase *kbase)
{
    struct manvil_group_desc desc;
    manvil_group_desc_default_async(&desc);
    return manvil_group_create(kbase, &desc);
}

void manvil_group_destroy(manvil_group *group)
{
    if (group == NULL) {
        return;
    }

    /*
     * Invalidate first. Once this is set, no caller can use the
     * handle, even if the ioctl below fails and the kernel keeps the
     * group alive for a moment longer. The kernel will clean up its
     * own state when it detects that the userspace side is gone, or
     * when the context is closed.
     */
    bool was_valid = group->is_valid;
    group->is_valid = false;

    if (was_valid) {
        struct manvil_kbase_ioctl_cs_queue_group_term term;
        memset(&term, 0, sizeof(term));
        term.group_handle = group->group_handle;

        int rc = manvil_kbase_ioctl(group->kbase,
                                    MANVIL_KBASE_IOCTL_CS_QUEUE_GROUP_TERMINATE,
                                    &term,
                                    "CS_QUEUE_GROUP_TERMINATE");
        if (rc < 0) {
            /*
             * The ioctl failed. Possible reasons:
             *  - the group was already terminated by the kernel due
             *    to an error
             *  - the context is going away and the ioctl path is
             *    closed
             *
             * In any case, Manvil releases the userspace structure.
             * The kernel will release its own state when it decides
             * to, or when the context is closed.
             */
        }
    }

    memset(group, 0, sizeof(*group));
    free(group);
}

uint8_t manvil_group_handle(const manvil_group *group)
{
    return group != NULL ? group->group_handle : 0;
}

uint32_t manvil_group_uid(const manvil_group *group)
{
    return group != NULL ? group->group_uid : 0;
}

bool manvil_group_is_valid(const manvil_group *group)
{
    return group != NULL && group->is_valid;
}

const struct manvil_group_desc *manvil_group_desc_of(const manvil_group *group)
{
    return group != NULL ? &group->desc : NULL;
}
