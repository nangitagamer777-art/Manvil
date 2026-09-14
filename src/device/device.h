/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * Device layer.
 *
 * A manvil_device represents an open context on a Kbase GPU. It owns
 * the low level backend, holds the results of the initial handshake,
 * and exposes the GPU properties and CSF capabilities that higher
 * layers depend on.
 *
 * The device layer is responsible for:
 *   - opening the Kbase backend and performing the UAPI version check
 *   - setting the context flags required by Manvil
 *   - mapping the memory tracking page
 *   - reading GPU properties through GET_GPUPROPS
 *   - reading the CSF global interface through CS_GET_GLB_IFACE
 *   - releasing all of the above in reverse order on close
 *
 * Higher layers (memory, CSF, KCPU) receive a manvil_device and use it
 * to reach the backend when they need to issue ioctls.
 */

#ifndef MANVIL_DEVICE_H
#define MANVIL_DEVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel_api/manvil_kbase.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Opaque handle to a device.
 */
typedef struct manvil_device manvil_device;

/*
 * GPU core properties, read once during open.
 *
 * These fields are a subset of what GET_GPUPROPS reports. Manvil only
 * keeps the ones that higher layers need to make decisions. Additional
 * fields can be added later without changing the API surface.
 */
struct manvil_gpu_props {
    uint32_t product_id;
    uint16_t version_status;
    uint16_t minor_revision;
    uint16_t major_revision;
    uint32_t gpu_freq_khz_max;
    uint64_t available_memory_bytes;

    /*
     * Mask of shader cores present, from the RAW_SHADER_PRESENT
     * property. Used to size the compute groups.
     */
    uint64_t shader_present;

    /*
     * Mask of tiler units present, from RAW_TILER_PRESENT.
     */
    uint64_t tiler_present;

    /*
     * Mask of L2 slices present, from RAW_L2_PRESENT.
     */
    uint64_t l2_present;

    /*
     * Number of execution engines reported by the kernel.
     */
    uint32_t num_exec_engines;
};

/*
 * CSF global interface, read once during open.
 *
 * These values come from CS_GET_GLB_IFACE and describe the CSF
 * capabilities that the firmware reported at boot. They do not change
 * during the lifetime of the device.
 */
struct manvil_csf_iface {
    uint32_t glb_version;
    uint32_t features;
    uint32_t group_num;
    uint32_t total_stream_num;
    uint32_t prfcnt_size;
    uint32_t instr_features;

    /*
     * Values extracted from the first CSI reported by the firmware.
     * They describe the per stream capabilities that the firmware
     * exposes at boot.
     *
     * work_registers      total number of 32-bit registers in the
     *                     CS register file.
     * scoreboards         number of scoreboard entries available for
     *                     tracking asynchronous operations.
     * user_register_base  index of the first register that userspace
     *                     may use. The top four registers are
     *                     reserved for the application; this is the
     *                     index of the first of those four.
     *
     * The exact number of registers is a hardware property and can
     * differ across GPU revisions. Manvil queries it at open time
     * instead of hardcoding a value.
     */
    uint32_t work_registers;
    uint32_t scoreboards;
    uint32_t user_register_base;
};

/*
 * Open a Manvil device on the given path.
 *
 * path     Device node, typically "/dev/mali0".
 *
 * On success, returns a handle. On failure, returns NULL. The reason
 * for the failure is available through manvil_device_last_error() on
 * the handle. If the handle itself is NULL, the failure reason is not
 * available through this API; callers should log the path they tried.
 *
 * The open sequence is:
 *   1. manvil_kbase_open(path)
 *   2. SET_FLAGS with CSF_EVENT_THREAD
 *   3. mmap of the memory tracking page
 *   4. GET_GPUPROPS in two calls, first for size then for data
 *   5. CS_GET_GLB_IFACE with enough room for all groups and streams
 *
 * If any step fails, all previously acquired resources are released
 * and the function returns NULL.
 */
manvil_device *manvil_device_open(const char *path);

/*
 * Close a device and release all resources.
 *
 * Passing NULL is a no-op.
 */
void manvil_device_close(manvil_device *dev);

/*
 * Access the low level backend.
 *
 * The returned pointer is owned by the device and remains valid until
 * the device is closed.
 */
manvil_kbase *manvil_device_kbase(manvil_device *dev);

/*
 * Access the GPU properties recorded at open time.
 *
 * Returns a pointer to an internal structure that remains valid until
 * the device is closed.
 */
const struct manvil_gpu_props *manvil_device_gpu_props(manvil_device *dev);

/*
 * Access the CSF global interface recorded at open time.
 */
const struct manvil_csf_iface *manvil_device_csf_iface(manvil_device *dev);

/*
 * Access the memory tracking page.
 *
 * Returns the virtual address at which the tracking page is mapped in
 * this process, or NULL if the mapping was not created.
 */
void *manvil_device_tracking_page(manvil_device *dev);

/*
 * Feature query.
 *
 * Shortcut for the same query on the backend. Returns false if the
 * device is NULL or the feature is not available.
 */
bool manvil_device_has_feature(manvil_device *dev, uint64_t feature);

/*
 * Last error message.
 *
 * Returns a NUL terminated string describing the most recent failure
 * on this device. The string is stored inside the device and remains
 * valid until the next operation that modifies it, or until the
 * device is closed.
 *
 * Returns an empty string if the device is NULL.
 */
const char *manvil_device_last_error(manvil_device *dev);

/*
 * Clear the last error buffer.
 *
 * Higher layers call this after reporting an error so that subsequent
 * calls to last_error only return fresh failures.
 */
void manvil_device_clear_error(manvil_device *dev);

#ifdef __cplusplus
}
#endif

#endif /* MANVIL_DEVICE_H */
