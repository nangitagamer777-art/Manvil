/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * Synchronization objects implementation.
 */

/*
 * Enable POSIX 2008 features.
 *
 * The default glibc feature selection when compiling with -std=c11
 * hides clock_gettime, CLOCK_MONOTONIC and usleep. Those are defined
 * by POSIX, not by ISO C, so they require an explicit opt-in.
 */
#define _POSIX_C_SOURCE 200809L

#include "sync.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "kernel_api/abi/manvil_abi.h"

/*
 * Internal sync structure.
 */
struct manvil_sync {
    manvil_kbase *kbase;
    manvil_mem   *mem;
    bool          cross_group;
    bool          is_valid;
};

/*
 * CPU pointer to the value field.
 */
static volatile uint64_t *sync_value_ptr(const manvil_sync *sync)
{
    uint8_t *base = (uint8_t *)manvil_mem_cpu_ptr(sync->mem);
    return (volatile uint64_t *)(base + MANVIL_SYNC_VALUE_OFFSET);
}

/*
 * CPU pointer to the error field.
 */
static volatile uint64_t *sync_error_ptr(const manvil_sync *sync)
{
    uint8_t *base = (uint8_t *)manvil_mem_cpu_ptr(sync->mem);
    return (volatile uint64_t *)(base + MANVIL_SYNC_ERROR_OFFSET);
}

/*
 * Return the current time in nanoseconds using CLOCK_MONOTONIC.
 */
static int64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000ll + (int64_t)ts.tv_nsec;
}

manvil_sync *manvil_sync_create(manvil_kbase *kbase, bool cross_group)
{
    if (kbase == NULL) {
        errno = EINVAL;
        return NULL;
    }

    manvil_sync *sync = calloc(1, sizeof(*sync));
    if (sync == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    /*
     * Select the allocation flags.
     *
     * Cross-group syncs need the CSF_EVENT flag. The kernel forces
     * an uncached mapping for those, which is the coherence model
     * required for visibility between command stream groups.
     *
     * Intra-group syncs use plain read-write memory. This is cheaper
     * and sufficient when both the signaler and the waiter are in
     * the same group.
     */
    uint32_t flags = MANVIL_MEM_CPU_READ |
                     MANVIL_MEM_CPU_WRITE |
                     MANVIL_MEM_GPU_READ |
                     MANVIL_MEM_GPU_WRITE;
    if (cross_group) {
        flags |= MANVIL_BASE_MEM_CSF_EVENT;
    }

    sync->mem = manvil_mem_alloc(kbase, MANVIL_SYNC_SIZE_BYTES, flags);
    if (sync->mem == NULL) {
        free(sync);
        return NULL;
    }

    /*
     * Zero the value and error fields. A fresh sync object starts at
     * value 0, error 0.
     */
    uint8_t *base = (uint8_t *)manvil_mem_cpu_ptr(sync->mem);
    memset(base, 0, MANVIL_SYNC_SIZE_BYTES);

    sync->kbase       = kbase;
    sync->cross_group = cross_group;
    sync->is_valid    = true;
    return sync;
}

void manvil_sync_destroy(manvil_sync *sync)
{
    if (sync == NULL) {
        return;
    }

    sync->is_valid = false;

    if (sync->mem != NULL) {
        manvil_mem_free(sync->mem);
        sync->mem = NULL;
    }

    memset(sync, 0, sizeof(*sync));
    free(sync);
}

uint64_t manvil_sync_gpu_va(const manvil_sync *sync)
{
    if (sync == NULL || sync->mem == NULL) {
        return 0;
    }
    return manvil_mem_gpu_va(sync->mem);
}

uint64_t manvil_sync_value(const manvil_sync *sync)
{
    if (sync == NULL || !sync->is_valid) {
        return 0;
    }
    return *sync_value_ptr(sync);
}

uint64_t manvil_sync_error(const manvil_sync *sync)
{
    if (sync == NULL || !sync->is_valid) {
        return 0;
    }
    return *sync_error_ptr(sync);
}

manvil_mem *manvil_sync_mem(const manvil_sync *sync)
{
    return sync != NULL ? sync->mem : NULL;
}

bool manvil_sync_is_valid(const manvil_sync *sync)
{
    return sync != NULL && sync->is_valid;
}

bool manvil_sync_is_cross_group(const manvil_sync *sync)
{
    return sync != NULL && sync->cross_group;
}

bool manvil_sync_is_signaled(const manvil_sync *sync, uint64_t target)
{
    if (sync == NULL || !sync->is_valid) {
        return false;
    }
    return *sync_value_ptr(sync) >= target;
}

bool manvil_sync_has_error(const manvil_sync *sync)
{
    if (sync == NULL || !sync->is_valid) {
        return false;
    }
    return *sync_error_ptr(sync) != 0;
}

void manvil_sync_clear_error(manvil_sync *sync)
{
    if (sync == NULL || !sync->is_valid) {
        return;
    }
    *sync_error_ptr(sync) = 0;
}

int manvil_sync_signal_from_kcpu(manvil_kcpu *kcpu,
                                  manvil_sync *sync,
                                  uint64_t value)
{
    if (kcpu == NULL || sync == NULL || !sync->is_valid) {
        errno = EINVAL;
        return -1;
    }

    /*
     * Use the 64-bit set operation. The kernel writes the value
     * directly. This matches the semantics of "signal the sync to
     * this value" that timeline semaphores expect.
     */
    return manvil_kcpu_add_cqs_set_operation(
        kcpu,
        manvil_sync_gpu_va(sync),
        value,
        MANVIL_BASEP_CQS_SET_OPERATION_SET,
        MANVIL_BASEP_CQS_DATA_TYPE_U64);
}

int manvil_sync_wait_from_kcpu(manvil_kcpu *kcpu,
                                manvil_sync *sync,
                                uint64_t value,
                                uint8_t operation)
{
    if (kcpu == NULL || sync == NULL || !sync->is_valid) {
        errno = EINVAL;
        return -1;
    }

    return manvil_kcpu_add_cqs_wait_operation(
        kcpu,
        manvil_sync_gpu_va(sync),
        value,
        operation,
        MANVIL_BASEP_CQS_DATA_TYPE_U64);
}

int manvil_sync_wait_cpu(manvil_sync *sync,
                          uint64_t target_value,
                          int64_t timeout_ns)
{
    if (sync == NULL || !sync->is_valid) {
        errno = EINVAL;
        return -1;
    }

    /*
     * Fast path: check once. If already signaled, return
     * immediately without waiting.
     */
    if (*sync_value_ptr(sync) >= target_value) {
        return 0;
    }

    /*
     * Non-blocking check when timeout is zero.
     */
    if (timeout_ns == 0) {
        return 1;
    }

    int64_t start = now_ns();
    int64_t deadline;
    bool bounded = (timeout_ns > 0);

    if (bounded) {
        deadline = start + timeout_ns;
    } else {
        deadline = 0;
    }

    /*
     * Poll loop. A short sleep between checks keeps the CPU cost
     * low. For sync objects that complete quickly, the first few
     * iterations complete without sleeping.
     */
    for (;;) {
        if (*sync_value_ptr(sync) >= target_value) {
            return 0;
        }

        if (*sync_error_ptr(sync) != 0) {
            errno = EIO;
            return -1;
        }

        if (bounded && now_ns() >= deadline) {
            return 1;
        }

        /*
         * Sleep for a short interval. nanosleep takes a timespec
         * with nanosecond precision. 100 microseconds keeps latency
         * negligible and the CPU idle.
         */
        struct timespec delay;
        delay.tv_sec = 0;
        delay.tv_nsec = 100000; /* 100 microseconds */
        nanosleep(&delay, NULL);
    }
}

int manvil_sync_flush_to_device(manvil_sync *sync)
{
    if (sync == NULL || !sync->is_valid) {
        errno = EINVAL;
        return -1;
    }

    return manvil_mem_sync(sync->mem, MANVIL_MEM_SYNC_TO_DEVICE);
}

int manvil_sync_invalidate_from_device(manvil_sync *sync)
{
    if (sync == NULL || !sync->is_valid) {
        errno = EINVAL;
        return -1;
    }

    return manvil_mem_sync(sync->mem, MANVIL_MEM_SYNC_FROM_DEVICE);
}
