/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * Kbase UAPI delta for version 1.12 through 1.14.
 *
 * This header adds the definitions that were introduced between UAPI
 * 1.12 and 1.14. It is included on top of manvil_abi_common.h and
 * manvil_abi_1_10.h.
 *
 * New in 1.12:
 *   - csi_handlers field in the queue group create input.
 *   - BASE_CSF_TILER_OOM_EXCEPTION_FLAG.
 *
 * New in 1.13:
 *   - KBASE_IOCTL_READ_USER_PAGE ioctl.
 *
 * New in 1.14:
 *   - buf_desc_va field in the tiler heap init input.
 *   - Legacy CS_TILER_HEAP_INIT_1_13 preserved on the same ioctl
 *     number.
 */

#ifndef MANVIL_KBASE_ABI_1_14_H
#define MANVIL_KBASE_ABI_1_14_H

#include "manvil_abi_common.h"
#include "manvil_abi_1_10.h"

/*
 * Version of the UAPI that this delta enables.
 */
#define MANVIL_KBASE_UAPI_1_14 14

/*
 * Queue group create input with the csi_handlers field.
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
 *   csi_handlers    u8   (introduced in 1.12, was padding before)
 *   padding         2 bytes
 *   reserved        u64
 */
union manvil_kbase_ioctl_cs_queue_group_create_v2 {
    struct {
        uint64_t tiler_mask;
        uint64_t fragment_mask;
        uint64_t compute_mask;
        uint8_t cs_min;
        uint8_t priority;
        uint8_t tiler_max;
        uint8_t fragment_max;
        uint8_t compute_max;
        uint8_t csi_handlers;
        uint8_t padding[2];
        uint64_t reserved;
    } in;
    struct {
        uint8_t group_handle;
        uint8_t padding[3];
        uint32_t group_uid;
    } out;
};

#define MANVIL_KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_V2 \
    MANVIL_IOWR(MANVIL_KBASE_IOCTL_TYPE, 58, \
                union manvil_kbase_ioctl_cs_queue_group_create_v2)

/*
 * Tiler heap init input with the buf_desc_va field.
 *
 * The layout is:
 *   chunk_size          u32
 *   initial_chunks      u32
 *   max_chunks          u32
 *   target_in_flight    u16
 *   group_id            u8
 *   padding             u8
 *   buf_desc_va         u64   (introduced in 1.14)
 */
union manvil_kbase_ioctl_cs_tiler_heap_init_v2 {
    struct {
        uint32_t chunk_size;
        uint32_t initial_chunks;
        uint32_t max_chunks;
        uint16_t target_in_flight;
        uint8_t group_id;
        uint8_t padding;
        uint64_t buf_desc_va;
    } in;
    struct {
        uint64_t gpu_heap_va;
        uint64_t first_chunk_va;
    } out;
};

#define MANVIL_KBASE_IOCTL_CS_TILER_HEAP_INIT_V2 \
    MANVIL_IOWR(MANVIL_KBASE_IOCTL_TYPE, 48, \
                union manvil_kbase_ioctl_cs_tiler_heap_init_v2)

/*
 * Tiler heap init legacy variant, kept for kernels that do not
 * support the buf_desc_va field. Shares ioctl number 48.
 */
union manvil_kbase_ioctl_cs_tiler_heap_init_1_13 {
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

#define MANVIL_KBASE_IOCTL_CS_TILER_HEAP_INIT_1_13 \
    MANVIL_IOWR(MANVIL_KBASE_IOCTL_TYPE, 48, \
                union manvil_kbase_ioctl_cs_tiler_heap_init_1_13)

/*
 * READ_USER_PAGE ioctl, introduced in 1.13.
 *
 * Reads a 32-bit or 64-bit register from the bound queue USER page
 * by offset. For 64-bit registers, val_lo contains the low word and
 * val_hi the high word.
 */
union manvil_kbase_ioctl_read_user_page {
    struct {
        uint32_t offset;
        uint32_t padding;
    } in;
    struct {
        uint32_t val_lo;
        uint32_t val_hi;
    } out;
};

#define MANVIL_KBASE_IOCTL_READ_USER_PAGE \
    MANVIL_IOWR(MANVIL_KBASE_IOCTL_TYPE, 60, \
                union manvil_kbase_ioctl_read_user_page)

#endif /* MANVIL_KBASE_ABI_1_14_H */
