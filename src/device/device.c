/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * Device layer implementation.
 */

#include "device.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "kernel_api/abi/manvil_abi.h"

/*
 * Size of the last error buffer inside the device.
 */
#define MANVIL_DEVICE_ERROR_MAX 256

/*
 * Number of shader cores reported by the GPU. Used as a safety bound
 * when reading GET_GPUPROPS output.
 */
#define MANVIL_DEVICE_MAX_CORES 256

/*
 * Internal device structure.
 */
struct manvil_device {
    manvil_kbase *kbase;

    /*
     * Context state set up during open.
     */
    void *tracking_page;
    size_t tracking_page_size;

    /*
     * Results of the initial queries.
     */
    struct manvil_gpu_props gpu_props;
    struct manvil_csf_iface csf_iface;

    /*
     * Copy of the feature bitmask for quick access. Kept in sync with
     * the backend at open time. Higher layers query this copy rather
     * than reaching into the backend.
     */
    uint64_t features;

    /*
     * Error buffer.
     */
    char last_error[MANVIL_DEVICE_ERROR_MAX];
};

/*
 * Record an error message in the device.
 */
static void device_set_error(manvil_device *dev, const char *fmt, ...)
{
    if (dev == NULL) {
        return;
    }

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(dev->last_error, sizeof(dev->last_error), fmt, ap);
    va_end(ap);
}

/*
 * Clear the error buffer.
 */
static void device_clear_error_internal(manvil_device *dev)
{
    if (dev == NULL) {
        return;
    }
    dev->last_error[0] = '\0';
}

/*
 * Perform the SET_FLAGS ioctl.
 *
 * Sets the context flags to request the CSF event notification thread,
 * which Manvil relies on for receiving CSF errors and notifications.
 */
static int device_set_flags(manvil_device *dev)
{
    struct manvil_kbase_ioctl_set_flags flags;
    memset(&flags, 0, sizeof(flags));

    /*
     * Pass zero for the context flags.
     *
     * The reference kbase implementation in the PanVK fork uses zero
     * here with the comment that it is "for maximum compatibility"
     * and that it also creates the kernel side context. The
     * BASE_CONTEXT_CSF_EVENT_THREAD flag exists in the headers and
     * is listed as allowed, but passing it causes the kernel to
     * reject the ioctl with EINVAL on UAPI 1.20.
     *
     * Zero is accepted by every version we have tested and is the
     * safest choice.
     */
    flags.create_flags = 0;

    int rc = manvil_kbase_ioctl(dev->kbase, MANVIL_KBASE_IOCTL_SET_FLAGS,
                                &flags, "SET_FLAGS");
    if (rc < 0) {
        device_set_error(dev, "SET_FLAGS failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

/*
 * Map the memory tracking page.
 *
 * The tracking page is a single page mapped into the process address
 * space at a fixed offset. GPU virtual addresses returned by MEM_ALLOC
 * and similar ioctls are cookies that are relative to this page. The
 * mapping uses prot = 0 because the contents are not accessed
 * directly by userspace; the mapping only reserves the address range
 * that the kernel uses when interpreting cookies.
 */
static int device_map_tracking_page(manvil_device *dev)
{
    /*
     * The size is a single page. The offset is the special handle
     * value MANVIL_BASE_MEM_MAP_TRACKING_HANDLE, which is 3 << 12.
     */
    const size_t size = 0x1000;
    void *addr = mmap(NULL, size, 0, MAP_SHARED,
                      manvil_kbase_fd(dev->kbase),
                      (off_t)MANVIL_BASE_MEM_MAP_TRACKING_HANDLE);
    if (addr == MAP_FAILED) {
        device_set_error(dev, "mmap tracking page failed: %s",
                         strerror(errno));
        return -1;
    }

    dev->tracking_page = addr;
    dev->tracking_page_size = size;
    return 0;
}

/*
 * Initialize the memory zones that later allocations depend on.
 *
 * Two zones must be set up before any allocation that uses them:
 *
 *   EXEC_VA          used by executable allocations. Initialized by
 *                    MEM_EXEC_INIT with a size in pages. Manvil asks
 *                    for a modest range; the kernel may round it up.
 *
 *   CUSTOM_VA        used by the tiler heap context allocator. It is
 *                    not created automatically. The kernel sets it up
 *                    when the JIT allocator is initialized through
 *                    MEM_JIT_INIT. Without that call, any allocation
 *                    that lands in CUSTOM_VA fails with ENOMEM, which
 *                    is what happens to CS_TILER_HEAP_INIT because
 *                    the heap context lives there.
 *
 * Both calls are mandatory for a full featured device. The sizes
 * chosen here are deliberately generous but small enough to be safe
 * on any supported hardware.
 */
static int device_init_memory_zones(manvil_device *dev)
{
    /*
     * EXEC_VA zone. 4096 pages is 16 MiB, which is more than enough
     * for shader code and the small fixed allocations that go with
     * it.
     */
    struct manvil_kbase_ioctl_mem_exec_init exec;
    memset(&exec, 0, sizeof(exec));
    exec.va_pages = 4096;

    int rc = manvil_kbase_ioctl(dev->kbase,
                                MANVIL_KBASE_IOCTL_MEM_EXEC_INIT,
                                &exec,
                                "MEM_EXEC_INIT");
    if (rc < 0) {
        device_set_error(dev, "MEM_EXEC_INIT failed: %s", strerror(errno));
        return -1;
    }

    /*
     * CUSTOM_VA zone, initialized through the JIT allocator. 4096
     * pages is 16 MiB of reserved VA. The JIT allocator does not
     * commit physical pages up front; they are populated on demand.
     */
    struct manvil_kbase_ioctl_mem_jit_init jit;
    memset(&jit, 0, sizeof(jit));
    jit.va_pages        = 4096;
    jit.max_allocations = 32;
    jit.trim_level      = 0;
    jit.group_id        = 0;
    jit.phys_pages      = 4096;

    rc = manvil_kbase_ioctl(dev->kbase,
                            MANVIL_KBASE_IOCTL_MEM_JIT_INIT,
                            &jit,
                            "MEM_JIT_INIT");
    if (rc < 0) {
        device_set_error(dev, "MEM_JIT_INIT failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

/*
 * Read the size of the GPU properties blob.
 *
 * The first call to GET_GPUPROPS uses size = 0 and buffer = 0. The
 * kernel returns the number of bytes it would need to store all
 * properties.
 */
static int device_get_gpuprops_size(manvil_device *dev, uint32_t *out_size)
{
    struct manvil_kbase_ioctl_get_gpuprops args;
    memset(&args, 0, sizeof(args));

    args.buffer = 0;
    args.size = 0;
    args.flags = 0;

    /*
     * The kernel returns the required buffer size as the ioctl
     * return value, not by writing into args.size. A non-negative
     * return value is the size in bytes.
     */
    int rc = manvil_kbase_ioctl(dev->kbase, MANVIL_KBASE_IOCTL_GET_GPUPROPS,
                                &args, "GET_GPUPROPS(size)");
    if (rc < 0) {
        device_set_error(dev, "GET_GPUPROPS size query failed: %s",
                         strerror(errno));
        return -1;
    }

    if (rc == 0) {
        device_set_error(dev, "GET_GPUPROPS reported a zero size");
        return -1;
    }

    *out_size = (uint32_t)rc;
    return 0;
}

/*
 * Read the GPU properties blob.
 *
 * The buffer is a sequence of key/value pairs. Each key is a u32; the
 * low two bits of the key indicate the size of the following value:
 * 00 = u8, 01 = u16, 10 = u32, 11 = u64. Values are tightly packed and
 * little-endian.
 *
 * This function walks the buffer and extracts the properties that
 * Manvil cares about. Unknown keys are skipped using their declared
 * size.
 */
static int device_parse_gpuprops(manvil_device *dev,
                                 const uint8_t *buf, size_t len)
{
    size_t pos = 0;
    bool seen_product_id = false;

    while (pos + sizeof(uint32_t) <= len) {
        uint32_t key;
        memcpy(&key, buf + pos, sizeof(key));
        pos += sizeof(key);

        uint32_t size_code = key & 0x3u;
        uint32_t prop_id = key >> 2;
        size_t value_size = (size_t)1 << size_code;

        if (pos + value_size > len) {
            device_set_error(dev, "GPU props buffer truncated");
            return -1;
        }

        uint64_t value = 0;
        memcpy(&value, buf + pos, value_size);
        pos += value_size;

        /*
         * The kernel reports the key as (id << 2) | size_code. The
         * numeric property identifiers are those defined in the
         * MANVIL_GPUPROP_* macros. Only a subset is handled here.
         */
        switch (prop_id) {
        case 1: /* PRODUCT_ID */
            dev->gpu_props.product_id = (uint32_t)value;
            seen_product_id = true;
            break;
        case 2: /* VERSION_STATUS */
            dev->gpu_props.version_status = (uint16_t)value;
            break;
        case 3: /* MINOR_REVISION */
            dev->gpu_props.minor_revision = (uint16_t)value;
            break;
        case 4: /* MAJOR_REVISION */
            dev->gpu_props.major_revision = (uint16_t)value;
            break;
        case 6: /* GPU_FREQ_KHZ_MAX */
            dev->gpu_props.gpu_freq_khz_max = (uint32_t)value;
            break;
        case 12: /* GPU_AVAILABLE_MEMORY_SIZE */
            dev->gpu_props.available_memory_bytes = value;
            break;
        case 25: /* RAW_SHADER_PRESENT */
            dev->gpu_props.shader_present = value;
            break;
        case 26: /* RAW_TILER_PRESENT */
            dev->gpu_props.tiler_present = value;
            break;
        case 27: /* RAW_L2_PRESENT */
            dev->gpu_props.l2_present = value;
            break;
        case 82: /* NUM_EXEC_ENGINES */
            dev->gpu_props.num_exec_engines = (uint32_t)value;
            break;
        default:
            /* Ignore unknown properties. */
            break;
        }
    }

    if (!seen_product_id) {
        device_set_error(dev, "GPU props did not include a product id");
        return -1;
    }

    return 0;
}

/*
 * Read and parse the GPU properties.
 *
 * Performs the two GET_GPUPROPS calls: first for size, then for the
 * data. Allocates a temporary buffer of the required size and hands
 * it to the parser.
 */
static int device_read_gpu_props(manvil_device *dev)
{
    uint32_t size = 0;
    if (device_get_gpuprops_size(dev, &size) < 0) {
        return -1;
    }

    if (size == 0) {
        device_set_error(dev, "GET_GPUPROPS reported a zero size");
        return -1;
    }

    /*
     * Cap the size to a sane upper bound. The buffer is a few
     * kilobytes on real hardware. A very large value indicates a
     * driver that is not behaving as expected.
     */
    if (size > 64u * 1024u) {
        device_set_error(dev, "GET_GPUPROPS size %u is too large", size);
        return -1;
    }

    uint8_t *buf = calloc(1, size);
    if (buf == NULL) {
        device_set_error(dev, "out of memory for GPU props");
        return -1;
    }

    struct manvil_kbase_ioctl_get_gpuprops args;
    memset(&args, 0, sizeof(args));
    args.buffer = (uint64_t)(uintptr_t)buf;
    args.size = size;
    args.flags = 0;

    int rc = manvil_kbase_ioctl(dev->kbase, MANVIL_KBASE_IOCTL_GET_GPUPROPS,
                                &args, "GET_GPUPROPS(data)");
    if (rc < 0) {
        device_set_error(dev, "GET_GPUPROPS data query failed: %s",
                         strerror(errno));
        free(buf);
        return -1;
    }

    rc = device_parse_gpuprops(dev, buf, args.size);
    free(buf);
    return rc;
}

/*
 * Read the CSF global interface.
 *
 * The CS_GET_GLB_IFACE ioctl takes an input with pointers to arrays
 * where the kernel writes per-group and per-stream capability
 * structures. Manvil allocates room for the maximum number of groups
 * and streams, then stores the summary values.
 *
 * The per-group and per-stream arrays are not retained at this point.
 * When Manvil needs them, a second call can be made.
 */
static int device_read_csf_iface(manvil_device *dev)
{
    /*
     * Query with zero capacity to learn the number of groups and
     * streams. The kernel still fills the summary fields in this
     * case.
     */
    union manvil_kbase_ioctl_cs_get_glb_iface args;
    memset(&args, 0, sizeof(args));

    args.in.max_group_num = 0;
    args.in.max_total_stream_num = 0;
    args.in.groups_ptr = 0;
    args.in.streams_ptr = 0;

    int rc = manvil_kbase_ioctl(dev->kbase,
                                MANVIL_KBASE_IOCTL_CS_GET_GLB_IFACE,
                                &args, "CS_GET_GLB_IFACE(size)");
    if (rc < 0) {
        device_set_error(dev, "CS_GET_GLB_IFACE size query failed: %s",
                         strerror(errno));
        return -1;
    }

    dev->csf_iface.glb_version = args.out.glb_version;
    dev->csf_iface.features = args.out.features;
    dev->csf_iface.group_num = args.out.group_num;
    dev->csf_iface.total_stream_num = args.out.total_stream_num;
    dev->csf_iface.prfcnt_size = args.out.prfcnt_size;
    dev->csf_iface.instr_features = args.out.instr_features;

    if (dev->csf_iface.group_num == 0) {
        device_set_error(dev, "CS_GET_GLB_IFACE reported zero groups");
        return -1;
    }

    /*
     * Second pass: retrieve the per stream capability structures.
     *
     * The features field of the first stream carries the total
     * number of registers in the CS register file (bits 0-7, minus
     * one) and the number of scoreboards (bits 8-15, minus one).
     * Those values determine where in the register file the
     * application-owned registers begin. Hardcoding them would
     * break on any GPU revision with a different register file
     * size.
     *
     * We ask the kernel for a single stream. That is enough to learn
     * the register file size, which is a property of the CS
     * interface rather than of individual streams.
     */
    struct manvil_basep_cs_stream_control stream_data;
    memset(&stream_data, 0, sizeof(stream_data));

    union manvil_kbase_ioctl_cs_get_glb_iface stream_args;
    memset(&stream_args, 0, sizeof(stream_args));

    stream_args.in.max_group_num = 0;
    stream_args.in.max_total_stream_num = 1;
    stream_args.in.groups_ptr = 0;
    stream_args.in.streams_ptr =
        (uint64_t)(uintptr_t)&stream_data;

    rc = manvil_kbase_ioctl(dev->kbase,
                            MANVIL_KBASE_IOCTL_CS_GET_GLB_IFACE,
                            &stream_args, "CS_GET_GLB_IFACE(stream)");
    if (rc < 0) {
        device_set_error(dev, "CS_GET_GLB_IFACE stream query failed: %s",
                         strerror(errno));
        return -1;
    }

    /*
     * Decode the feature field.
     *
     * The register file size and the scoreboard count are encoded as
     * "value - 1". A value of zero in the field means one register
     * or one scoreboard.
     */
    uint32_t stream_features = stream_data.features;
    uint32_t work_regs = (stream_features & 0xFFu) + 1u;
    uint32_t scoreboards = ((stream_features >> 8) & 0xFFu) + 1u;

    /*
     * The top four registers are reserved for the application. The
     * register file may be as small as four registers in theory;
     * guard against an underflow that would make the base wrap.
     */
    uint32_t user_base = (work_regs >= 4u) ? (work_regs - 4u) : 0u;

    dev->csf_iface.work_registers = work_regs;
    dev->csf_iface.scoreboards = scoreboards;
    dev->csf_iface.user_register_base = user_base;

    return 0;
}

/*
 * Public API.
 */

manvil_device *manvil_device_open(const char *path)
{
    if (path == NULL) {
        return NULL;
    }

    manvil_device *dev = calloc(1, sizeof(*dev));
    if (dev == NULL) {
        return NULL;
    }

    /*
     * Step 1: open the Kbase backend. This performs the UAPI version
     * check and stores the version and feature bitmask.
     */
    char errbuf[MANVIL_DEVICE_ERROR_MAX];
    errbuf[0] = '\0';

    dev->kbase = manvil_kbase_open(path, errbuf, sizeof(errbuf));
    if (dev->kbase == NULL) {
        device_set_error(dev, "%s", errbuf[0] ? errbuf
                         : "manvil_kbase_open failed");
        free(dev);
        return NULL;
    }

    /*
     * Take a snapshot of the feature bitmask for quick access.
     */
    dev->features = manvil_kbase_features(dev->kbase);

    /*
     * Step 2: set the context flags. This must happen before any
     * ioctl that expects a context to exist.
     */
    if (device_set_flags(dev) < 0) {
        manvil_device_close(dev);
        return NULL;
    }

    /*
     * Step 3: map the tracking page.
     */
    if (device_map_tracking_page(dev) < 0) {
        manvil_device_close(dev);
        return NULL;
    }

    /*
     * Step 4: initialize the memory zones that later allocations
     * depend on. EXEC_VA and CUSTOM_VA are both required. In
     * particular CUSTOM_VA is needed for the tiler heap context, so
     * this must happen before any heap is created.
     */
    if (device_init_memory_zones(dev) < 0) {
        manvil_device_close(dev);
        return NULL;
    }

    /*
     * Step 5: read the GPU properties.
     */
    if (device_read_gpu_props(dev) < 0) {
        manvil_device_close(dev);
        return NULL;
    }

    /*
     * Step 6: read the CSF global interface.
     */
    if (device_read_csf_iface(dev) < 0) {
        manvil_device_close(dev);
        return NULL;
    }

    if (manvil_device_has_feature(dev, 0)) {
        /*
         * Placeholder. Feature queries are always true for the zero
         * feature mask because of the mask semantics. This branch is
         * here to document that future versions may want to gate
         * additional setup on feature flags.
         */
    }

    return dev;
}

void manvil_device_close(manvil_device *dev)
{
    if (dev == NULL) {
        return;
    }

    if (dev->tracking_page != NULL) {
        munmap(dev->tracking_page, dev->tracking_page_size);
        dev->tracking_page = NULL;
        dev->tracking_page_size = 0;
    }

    if (dev->kbase != NULL) {
        manvil_kbase_close(dev->kbase);
        dev->kbase = NULL;
    }

    memset(dev, 0, sizeof(*dev));
    free(dev);
}

manvil_kbase *manvil_device_kbase(manvil_device *dev)
{
    return dev != NULL ? dev->kbase : NULL;
}

const struct manvil_gpu_props *manvil_device_gpu_props(manvil_device *dev)
{
    return dev != NULL ? &dev->gpu_props : NULL;
}

const struct manvil_csf_iface *manvil_device_csf_iface(manvil_device *dev)
{
    return dev != NULL ? &dev->csf_iface : NULL;
}

void *manvil_device_tracking_page(manvil_device *dev)
{
    return dev != NULL ? dev->tracking_page : NULL;
}

bool manvil_device_has_feature(manvil_device *dev, uint64_t feature)
{
    if (dev == NULL) {
        return false;
    }
    return (dev->features & feature) == feature;
}

const char *manvil_device_last_error(manvil_device *dev)
{
    if (dev == NULL) {
        return "";
    }
    return dev->last_error;
}

void manvil_device_clear_error(manvil_device *dev)
{
    device_clear_error_internal(dev);
}
