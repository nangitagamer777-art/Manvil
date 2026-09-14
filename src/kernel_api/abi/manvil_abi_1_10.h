/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * Kbase UAPI baseline for version 1.0 through 1.11.
 *
 * This header contains the original CSF interface. Later versions add
 * to it in the manvil_abi_1_14.h and manvil_abi_1_20.h deltas.
 *
 * The CS_QUEUE_GROUP_CREATE ioctl on this version uses the layout
 * without the csi_handlers field. The csi_handlers field was added in
 * 1.12 and its position was previously padding.
 *
 * The CS_TILER_HEAP_INIT ioctl on this version uses the layout without
 * the buf_desc_va field. That field was added in 1.14.
 */

#ifndef MANVIL_KBASE_ABI_1_10_H
#define MANVIL_KBASE_ABI_1_10_H

#include "manvil_abi_common.h"

/*
 * Version of the UAPI that this baseline covers.
 */
#define MANVIL_KBASE_UAPI_1_10 10

/*
 * Queue group create input, baseline layout.
 *
 * The layout is:
 *   tiler_mask      u64
 *   fragment_mask   u64
 *   compute_mask    u64
 *   cs_min          u8
 *   priority        u8
 *   tiler_max       u8
 *   fragment_max    u8
 *   compute_max     u8
 *   padding         3 bytes
 *   reserved        u64
 *
 * The csi_handlers field is not present. It occupies the first byte of
 * the padding area starting with UAPI 1.12.
 */
union manvil_kbase_ioctl_cs_queue_group_create_v1 {
    struct {
        uint64_t tiler_mask;
        uint64_t fragment_mask;
        uint64_t compute_mask;
        uint8_t cs_min;
        uint8_t priority;
        uint8_t tiler_max;
        uint8_t fragment_max;
        uint8_t compute_max;
        uint8_t padding[3];
        uint64_t reserved;
    } in;
    struct {
        uint8_t group_handle;
        uint8_t padding[3];
        uint32_t group_uid;
    } out;
};

#define MANVIL_KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_V1 \
    MANVIL_IOWR(MANVIL_KBASE_IOCTL_TYPE, 58, \
                union manvil_kbase_ioctl_cs_queue_group_create_v1)

/*
 * Legacy queue group create ioctl from version 1.6.
 *
 * This form predates the addition of the group_uid field in 1.3 and
 * the reserved field in 1.7. It is preserved for compatibility with
 * very old clients and is not used by Manvil.
 */
union manvil_kbase_ioctl_cs_queue_group_create_1_6 {
    struct {
        uint64_t tiler_mask;
        uint64_t fragment_mask;
        uint64_t compute_mask;
        uint8_t cs_min;
        uint8_t priority;
        uint8_t tiler_max;
        uint8_t fragment_max;
        uint8_t compute_max;
        uint8_t padding[3];
    } in;
    struct {
        uint8_t group_handle;
        uint8_t padding[3];
        uint32_t group_uid;
    } out;
};

#define MANVIL_KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_1_6 \
    MANVIL_IOWR(MANVIL_KBASE_IOCTL_TYPE, 42, \
                union manvil_kbase_ioctl_cs_queue_group_create_1_6)

/*
 * Tiler heap init input, baseline layout.
 *
 * The layout is:
 *   chunk_size          u32
 *   initial_chunks      u32
 *   max_chunks          u32
 *   target_in_flight    u16
 *   group_id            u8
 *   padding             u8
 *
 * The buf_desc_va field is not present. It was added in 1.14.
 */
union manvil_kbase_ioctl_cs_tiler_heap_init_v1 {
    struct {
        uint32_t chunk_size;
        uint32_t initial_chunks;
        uint32_t max_chunks;
        uint16_t target_in_flight;
        uint8_t group_id;
        uint8_t padding;
    } in;
    struct {
        uint64_t gpu_heap_va;
        uint64_t first_chunk_va;
    } out;
};

#define MANVIL_KBASE_IOCTL_CS_TILER_HEAP_INIT_V1 \
    MANVIL_IOWR(MANVIL_KBASE_IOCTL_TYPE, 48, \
                union manvil_kbase_ioctl_cs_tiler_heap_init_v1)

/*
 * Queue register with extended format, added in 1.5.
 *
 * The base fields are identical to the plain register call. The
 * extended fields configure a CS trace buffer for the queue.
 */
struct manvil_kbase_ioctl_cs_queue_register_ex {
    uint64_t buffer_gpu_addr;
    uint32_t buffer_size;
    uint8_t priority;
    uint8_t padding[3];
    uint64_t ex_offset_var_addr;
    uint64_t ex_buffer_base;
    uint32_t ex_buffer_size;
    uint8_t ex_event_size;
    uint8_t ex_event_state;
    uint8_t ex_padding[2];
};

#define MANVIL_KBASE_IOCTL_CS_QUEUE_REGISTER_EX \
    MANVIL_IOW(MANVIL_KBASE_IOCTL_TYPE, 40, \
               struct manvil_kbase_ioctl_cs_queue_register_ex)

/*
 * Aliases for the default CSF ioctls.
 *
 * These names point to the baseline variants. Code that wants to work
 * with a specific version should use the explicit _V1 or _V2 suffix
 * names defined in the version deltas. The unsuffixed names select
 * the variant that matches the compile time target.
 */
#ifndef MANVIL_TARGET_UAPI
#define MANVIL_TARGET_UAPI 20
#endif

#if MANVIL_TARGET_UAPI >= 14
#define MANVIL_KBASE_IOCTL_CS_QUEUE_GROUP_CREATE \
    MANVIL_KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_V2
#define MANVIL_KBASE_IOCTL_CS_TILER_HEAP_INIT \
    MANVIL_KBASE_IOCTL_CS_TILER_HEAP_INIT_V2
#else
#define MANVIL_KBASE_IOCTL_CS_QUEUE_GROUP_CREATE \
    MANVIL_KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_V1
#define MANVIL_KBASE_IOCTL_CS_TILER_HEAP_INIT \
    MANVIL_KBASE_IOCTL_CS_TILER_HEAP_INIT_V1
#endif

#endif /* MANVIL_KBASE_ABI_1_10_H */
