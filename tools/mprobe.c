/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * mprobe - Manvil probe tool.
 *
 * mprobe opens a Kbase device, exercises the core modules of
 * Manvil, and reports the results. It is intended as a
 * diagnostic, a portability check, and the first end to end
 * validation of the driver on a new device.
 *
 * The sequence exercised by mprobe is:
 *
 *   1. open the device and read its capabilities
 *   2. allocate a small GPU memory region
 *   3. create a compute group
 *   4. create a queue bound to that group
 *   5. create a sync object
 *   6. submit a two command batch: ENOP plus SYNC_SET64
 *   7. wait for the sync to reach the target value
 *   8. verify the same path through the scheduler entry point
 *   9. release everything in reverse order
 *
 * Any failure along the way is reported with the exact step, the
 * last error reported by the failing module, and any CSF
 * notification that the kernel delivered during the run.
 *
 * Usage:
 *   mprobe [options]
 *
 * Options:
 *   -d path    Device node to use. Default: /dev/mali0
 *   -t ms      Timeout in milliseconds for each sync wait. Default: 5000
 *   -h         Show this help and exit.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "device/device.h"
#include "mem/mem.h"
#include "csf/group.h"
#include "csf/queue.h"
#include "csf/event.h"
#include "sync/sync.h"
#include "sched/sched.h"
#include "cmd/cmd.h"
#include "heap/heap.h"

/* Diagnostic helper defined in heap.c */
int manvil_heap_probe(manvil_kbase *kbase);

/*
 * Global run configuration.
 */
struct mprobe_config {
    const char *device_path;
    int64_t     wait_timeout_ns;
    bool        verbose;
};

static struct mprobe_config g_cfg = {
    .device_path    = "/dev/mali0",
    .wait_timeout_ns = 5000ll * 1000000ll,  /* 5 seconds */
    .verbose        = false,
};

/*
 * State collected during a run, used for the final report.
 */
struct mprobe_state {
    manvil_device *dev;
    manvil_mem    *mem;
    manvil_group  *group;
    manvil_queue  *queue;
    manvil_sync   *sync;
    manvil_heap   *heap;

    int64_t started_ns;
};

/*
 * Return the current monotonic time in nanoseconds.
 */
static int64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000ll + (int64_t)ts.tv_nsec;
}

/*
 * Print a formatted line with two-space indentation.
 */
static void indent_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("    ");
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

/*
 * Print a step header.
 *
 * The step number is supplied by the caller. The description is
 * printed left aligned in a fixed width field so that the OK or
 * FAIL marker appears in a stable column.
 */
static void step_header(int step, const char *desc)
{
    printf("[%d] %-45s", step, desc);
    fflush(stdout);
}

static void step_ok(void)
{
    printf("OK\n");
}

static void step_fail(void)
{
    printf("FAIL\n");
}

/*
 * Drain any pending CSF notifications and print a summary line for
 * each one. Returns the number of notifications consumed.
 *
 * Called after each step so that the firmware side errors surface
 * with the step that triggered them.
 */
static int drain_notifications(manvil_device *dev)
{
    int count = 0;

    for (;;) {
        struct manvil_csf_event_info ev;
        int rc = manvil_csf_wait_notification(manvil_device_kbase(dev),
                                                &ev,
                                                MANVIL_CSF_POLL_NOWAIT);
        if (rc <= 0) {
            break;
        }

        count++;
        indent_printf("[csf] %s group=%u csi=%u status=0x%08x",
                      manvil_csf_event_kind_str(ev.kind),
                      (unsigned)ev.group_handle,
                      (unsigned)ev.csi_index,
                      (unsigned)ev.status);
    }

    return count;
}

/*
 * Report the last error recorded by the device, if any.
 */
static void report_device_error(manvil_device *dev)
{
    const char *err = manvil_device_last_error(dev);
    if (err != NULL && err[0] != '\0') {
        indent_printf("error: %s", err);
    }
}

/*
 * Format a product id as a Mali marketing name when known.
 *
 * The mapping is derived from the GPU_ID2 product model field in
 * the public UAPI headers and from observed hardware. Unknown
 * values are printed as hexadecimal by the caller.
 */
static const char *product_name(uint32_t product_id)
{
    switch (product_id) {
    case 0x0901: return "Mali-G615";
    case 0x0902: return "Mali-G715";
    case 0x0903: return "Mali-G515";
    case 0x0904: return "Mali-G310";
    case 0x0a01: return "Mali-G715";
    case 0x0a02: return "Mali-G615";
    default:     return NULL;
    }
}

/*
 * Print a formatted hex mask with leading zeros suppressed.
 */
static void indent_printf_hex64(const char *label, uint64_t value)
{
    indent_printf("%-20s0x%016" PRIx64, label, value);
}

/*
 * Command line parsing.
 */

static void print_usage(const char *prog)
{
    printf("Usage: %s [options]\n", prog);
    printf("\n");
    printf("Options:\n");
    printf("  -d path    Device node to use. Default: /dev/mali0\n");
    printf("  -t ms      Timeout in milliseconds for each sync wait.\n");
    printf("             Default: 5000\n");
    printf("  -v         Verbose output.\n");
    printf("  -h         Show this help and exit.\n");
}

static int parse_args(int argc, char **argv)
{
    int opt;
    while ((opt = getopt(argc, argv, "d:t:vh")) != -1) {
        switch (opt) {
        case 'd':
            g_cfg.device_path = optarg;
            break;
        case 't': {
            long ms = strtol(optarg, NULL, 10);
            if (ms <= 0) {
                fprintf(stderr, "mprobe: invalid timeout: %s\n", optarg);
                return -1;
            }
            g_cfg.wait_timeout_ns = (int64_t)ms * 1000000ll;
            break;
        }
        case 'v':
            g_cfg.verbose = true;
            break;
        case 'h':
            print_usage(argv[0]);
            exit(0);
        default:
            print_usage(argv[0]);
            return -1;
        }
    }
    return 0;
}

/*
 * Helper macro used by the run function below. It frees everything
 * allocated so far in the given state and returns a status code.
 */
static void cleanup_state(struct mprobe_state *st)
{
    if (st->heap != NULL) {
        manvil_heap_destroy(st->heap);
        st->heap = NULL;
    }
    if (st->sync != NULL) {
        manvil_sync_destroy(st->sync);
        st->sync = NULL;
    }
    if (st->queue != NULL) {
        manvil_queue_destroy(st->queue);
        st->queue = NULL;
    }
    if (st->group != NULL) {
        manvil_group_destroy(st->group);
        st->group = NULL;
    }
    if (st->mem != NULL) {
        manvil_mem_free(st->mem);
        st->mem = NULL;
    }
    if (st->dev != NULL) {
        manvil_device_close(st->dev);
        st->dev = NULL;
    }
}

/*
 * Step 1: open the device and report what the kernel says about it.
 *
 * Returns 0 on success, -1 on failure.
 */
static int step_open_device(struct mprobe_state *st)
{
    step_header(1, "Opening device");

    st->dev = manvil_device_open(g_cfg.device_path);
    if (st->dev == NULL) {
        step_fail();
        fprintf(stderr, "mprobe: cannot open %s\n", g_cfg.device_path);
        return -1;
    }
    step_ok();

    manvil_kbase *kbase = manvil_device_kbase(st->dev);

    indent_printf("UAPI version:       %u.%u",
                  (unsigned)manvil_kbase_uapi_major(kbase),
                  (unsigned)manvil_kbase_uapi_minor(kbase));
    indent_printf_hex64("Features:", manvil_kbase_features(kbase));

    const struct manvil_gpu_props *gpu = manvil_device_gpu_props(st->dev);
    if (gpu != NULL) {
        const char *name = product_name(gpu->product_id);
        if (name != NULL) {
            indent_printf("Product ID:         0x%04" PRIx32 "  (%s)",
                          gpu->product_id, name);
        } else {
            indent_printf("Product ID:         0x%04" PRIx32 "  (unknown)",
                          gpu->product_id);
        }
        indent_printf("Revision:           %u.%u.%u",
                      (unsigned)gpu->major_revision,
                      (unsigned)gpu->minor_revision,
                      (unsigned)gpu->version_status);
        indent_printf("GPU freq max:       %u kHz", gpu->gpu_freq_khz_max);
        indent_printf("Available memory:   %" PRIu64 " bytes",
                      gpu->available_memory_bytes);
        indent_printf("Exec engines:       %u", gpu->num_exec_engines);
        indent_printf_hex64("Shader present:", gpu->shader_present);
        indent_printf_hex64("Tiler present:", gpu->tiler_present);
        indent_printf_hex64("L2 present:", gpu->l2_present);
    }

    const struct manvil_csf_iface *csf = manvil_device_csf_iface(st->dev);
    if (csf != NULL) {
        uint32_t major = (csf->glb_version >> 24) & 0xff;
        uint32_t minor = (csf->glb_version >> 16) & 0xff;
        uint32_t patch = (csf->glb_version) & 0xffff;
        indent_printf("CSF version:        %u.%u.%u",
                      major, minor, patch);
        indent_printf("CSF groups:         %u", csf->group_num);
        indent_printf("CSF total streams:  %u", csf->total_stream_num);
        indent_printf("CSF work registers: %u", csf->work_registers);
        indent_printf("CSF scoreboards:    %u", csf->scoreboards);
        indent_printf("CSF user reg base:  %u",
                      csf->user_register_base);
        indent_printf_hex64("CSF features:", csf->features);
    }

    drain_notifications(st->dev);
    return 0;
}

/*
 * Step 2: allocate a small GPU memory region.
 */
static int step_alloc_memory(struct mprobe_state *st)
{
    const uint64_t size = 4096;

    step_header(2, "Allocating 4 KiB of GPU memory");

    st->mem = manvil_mem_alloc_rw(manvil_device_kbase(st->dev), size);
    if (st->mem == NULL) {
        step_fail();
        report_device_error(st->dev);
        return -1;
    }
    step_ok();

    indent_printf_hex64("GPU VA:", manvil_mem_gpu_va(st->mem));
    indent_printf("%-20s%p", "CPU ptr:", manvil_mem_cpu_ptr(st->mem));
    indent_printf("Size:               %" PRIu64,
                  manvil_mem_size(st->mem));

    /*
     * Write a small pattern to verify that the mapping is usable
     * from the CPU side.
     */
    volatile uint32_t *p = (volatile uint32_t *)manvil_mem_cpu_ptr(st->mem);
    p[0] = 0xDEADBEEF;
    if (p[0] != 0xDEADBEEF) {
        step_fail();
        indent_printf("write-back mismatch");
        return -1;
    }
    indent_printf("CPU write-back:     OK");

    drain_notifications(st->dev);
    return 0;
}

/*
 * Step 3: create a compute group.
 */
static int step_create_group(struct mprobe_state *st)
{
    step_header(3, "Creating compute group");

    st->group = manvil_group_create_compute(manvil_device_kbase(st->dev));
    if (st->group == NULL) {
        step_fail();
        report_device_error(st->dev);
        return -1;
    }
    step_ok();

    indent_printf("Group handle:       %u",
                  (unsigned)manvil_group_handle(st->group));
    indent_printf("Group UID:          %u",
                  (unsigned)manvil_group_uid(st->group));

    drain_notifications(st->dev);
    return 0;
}

/*
 * Step 4: create a queue bound to the group.
 */
static int step_create_queue(struct mprobe_state *st)
{
    step_header(4, "Creating queue bound to group");

    st->queue = manvil_queue_create(manvil_device_kbase(st->dev),
                                     st->group,
                                     0,   /* default ring size */
                                     0);  /* CSI 0 */
    if (st->queue == NULL) {
        step_fail();
        report_device_error(st->dev);
        return -1;
    }
    step_ok();

    indent_printf("Ring size:          %u bytes",
                  manvil_queue_ring_size(st->queue));
    indent_printf("Ring GPU VA:        0x%016" PRIx64,
                  manvil_queue_ring_gpu_va(st->queue));
    indent_printf("CSI index:          %u",
                  (unsigned)manvil_queue_csi_index(st->queue));

    drain_notifications(st->dev);
    return 0;
}

/*
 * Step 5: create a sync object.
 *
 * The sync object is intra-group, which is sufficient for this
 * test because the signal comes from the same queue that does the
 * work.
 */
static int step_create_sync(struct mprobe_state *st)
{
    step_header(5, "Creating intra-group sync object");

    st->sync = manvil_sync_create(manvil_device_kbase(st->dev), false);
    if (st->sync == NULL) {
        step_fail();
        report_device_error(st->dev);
        return -1;
    }
    step_ok();

    indent_printf_hex64("Sync GPU VA:", manvil_sync_gpu_va(st->sync));
    indent_printf("Initial value:      %" PRIu64,
                  manvil_sync_value(st->sync));

    drain_notifications(st->dev);
    return 0;
}

/*
 * Step 5b: create a tiler heap and use it in a submission.
 *
 * The heap is created with the default parameters. A submission is
 * then issued that first loads the heap context address into a
 * user register and then emits HEAP_SET through that register. The
 * sync is signaled at the end so we can verify the firmware
 * processed the whole batch without errors.
 */
static int step_heap_create(struct mprobe_state *st)
{
    step_header(6, "Creating tiler heap");

    struct manvil_heap_desc desc;
    manvil_heap_desc_default(&desc);

    st->heap = manvil_heap_create(manvil_device_kbase(st->dev), &desc);
    if (st->heap == NULL) {
        int first_errno = errno;
        step_fail();
        indent_printf("default config failed: errno=%d (%s)",
                      first_errno, strerror(first_errno));
        indent_printf("running heap_probe to find a working configuration...");

        /*
         * The probe tries a series of configurations and stops on the
         * first that succeeds. It leaves the created heap intact only
         * if the caller has not already created one; the probe
         * destroys what it creates internally and returns 0 on
         * success. We then try the default again with a smaller
         * config if the probe reports success.
         */
        if (manvil_heap_probe(manvil_device_kbase(st->dev)) < 0) {
            indent_printf("heap_probe: no configuration worked");
            report_device_error(st->dev);
            return -1;
        }

        /*
         * A configuration worked. Recreate the heap with a small
         * safe configuration so subsequent steps can use it. The
         * probe already confirmed that 4 KiB chunks with max 4 work
         * on this hardware, but that is specific to the target.
         * Use the probe's fallback values.
         */
        manvil_heap_desc_default(&desc);
        desc.chunk_size       = 4u * 1024u;
        desc.initial_chunks   = 1u;
        desc.max_chunks       = 4u;
        desc.target_in_flight = 1u;

        st->heap = manvil_heap_create(manvil_device_kbase(st->dev), &desc);
        if (st->heap == NULL) {
            indent_printf("re-create with fallback failed");
            return -1;
        }

        indent_printf("recovered with fallback: chunk=4 KiB max=4 in_flight=1");
    }
    step_ok();

    const struct manvil_heap_desc *used = manvil_heap_desc_of(st->heap);
    if (used != NULL) {
        indent_printf("Chunk size:         %u bytes", used->chunk_size);
        indent_printf("Initial chunks:     %u", used->initial_chunks);
        indent_printf("Max chunks:         %u", used->max_chunks);
        indent_printf("Target in flight:   %u", used->target_in_flight);
    }
    indent_printf_hex64("Heap context VA:", manvil_heap_gpu_va(st->heap));
    indent_printf_hex64("First chunk VA:", manvil_heap_first_chunk_va(st->heap));

    drain_notifications(st->dev);
    return 0;
}

/*
 * Step 7: use the tiler heap in a submission.
 *
 * Emits the following sequence:
 *   1. MOVE48 -> user register: heap context address
 *   2. HEAP_SET through that register
 *   3. HEAP_OPERATION (VERTEX_TILER_STARTED)
 *   4. SYNC_SET64 to signal the sync (appended by the scheduler)
 *
 * The purpose is to verify that the firmware accepts HEAP_SET and
 * HEAP_OPERATION with a valid heap context, and that the batch
 * retires cleanly. The sync is reset to zero before the submission
 * so the value observed afterwards is from this batch only.
 */
static int step_heap_submit(struct mprobe_state *st)
{
    step_header(7, "Submitting HEAP_SET + HEAP_OPERATION");

    const struct manvil_csf_iface *csf = manvil_device_csf_iface(st->dev);
    if (csf == NULL) {
        step_fail();
        indent_printf("no CSF interface");
        return -1;
    }

    uint8_t heap_reg = (uint8_t)(csf->user_register_base + 0);

    manvil_cmd cmds[3];

    /* 1. Load the heap context address into the register. */
    manvil_cmd_move48(cmds[0], heap_reg, manvil_heap_gpu_va(st->heap));

    /* 2. HEAP_SET through the register. */
    manvil_cmd_heap_set(cmds[1], heap_reg);

    /* 3. HEAP_OPERATION: VERTEX_TILER_STARTED. */
    manvil_cmd_heap_operation(cmds[2],
                               MANVIL_CS_HEAP_OP_VERTEX_TILER_STARTED,
                               0,                            /* wait_mask */
                               0,                            /* signal_slot */
                               MANVIL_CS_DEFER_IMMEDIATE);

    /*
     * Reset the sync so we observe the signal from this batch.
     */
    void *sync_ptr = manvil_mem_cpu_ptr(manvil_sync_mem(st->sync));
    *(volatile uint64_t *)sync_ptr = 0;

    struct manvil_submit_desc desc;
    memset(&desc, 0, sizeof(desc));

    manvil_sync *signals[1] = { st->sync };
    uint64_t      signal_values[1] = { 1 };

    desc.cmds             = (const manvil_cmd *)cmds;
    desc.num_cmds         = 3;
    desc.signal_syncs     = signals;
    desc.signal_values    = signal_values;
    desc.num_signals      = 1;
    desc.signal_addr_reg  = (uint8_t)(csf->user_register_base + 0);
    desc.signal_data_reg  = (uint8_t)(csf->user_register_base + 2);

    int rc = manvil_sched_submit(st->queue, &desc);
    if (rc < 0) {
        step_fail();
        indent_printf("manvil_sched_submit: %s", strerror(errno));
        report_device_error(st->dev);
        return -1;
    }

    rc = manvil_sync_wait_cpu(st->sync, 1, g_cfg.wait_timeout_ns);
    if (rc < 0) {
        step_fail();
        indent_printf("manvil_sync_wait_cpu: %s", strerror(errno));
        return -1;
    }
    if (rc > 0) {
        step_fail();
        indent_printf("timeout waiting for heap submission");
        drain_notifications(st->dev);
        return -1;
    }
    step_ok();

    indent_printf_hex64("Heap VA:", manvil_heap_gpu_va(st->heap));
    indent_printf("Heap register:      %u", (unsigned)heap_reg);
    indent_printf("Sync value:         %" PRIu64,
                  manvil_sync_value(st->sync));

    drain_notifications(st->dev);
    return 0;
}

/*
 * Step 6: submit a two command batch through the scheduler, with
 * the sync set at the end.
 *
 * This exercises the full path: command building, ring write,
 * signal emission, and kick.
 */
static int step_submit(struct mprobe_state *st)
{
    step_header(8, "Submitting ENOP + signal");

    manvil_cmd cmds[1];
    manvil_cmd_enop(cmds[0], 0xABC123);

    struct manvil_submit_desc desc;
    memset(&desc, 0, sizeof(desc));

    manvil_sync *signals[1] = { st->sync };
    uint64_t      signal_values[1] = { 2 };

    /*
     * Use the top four registers of the CS register file for the
     * signal. The base is reported by the firmware at open time and
     * stored in the device interface struct. The first two
     * registers hold the 64 bit address of the sync object, the
     * next two hold the 64 bit value to write.
     */
    const struct manvil_csf_iface *csf = manvil_device_csf_iface(st->dev);
    uint8_t addr_reg = 0;
    uint8_t data_reg = 0;
    if (csf != NULL) {
        addr_reg = (uint8_t)(csf->user_register_base + 0);
        data_reg = (uint8_t)(csf->user_register_base + 2);
    }

    desc.cmds             = (const manvil_cmd *)cmds;
    desc.num_cmds         = 1;
    desc.signal_syncs     = signals;
    desc.signal_values    = signal_values;
    desc.num_signals      = 1;
    desc.signal_addr_reg  = addr_reg;
    desc.signal_data_reg  = data_reg;

    int rc = manvil_sched_submit(st->queue, &desc);
    if (rc < 0) {
        step_fail();
        indent_printf("manvil_sched_submit: %s", strerror(errno));
        report_device_error(st->dev);
        return -1;
    }
    step_ok();

    indent_printf("Commands written:   %zu", desc.num_cmds + desc.num_signals);
    indent_printf("Signal target:      %" PRIu64, signal_values[0]);
    indent_printf("CS_INSERT:          %llu",
                  (unsigned long long)manvil_queue_cs_insert(st->queue));
    indent_printf("CS_EXTRACT:         %llu",
                  (unsigned long long)manvil_queue_cs_extract(st->queue));

    return 0;
}

/*
 * Step 7: wait for the sync to reach the target value.
 *
 * The sync is set by the firmware after the queue finishes
 * executing the commands from step 6. A successful return proves
 * that the firmware is alive and that the full path from userspace
 * through the ring, the firmware, and back through the sync object
 * works end to end.
 */
static int step_wait_sync(struct mprobe_state *st)
{
    step_header(9, "Waiting for sync to reach value 2");

    int64_t started = now_ns();
    int rc = manvil_sync_wait_cpu(st->sync, 2, g_cfg.wait_timeout_ns);
    int64_t elapsed = now_ns() - started;

    if (rc < 0) {
        step_fail();
        indent_printf("manvil_sync_wait_cpu: %s", strerror(errno));
        report_device_error(st->dev);
        return -1;
    }
    if (rc > 0) {
        step_fail();
        indent_printf("timeout after %" PRId64 " microseconds",
                      elapsed / 1000);
        indent_printf("sync value is still %" PRIu64,
                      manvil_sync_value(st->sync));
        drain_notifications(st->dev);
        return -1;
    }
    step_ok();

    indent_printf("Sync value:         %" PRIu64,
                  manvil_sync_value(st->sync));
    indent_printf("Elapsed:            %" PRId64 " microseconds",
                  elapsed / 1000);

    drain_notifications(st->dev);
    return 0;
}

/*
 * Step 8: run the same submit sequence once more, using a higher
 * target value, to verify that the sync object can be reused and
 * that the second submission after the first one also works.
 *
 * This exercises a subtle path: the sync value is observed by the
 * CPU after the first wait, but the firmware side counter is not
 * reset. The second submit sets it to 2 directly, and the CPU
 * observes the transition. This is the pattern that timeline
 * semaphores use.
 */
static int step_verify_second_submit(struct mprobe_state *st)
{
    step_header(10, "Second submit, sync target 3");

    manvil_cmd cmds[1];
    manvil_cmd_enop(cmds[0], 0xDEF456);

    struct manvil_submit_desc desc;
    memset(&desc, 0, sizeof(desc));

    manvil_sync *signals[1] = { st->sync };
    uint64_t      signal_values[1] = { 3 };

    const struct manvil_csf_iface *csf = manvil_device_csf_iface(st->dev);
    uint8_t addr_reg = 0;
    uint8_t data_reg = 0;
    if (csf != NULL) {
        addr_reg = (uint8_t)(csf->user_register_base + 0);
        data_reg = (uint8_t)(csf->user_register_base + 2);
    }

    desc.cmds             = (const manvil_cmd *)cmds;
    desc.num_cmds         = 1;
    desc.signal_syncs     = signals;
    desc.signal_values    = signal_values;
    desc.num_signals      = 1;
    desc.signal_addr_reg  = addr_reg;
    desc.signal_data_reg  = data_reg;

    int rc = manvil_sched_submit(st->queue, &desc);
    if (rc < 0) {
        step_fail();
        indent_printf("manvil_sched_submit: %s", strerror(errno));
        report_device_error(st->dev);
        return -1;
    }

    int64_t started = now_ns();
    rc = manvil_sync_wait_cpu(st->sync, 3, g_cfg.wait_timeout_ns);
    int64_t elapsed = now_ns() - started;

    if (rc < 0) {
        step_fail();
        indent_printf("manvil_sync_wait_cpu: %s", strerror(errno));
        return -1;
    }
    if (rc > 0) {
        step_fail();
        indent_printf("timeout after %" PRId64 " microseconds",
                      elapsed / 1000);
        return -1;
    }
    step_ok();

    indent_printf("Sync value:         %" PRIu64,
                  manvil_sync_value(st->sync));
    indent_printf("Elapsed:            %" PRId64 " microseconds",
                  elapsed / 1000);

    drain_notifications(st->dev);
    return 0;
}

/*
 * Step 9: verify that the queue is idle after the sync has been
 * observed. The firmware should have consumed all entries by now.
 */
static int step_check_idle(struct mprobe_state *st)
{
    step_header(11, "Checking queue idle");

    if (!manvil_queue_is_idle(st->queue)) {
        step_fail();
        indent_printf("CS_INSERT=%llu CS_EXTRACT=%llu",
                      (unsigned long long)manvil_queue_cs_insert(st->queue),
                      (unsigned long long)manvil_queue_cs_extract(st->queue));
        return -1;
    }
    step_ok();

    indent_printf("CS_INSERT:          %u",
                  manvil_queue_cs_insert(st->queue));
    indent_printf("CS_EXTRACT:         %u",
                  manvil_queue_cs_extract(st->queue));

    return 0;
}

/*
 * Step 10: release all resources.
 *
 * The order is the reverse of creation: sync, queue, group, memory,
 * device. Each destroy function tolerates a NULL argument, so the
 * sequence can be run even on a partially initialized state.
 */
static int step_cleanup(struct mprobe_state *st)
{
    step_header(12, "Cleanup");

    cleanup_state(st);
    step_ok();
    return 0;
}

/*
 * Main.
 */

int main(int argc, char **argv)
{
    printf("Manvil probe tool\n");
    printf("\n");
    printf("Building against UAPI %d\n", MANVIL_COMPILED_UAPI_VERSION);
    printf("Runtime: %s\n", g_cfg.device_path);
    printf("\n");

    if (parse_args(argc, argv) < 0) {
        return EXIT_FAILURE;
    }

    printf("Configuration:\n");
    indent_printf("device:             %s", g_cfg.device_path);
    indent_printf("wait timeout:       %" PRId64 " ms",
                  g_cfg.wait_timeout_ns / 1000000);
    indent_printf("verbose:            %s", g_cfg.verbose ? "yes" : "no");
    printf("\n");

    struct mprobe_state st;
    memset(&st, 0, sizeof(st));
    st.started_ns = now_ns();

    int result = 0;

    if (step_open_device(&st) < 0) { result = 1; goto done; }
    if (step_alloc_memory(&st) < 0) { result = 2; goto done; }
    if (step_create_group(&st) < 0) { result = 3; goto done; }
    if (step_create_queue(&st) < 0) { result = 4; goto done; }
    if (step_create_sync(&st) < 0) { result = 5; goto done; }
    if (step_heap_create(&st) < 0) { result = 6; goto done; }
    if (step_heap_submit(&st) < 0) { result = 7; goto done; }
    if (step_submit(&st) < 0) { result = 8; goto done; }
    if (step_wait_sync(&st) < 0) { result = 9; goto done; }
    if (step_verify_second_submit(&st) < 0) { result = 10; goto done; }
    if (step_check_idle(&st) < 0) { result = 11; goto done; }
    if (step_cleanup(&st) < 0) { result = 12; goto done; }

done:
    /*
     * If any step failed, still release whatever was allocated.
     * cleanup_state is idempotent.
     */
    cleanup_state(&st);

    int64_t elapsed = now_ns() - st.started_ns;

    printf("\n");
    if (result == 0) {
        printf("Result: SUCCESS\n");
    } else {
        printf("Result: FAIL at step %d\n", result);
    }
    printf("\n");
    printf("Total runtime: %" PRId64 " milliseconds\n", elapsed / 1000000);

    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
