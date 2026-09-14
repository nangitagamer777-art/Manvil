/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * Common Kbase UAPI definitions shared by all supported CSF versions.
 *
 * This header contains everything that does not change between UAPI
 * versions: the ioctl type byte, the ioctl encoding macros, universal
 * memory and context flags, and structures whose layout is stable
 * across all CSF versions.
 *
 * Version specific additions live in manvil_abi_1_XX.h.
 */

#ifndef MANVIL_KBASE_ABI_COMMON_H
#define MANVIL_KBASE_ABI_COMMON_H

#include <stdint.h>

/*
 * Ioctl type byte for all Kbase ioctls.
 */
#define MANVIL_KBASE_IOCTL_TYPE 0x80

/*
 * Page size assumed by the ABI. The Kbase UAPI hardcodes this value
 * for the layout of special handles.
 */
#define MANVIL_KBASE_PAGE_SHIFT 12

/*
 * Version encoding used by the Kbase API surface.
 *
 * Bits 31-20  major
 * Bits 19-8   minor
 * Bits  7-0   patch
 */
#define MANVIL_KBASE_API_VERSION(major, minor) \
    ((((uint32_t)(major) & 0xFFFu) << 20) | \
     (((uint32_t)(minor) & 0xFFFu) << 8))

#define MANVIL_KBASE_API_MAJOR(v) (((v) >> 20) & 0xFFFu)
#define MANVIL_KBASE_API_MINOR(v) (((v) >>  8) & 0xFFFu)

/*
 * Ioctl encoding macros, following the standard Linux conventions.
 *
 *   bits 31-30  direction (00 none, 01 write, 10 read, 11 read/write)
 *   bits 29-16  size of the argument
 *   bits 15-8   type
 *   bits  7-0   number
 */
#define MANVIL_IOC_NONE  0u
#define MANVIL_IOC_WRITE 1u
#define MANVIL_IOC_READ  2u

#define MANVIL_IOC_NRBITS   8
#define MANVIL_IOC_TYPEBITS 8
#define MANVIL_IOC_SIZEBITS 14
#define MANVIL_IOC_DIRBITS  2

#define MANVIL_IOC_NRSHIFT   0
#define MANVIL_IOC_TYPESHIFT (MANVIL_IOC_NRSHIFT + MANVIL_IOC_NRBITS)
#define MANVIL_IOC_SIZESHIFT (MANVIL_IOC_TYPESHIFT + MANVIL_IOC_TYPEBITS)
#define MANVIL_IOC_DIRSHIFT  (MANVIL_IOC_SIZESHIFT + MANVIL_IOC_SIZEBITS)

#define MANVIL_IOC(dir, type, nr, size) \
    (((dir)  << MANVIL_IOC_DIRSHIFT) | \
     ((type) << MANVIL_IOC_TYPESHIFT) | \
     ((nr)   << MANVIL_IOC_NRSHIFT) | \
     ((size) << MANVIL_IOC_SIZESHIFT))

#define MANVIL_IOC_TYPECHECK(t) (sizeof(t))

#define MANVIL_IO(type, nr) \
    MANVIL_IOC(MANVIL_IOC_NONE, (type), (nr), 0)
#define MANVIL_IOR(type, nr, size) \
    MANVIL_IOC(MANVIL_IOC_READ, (type), (nr), MANVIL_IOC_TYPECHECK(size))
#define MANVIL_IOW(type, nr, size) \
    MANVIL_IOC(MANVIL_IOC_WRITE, (type), (nr), MANVIL_IOC_TYPECHECK(size))
#define MANVIL_IOWR(type, nr, size) \
    MANVIL_IOC(MANVIL_IOC_READ | MANVIL_IOC_WRITE, (type), (nr), \
               MANVIL_IOC_TYPECHECK(size))

/*
 * Memory allocation flags. These are present in all CSF versions.
 */
#define MANVIL_BASE_MEM_PROT_CPU_RD              ((uint32_t)1u << 0)
#define MANVIL_BASE_MEM_PROT_CPU_WR              ((uint32_t)1u << 1)
#define MANVIL_BASE_MEM_PROT_GPU_RD              ((uint32_t)1u << 2)
#define MANVIL_BASE_MEM_PROT_GPU_WR              ((uint32_t)1u << 3)
#define MANVIL_BASE_MEM_PROT_GPU_EX              ((uint32_t)1u << 4)
#define MANVIL_BASE_MEM_GPU_VA_SAME_4GB_PAGE     ((uint32_t)1u << 6)
#define MANVIL_BASE_MEM_FIXED                    ((uint32_t)1u << 8)
#define MANVIL_BASE_MEM_GROW_ON_GPF              ((uint32_t)1u << 9)
#define MANVIL_BASE_MEM_COHERENT_SYSTEM          ((uint32_t)1u << 10)
#define MANVIL_BASE_MEM_COHERENT_LOCAL           ((uint32_t)1u << 11)
#define MANVIL_BASE_MEM_CACHED_CPU               ((uint32_t)1u << 12)
#define MANVIL_BASE_MEM_SAME_VA                  ((uint32_t)1u << 13)
#define MANVIL_BASE_MEM_NEED_MMAP                ((uint32_t)1u << 14)
#define MANVIL_BASE_MEM_COHERENT_SYSTEM_REQUIRED ((uint32_t)1u << 15)
#define MANVIL_BASE_MEM_DONT_NEED                ((uint32_t)1u << 17)
#define MANVIL_BASE_MEM_IMPORT_SHARED            ((uint32_t)1u << 18)
#define MANVIL_BASE_MEM_CSF_EVENT                ((uint32_t)1u << 19)
#define MANVIL_BASE_MEM_UNCACHED_GPU             ((uint32_t)1u << 21)

/*
 * Memory group ID occupies bits 22 to 25 of the flags field.
 */
#define MANVIL_BASE_MEM_GROUP_ID_SHIFT 22
#define MANVIL_BASE_MEM_GROUP_ID_MASK \
    ((uint32_t)0xFu << MANVIL_BASE_MEM_GROUP_ID_SHIFT)

/*
 * Pre-reserved GPU virtual addresses for special purposes. The value
 * is (index << PAGE_SHIFT).
 */
#define MANVIL_BASE_MEM_INVALID_HANDLE           ((uint64_t)0ull)
#define MANVIL_BASE_MEM_MMU_DUMP_HANDLE          ((uint64_t)1ull << MANVIL_KBASE_PAGE_SHIFT)
#define MANVIL_BASE_MEM_TRACE_BUFFER_HANDLE      ((uint64_t)2ull << MANVIL_KBASE_PAGE_SHIFT)
#define MANVIL_BASE_MEM_MAP_TRACKING_HANDLE      ((uint64_t)3ull << MANVIL_KBASE_PAGE_SHIFT)
#define MANVIL_BASE_MEM_WRITE_ALLOC_PAGES_HANDLE ((uint64_t)4ull << MANVIL_KBASE_PAGE_SHIFT)
#define MANVIL_BASE_MEM_CSF_USER_REG_PAGE_HANDLE ((uint64_t)47ull << MANVIL_KBASE_PAGE_SHIFT)
#define MANVIL_BASE_MEM_CSF_USER_IO_PAGES_HANDLE ((uint64_t)48ull << MANVIL_KBASE_PAGE_SHIFT)
#define MANVIL_BASE_MEM_COOKIE_BASE              ((uint64_t)64ull << MANVIL_KBASE_PAGE_SHIFT)

/*
 * Number of pages mapped for a bound GPU command queue.
 */
#define MANVIL_BASEP_QUEUE_NR_MMAP_USER_PAGES 3

/*
 * Maximum priority for a queue within a group.
 */
#define MANVIL_BASE_QUEUE_MAX_PRIORITY 15u

/*
 * Context creation flags for SET_FLAGS.
 */
#define MANVIL_BASE_CONTEXT_CREATE_FLAG_NONE                ((uint32_t)0)
#define MANVIL_BASE_CONTEXT_CCTX_EMBEDDED                   ((uint32_t)1u << 0)
#define MANVIL_BASE_CONTEXT_SYSTEM_MONITOR_SUBMIT_DISABLED  ((uint32_t)1u << 1)
#define MANVIL_BASE_CONTEXT_CSF_EVENT_THREAD                ((uint32_t)1u << 2)
#define MANVIL_BASE_CONTEXT_MMU_GROUP_ID_SHIFT              3
#define MANVIL_BASE_CONTEXT_MMU_GROUP_ID_MASK \
    ((uint32_t)0xFu << MANVIL_BASE_CONTEXT_MMU_GROUP_ID_SHIFT)

/*
 * Progress timer scaling. Each unit of the progress timeout register
 * corresponds to 1024 GPU cycles.
 */
#define MANVIL_GLB_PROGRESS_TIMER_TIMEOUT_SCALE ((uint64_t)1024)

/*
 * Priority values in the UAPI. Lower numbers mean higher priority.
 */
#define MANVIL_BASE_QUEUE_GROUP_PRIORITY_HIGH     0u
#define MANVIL_BASE_QUEUE_GROUP_PRIORITY_MEDIUM   1u
#define MANVIL_BASE_QUEUE_GROUP_PRIORITY_LOW      2u
#define MANVIL_BASE_QUEUE_GROUP_PRIORITY_REALTIME 3u
#define MANVIL_BASE_QUEUE_GROUP_PRIORITY_COUNT    4u

/*
 * KCPU command types.
 */
#define MANVIL_BASE_KCPU_COMMAND_TYPE_FENCE_SIGNAL       0u
#define MANVIL_BASE_KCPU_COMMAND_TYPE_FENCE_WAIT         1u
#define MANVIL_BASE_KCPU_COMMAND_TYPE_CQS_WAIT           2u
#define MANVIL_BASE_KCPU_COMMAND_TYPE_CQS_SET            3u
#define MANVIL_BASE_KCPU_COMMAND_TYPE_CQS_WAIT_OPERATION 4u
#define MANVIL_BASE_KCPU_COMMAND_TYPE_CQS_SET_OPERATION  5u
#define MANVIL_BASE_KCPU_COMMAND_TYPE_MAP_IMPORT         6u
#define MANVIL_BASE_KCPU_COMMAND_TYPE_UNMAP_IMPORT       7u
#define MANVIL_BASE_KCPU_COMMAND_TYPE_UNMAP_IMPORT_FORCE 8u
#define MANVIL_BASE_KCPU_COMMAND_TYPE_JIT_ALLOC          9u
#define MANVIL_BASE_KCPU_COMMAND_TYPE_JIT_FREE          10u
#define MANVIL_BASE_KCPU_COMMAND_TYPE_GROUP_SUSPEND     11u
#define MANVIL_BASE_KCPU_COMMAND_TYPE_ERROR_BARRIER     12u

/*
 * CQS data types and operations.
 */
#define MANVIL_BASEP_CQS_DATA_TYPE_U32 0u
#define MANVIL_BASEP_CQS_DATA_TYPE_U64 1u

#define MANVIL_BASEP_CQS_WAIT_OPERATION_LE 0u
#define MANVIL_BASEP_CQS_WAIT_OPERATION_GT 1u

#define MANVIL_BASEP_CQS_SET_OPERATION_ADD 0u
#define MANVIL_BASEP_CQS_SET_OPERATION_SET 1u

/*
 * Maximum number of objects per CQS operation.
 */
#define MANVIL_BASEP_KCPU_CQS_MAX_NUM_OBJS 32u

/*
 * CSF notification types.
 */
#define MANVIL_BASE_CSF_NOTIFICATION_EVENT                 0u
#define MANVIL_BASE_CSF_NOTIFICATION_GPU_QUEUE_GROUP_ERROR 1u
#define MANVIL_BASE_CSF_NOTIFICATION_CPU_QUEUE_DUMP        2u

/*
 * GPU queue group error types.
 */
#define MANVIL_BASE_GPU_QUEUE_GROUP_ERROR_FATAL           0u
#define MANVIL_BASE_GPU_QUEUE_GROUP_QUEUE_ERROR_FATAL     1u
#define MANVIL_BASE_GPU_QUEUE_GROUP_ERROR_TIMEOUT         2u
#define MANVIL_BASE_GPU_QUEUE_GROUP_ERROR_TILER_HEAP_OOM  3u

/*
 * CSI exception handler flags.
 */
#define MANVIL_BASE_CSF_TILER_OOM_EXCEPTION_FLAG ((uint32_t)1u << 0)
#define MANVIL_BASE_CSF_EXCEPTION_HANDLER_FLAGS_MASK \
    (MANVIL_BASE_CSF_TILER_OOM_EXCEPTION_FLAG)

/*
 * Memory import types.
 */
#define MANVIL_BASE_MEM_IMPORT_TYPE_INVALID     0u
#define MANVIL_BASE_MEM_IMPORT_TYPE_UMM         2u
#define MANVIL_BASE_MEM_IMPORT_TYPE_USER_BUFFER 3u

/*
 * Sync32 and Sync64 object layouts.
 */
#define MANVIL_BASEP_EVENT32_VAL_OFFSET  0u
#define MANVIL_BASEP_EVENT32_ERR_OFFSET  4u
#define MANVIL_BASEP_EVENT32_SIZE_BYTES  8u
#define MANVIL_BASEP_EVENT32_ALIGN_BYTES 8u

#define MANVIL_BASEP_EVENT64_VAL_OFFSET  0u
#define MANVIL_BASEP_EVENT64_ERR_OFFSET  8u
#define MANVIL_BASEP_EVENT64_SIZE_BYTES  16u
#define MANVIL_BASEP_EVENT64_ALIGN_BYTES 16u

/*
 * Structures whose layout is stable across all CSF versions.
 */

struct manvil_kbase_ioctl_version_check {
    uint16_t major;
    uint16_t minor;
};

struct manvil_kbase_ioctl_set_flags {
    uint32_t create_flags;
};

struct manvil_kbase_ioctl_get_gpuprops {
    uint64_t buffer;
    uint32_t size;
    uint32_t flags;
};

union manvil_kbase_ioctl_mem_alloc {
    struct {
        uint64_t va_pages;
        uint64_t commit_pages;
        uint64_t extension;
        uint64_t flags;
    } in;
    struct {
        uint64_t flags;
        uint64_t gpu_va;
    } out;
};

struct manvil_kbase_ioctl_mem_free {
    uint64_t gpu_addr;
};

/*
 * Memory query.
 *
 * The gpu_addr is a value previously returned by MEM_ALLOC. The query
 * field selects what to return:
 *   1  MANVIL_MEM_QUERY_COMMIT_SIZE  physical pages committed
 *   2  MANVIL_MEM_QUERY_VA_SIZE      virtual pages reserved
 *   3  MANVIL_MEM_QUERY_FLAGS        current flags
 */
union manvil_kbase_ioctl_mem_query {
    struct {
        uint64_t gpu_addr;
        uint64_t query;
    } in;
    struct {
        uint64_t value;
    } out;
};

struct manvil_kbase_ioctl_mem_sync {
    uint64_t handle;
    uint64_t user_addr;
    uint64_t size;
    uint8_t type;
    uint8_t padding[7];
};

struct manvil_kbase_ioctl_mem_exec_init {
    uint64_t va_pages;
};

struct manvil_kbase_ioctl_cs_queue_register {
    uint64_t buffer_gpu_addr;
    uint32_t buffer_size;
    uint8_t priority;
    uint8_t padding[3];
};

struct manvil_kbase_ioctl_cs_queue_kick {
    uint64_t buffer_gpu_addr;
};

union manvil_kbase_ioctl_cs_queue_bind {
    struct {
        uint64_t buffer_gpu_addr;
        uint8_t group_handle;
        uint8_t csi_index;
        uint8_t padding[6];
    } in;
    struct {
        uint64_t mmap_handle;
    } out;
};

struct manvil_kbase_ioctl_cs_queue_terminate {
    uint64_t buffer_gpu_addr;
};

struct manvil_kbase_ioctl_cs_queue_group_term {
    uint8_t group_handle;
    uint8_t padding[7];
};

struct manvil_kbase_ioctl_kcpu_queue_new {
    uint8_t id;
    uint8_t padding[7];
};

struct manvil_kbase_ioctl_kcpu_queue_delete {
    uint8_t id;
    uint8_t padding[7];
};

struct manvil_kbase_ioctl_kcpu_queue_enqueue {
    uint64_t addr;
    uint32_t nr_commands;
    uint8_t id;
    uint8_t padding[3];
};

struct manvil_kbase_ioctl_cs_tiler_heap_term {
    uint64_t gpu_heap_va;
};

union manvil_kbase_ioctl_cs_get_glb_iface {
    struct {
        uint32_t max_group_num;
        uint32_t max_total_stream_num;
        uint64_t groups_ptr;
        uint64_t streams_ptr;
    } in;
    struct {
        uint32_t glb_version;
        uint32_t features;
        uint32_t group_num;
        uint32_t prfcnt_size;
        uint32_t total_stream_num;
        uint32_t instr_features;
    } out;
};

struct manvil_kbase_ioctl_cs_cpu_queue_info {
    uint64_t buffer;
    uint64_t size;
};

struct manvil_kbase_ioctl_get_context_id {
    uint32_t id;
};

struct manvil_kbase_ioctl_stream_create {
    char name[32];
};

struct manvil_kbase_ioctl_tlstream_acquire {
    uint32_t flags;
};

struct manvil_kbase_ioctl_get_cpu_gpu_timeinfo_in {
    uint32_t request_flags;
    uint32_t paddings[7];
};

struct manvil_kbase_ioctl_get_cpu_gpu_timeinfo_out {
    uint64_t sec;
    uint32_t nsec;
    uint32_t padding;
    uint64_t timestamp;
    uint64_t cycle_counter;
};

union manvil_kbase_ioctl_get_cpu_gpu_timeinfo {
    struct manvil_kbase_ioctl_get_cpu_gpu_timeinfo_in in;
    struct manvil_kbase_ioctl_get_cpu_gpu_timeinfo_out out;
};

/*
 * KCPU command payloads.
 */
struct manvil_base_kcpu_command_fence_info {
    uint64_t fence;
};

struct manvil_base_cqs_wait_info {
    uint64_t addr;
    uint32_t val;
    uint32_t padding;
};

struct manvil_base_kcpu_command_cqs_wait_info {
    uint64_t objs;
    uint32_t nr_objs;
    uint32_t inherit_err_flags;
};

struct manvil_base_cqs_set {
    uint64_t addr;
};

struct manvil_base_kcpu_command_cqs_set_info {
    uint64_t objs;
    uint32_t nr_objs;
    uint32_t padding;
};

struct manvil_base_cqs_wait_operation_info {
    uint64_t addr;
    uint64_t val;
    uint8_t operation;
    uint8_t data_type;
    uint8_t padding[6];
};

struct manvil_base_kcpu_command_cqs_wait_operation_info {
    uint64_t objs;
    uint32_t nr_objs;
    uint32_t inherit_err_flags;
};

struct manvil_base_cqs_set_operation_info {
    uint64_t addr;
    uint64_t val;
    uint8_t operation;
    uint8_t data_type;
    uint8_t padding[6];
};

struct manvil_base_kcpu_command_cqs_set_operation_info {
    uint64_t objs;
    uint32_t nr_objs;
    uint32_t padding;
};

struct manvil_base_kcpu_command_import_info {
    uint64_t handle;
};

struct manvil_base_kcpu_command_jit_alloc_info {
    uint64_t info;
    uint8_t count;
    uint8_t padding[7];
};

struct manvil_base_kcpu_command_jit_free_info {
    uint64_t ids;
    uint8_t count;
    uint8_t padding[7];
};

struct manvil_base_kcpu_command_group_suspend_info {
    uint64_t buffer;
    uint32_t size;
    uint8_t group_handle;
    uint8_t padding[3];
};

struct manvil_base_kcpu_command {
    uint8_t type;
    uint8_t padding[7];
    union {
        struct manvil_base_kcpu_command_fence_info              fence;
        struct manvil_base_kcpu_command_cqs_wait_info           cqs_wait;
        struct manvil_base_kcpu_command_cqs_set_info            cqs_set;
        struct manvil_base_kcpu_command_cqs_wait_operation_info cqs_wait_operation;
        struct manvil_base_kcpu_command_cqs_set_operation_info  cqs_set_operation;
        struct manvil_base_kcpu_command_import_info             import;
        struct manvil_base_kcpu_command_jit_alloc_info          jit_alloc;
        struct manvil_base_kcpu_command_jit_free_info           jit_free;
        struct manvil_base_kcpu_command_group_suspend_info      suspend_buf_copy;
        uint64_t padding[2];
    } info;
};

/*
 * JIT allocation information.
 */
struct manvil_base_jit_alloc_info {
    uint64_t gpu_alloc_addr;
    uint64_t va_pages;
    uint64_t commit_pages;
    uint64_t extension;
    uint8_t id;
    uint8_t bin_id;
    uint8_t max_allocations;
    uint8_t flags;
    uint8_t padding[2];
    uint16_t usage_id;
    uint64_t heap_info_gpu_addr;
};

/*
 * CSF group and stream control.
 */
struct manvil_basep_cs_group_control {
    uint32_t features;
    uint32_t stream_num;
    uint32_t suspend_size;
    uint32_t padding;
};

struct manvil_basep_cs_stream_control {
    uint32_t features;
    uint32_t padding;
};

/*
 * CSF notification structures.
 */
struct manvil_base_gpu_queue_group_error_fatal_payload {
    uint64_t sideband;
    uint32_t status;
    uint32_t padding;
};

struct manvil_base_gpu_queue_error_fatal_payload {
    uint64_t sideband;
    uint32_t status;
    uint8_t csi_index;
    uint8_t padding[3];
};

struct manvil_base_gpu_queue_group_error {
    uint8_t error_type;
    uint8_t padding[7];
    union {
        struct manvil_base_gpu_queue_group_error_fatal_payload fatal_group;
        struct manvil_base_gpu_queue_error_fatal_payload       fatal_queue;
    } payload;
};

struct manvil_base_csf_notification {
    uint8_t type;
    uint8_t padding[7];
    union {
        struct {
            uint8_t handle;
            uint8_t padding[7];
            struct manvil_base_gpu_queue_group_error error;
        } csg_error;

        uint8_t align[56];
    } payload;
};

/*
 * Core ioctls that are stable across all CSF versions.
 */
#define MANVIL_KBASE_IOCTL_VERSION_CHECK \
    MANVIL_IOWR(MANVIL_KBASE_IOCTL_TYPE, 52, \
                struct manvil_kbase_ioctl_version_check)
#define MANVIL_KBASE_IOCTL_VERSION_CHECK_LEGACY \
    MANVIL_IOWR(MANVIL_KBASE_IOCTL_TYPE, 0, \
                struct manvil_kbase_ioctl_version_check)
#define MANVIL_KBASE_IOCTL_SET_FLAGS \
    MANVIL_IOW(MANVIL_KBASE_IOCTL_TYPE, 1, \
               struct manvil_kbase_ioctl_set_flags)
#define MANVIL_KBASE_IOCTL_GET_GPUPROPS \
    MANVIL_IOW(MANVIL_KBASE_IOCTL_TYPE, 3, \
               struct manvil_kbase_ioctl_get_gpuprops)
#define MANVIL_KBASE_IOCTL_MEM_ALLOC \
    MANVIL_IOWR(MANVIL_KBASE_IOCTL_TYPE, 5, \
                union manvil_kbase_ioctl_mem_alloc)
#define MANVIL_KBASE_IOCTL_MEM_FREE \
    MANVIL_IOW(MANVIL_KBASE_IOCTL_TYPE, 7, \
               struct manvil_kbase_ioctl_mem_free)
#define MANVIL_KBASE_IOCTL_MEM_QUERY \
    MANVIL_IOWR(MANVIL_KBASE_IOCTL_TYPE, 6, \
                union manvil_kbase_ioctl_mem_query)
#define MANVIL_KBASE_IOCTL_MEM_SYNC \
    MANVIL_IOW(MANVIL_KBASE_IOCTL_TYPE, 15, \
               struct manvil_kbase_ioctl_mem_sync)
#define MANVIL_KBASE_IOCTL_MEM_EXEC_INIT \
    MANVIL_IOW(MANVIL_KBASE_IOCTL_TYPE, 38, \
               struct manvil_kbase_ioctl_mem_exec_init)
#define MANVIL_KBASE_IOCTL_GET_CONTEXT_ID \
    MANVIL_IOR(MANVIL_KBASE_IOCTL_TYPE, 17, \
               struct manvil_kbase_ioctl_get_context_id)
#define MANVIL_KBASE_IOCTL_STREAM_CREATE \
    MANVIL_IOW(MANVIL_KBASE_IOCTL_TYPE, 24, \
               struct manvil_kbase_ioctl_stream_create)
#define MANVIL_KBASE_IOCTL_TLSTREAM_ACQUIRE \
    MANVIL_IOW(MANVIL_KBASE_IOCTL_TYPE, 18, \
               struct manvil_kbase_ioctl_tlstream_acquire)
#define MANVIL_KBASE_IOCTL_GET_CPU_GPU_TIMEINFO \
    MANVIL_IOWR(MANVIL_KBASE_IOCTL_TYPE, 50, \
                union manvil_kbase_ioctl_get_cpu_gpu_timeinfo)

#define MANVIL_KBASE_IOCTL_CS_QUEUE_REGISTER \
    MANVIL_IOW(MANVIL_KBASE_IOCTL_TYPE, 36, \
               struct manvil_kbase_ioctl_cs_queue_register)
#define MANVIL_KBASE_IOCTL_CS_QUEUE_KICK \
    MANVIL_IOW(MANVIL_KBASE_IOCTL_TYPE, 37, \
               struct manvil_kbase_ioctl_cs_queue_kick)
#define MANVIL_KBASE_IOCTL_CS_QUEUE_BIND \
    MANVIL_IOWR(MANVIL_KBASE_IOCTL_TYPE, 39, \
                union manvil_kbase_ioctl_cs_queue_bind)
#define MANVIL_KBASE_IOCTL_CS_QUEUE_TERMINATE \
    MANVIL_IOW(MANVIL_KBASE_IOCTL_TYPE, 41, \
               struct manvil_kbase_ioctl_cs_queue_terminate)
#define MANVIL_KBASE_IOCTL_CS_QUEUE_GROUP_TERMINATE \
    MANVIL_IOW(MANVIL_KBASE_IOCTL_TYPE, 43, \
               struct manvil_kbase_ioctl_cs_queue_group_term)
#define MANVIL_KBASE_IOCTL_KCPU_QUEUE_CREATE \
    MANVIL_IOR(MANVIL_KBASE_IOCTL_TYPE, 45, \
               struct manvil_kbase_ioctl_kcpu_queue_new)
#define MANVIL_KBASE_IOCTL_KCPU_QUEUE_DELETE \
    MANVIL_IOW(MANVIL_KBASE_IOCTL_TYPE, 46, \
               struct manvil_kbase_ioctl_kcpu_queue_delete)
#define MANVIL_KBASE_IOCTL_KCPU_QUEUE_ENQUEUE \
    MANVIL_IOW(MANVIL_KBASE_IOCTL_TYPE, 47, \
               struct manvil_kbase_ioctl_kcpu_queue_enqueue)
#define MANVIL_KBASE_IOCTL_CS_TILER_HEAP_TERM \
    MANVIL_IOW(MANVIL_KBASE_IOCTL_TYPE, 49, \
               struct manvil_kbase_ioctl_cs_tiler_heap_term)
#define MANVIL_KBASE_IOCTL_CS_GET_GLB_IFACE \
    MANVIL_IOWR(MANVIL_KBASE_IOCTL_TYPE, 51, \
                union manvil_kbase_ioctl_cs_get_glb_iface)
#define MANVIL_KBASE_IOCTL_CS_CPU_QUEUE_DUMP \
    MANVIL_IOW(MANVIL_KBASE_IOCTL_TYPE, 53, \
               struct manvil_kbase_ioctl_cs_cpu_queue_info)
#define MANVIL_KBASE_IOCTL_CS_EVENT_SIGNAL \
    MANVIL_IO(MANVIL_KBASE_IOCTL_TYPE, 44)

#endif /* MANVIL_KBASE_ABI_COMMON_H */
