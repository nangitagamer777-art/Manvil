/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * KCPU command queue implementation.
 */

#include "kcpu.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "kernel_api/abi/manvil_abi.h"

/*
 * Internal KCPU queue structure.
 *
 * The command buffer holds a flat array of base_kcpu_command
 * structures. Each command is 24 bytes and carries a type tag plus a
 * payload that fits in 16 bytes. This matches the layout expected by
 * the kernel.
 *
 * Some commands need additional per-command metadata that does not
 * fit in the 16 byte payload. For those, Manvil allocates a small
 * structure in a separate scratch area and stores a pointer to it in
 * the payload. This is the same technique used by the kernel itself
 * when it processes the batch.
 *
 * Manvil currently implements only commands whose payload fits in
 * the base_kcpu_command structure or that reference a value in GPU
 * memory. Commands that need kernel side structures (jit_alloc,
 * jit_free, group_suspend) will be added when the higher layers need
 * them.
 */
struct manvil_kcpu {
    manvil_kbase *kbase;

    /*
     * Identifier returned by KCPU_QUEUE_CREATE. Also the ioctl
     * identifier used by KCPU_QUEUE_ENQUEUE.
     */
    uint8_t kcpu_id;

    /*
     * Command buffer.
     *
     * The buffer is sized for MANVIL_KCPU_MAX_COMMANDS entries. It
     * is GPU memory because the kernel reads it through the enqueue
     * ioctl. The kernel copies the commands into its own queue
     * before processing them, so the buffer can be reused after each
     * successful submit.
     */
    manvil_mem *cmd_mem;
    void       *cmd_cpu_ptr;
    size_t      cmd_buffer_bytes;

    /*
     * Number of commands currently in the batch.
     */
    uint32_t batch_count;

    /*
     * Bump allocator offset inside the result buffer scratch region.
     * Reset whenever the KCPU queue is recreated. Not reset between
     * submits because the kernel may still be reading scratch
     * structures from a previous batch.
     */
    size_t scratch_used;

    /*
     * Shared result buffer.
     *
     * Some commands need to write a result to a location that the
     * kernel can access. Manvil provides one page by default and
     * lets callers use offsets within it. The buffer is not consumed
     * automatically; higher layers manage their own offsets.
     */
    manvil_mem *result_mem;

    /*
     * Validity flag.
     */
    bool is_valid;
};

/*
 * Total size of the command buffer in bytes.
 */
static size_t cmd_buffer_size(void)
{
    return (size_t)MANVIL_KCPU_MAX_COMMANDS
         * sizeof(struct manvil_base_kcpu_command);
}

/*
 * Create the KCPU queue with the kernel.
 *
 * The kernel returns a small identifier. The identifier is used for
 * every subsequent enqueue operation.
 */
static int kcpu_do_create(manvil_kcpu *kcpu)
{
    struct manvil_kbase_ioctl_kcpu_queue_new args;
    memset(&args, 0, sizeof(args));

    int rc = manvil_kbase_ioctl(kcpu->kbase,
                                MANVIL_KBASE_IOCTL_KCPU_QUEUE_CREATE,
                                &args,
                                "KCPU_QUEUE_CREATE");
    if (rc < 0) {
        return -1;
    }

    kcpu->kcpu_id = args.id;
    return 0;
}

/*
 * Delete the KCPU queue from the kernel.
 *
 * Called on destroy and on any failure path after a successful
 * create. Safe to call when the queue was never created; the check
 * is done by the caller.
 */
static void kcpu_do_delete(manvil_kcpu *kcpu)
{
    struct manvil_kbase_ioctl_kcpu_queue_delete args;
    memset(&args, 0, sizeof(args));
    args.id = kcpu->kcpu_id;

    (void)manvil_kbase_ioctl(kcpu->kbase,
                             MANVIL_KBASE_IOCTL_KCPU_QUEUE_DELETE,
                             &args,
                             "KCPU_QUEUE_DELETE");
}

manvil_kcpu *manvil_kcpu_create(manvil_kbase *kbase)
{
    if (kbase == NULL) {
        errno = EINVAL;
        return NULL;
    }

    manvil_kcpu *kcpu = calloc(1, sizeof(*kcpu));
    if (kcpu == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    kcpu->kbase = kbase;

    /*
     * Create the KCPU queue with the kernel first, so that a failure
     * here does not leave partially allocated buffers behind.
     */
    if (kcpu_do_create(kcpu) < 0) {
        free(kcpu);
        return NULL;
    }

    /*
     * Allocate the command buffer.
     */
    kcpu->cmd_buffer_bytes = cmd_buffer_size();
    kcpu->cmd_mem = manvil_mem_alloc(kbase, kcpu->cmd_buffer_bytes,
                                     MANVIL_MEM_FLAGS_RW);
    if (kcpu->cmd_mem == NULL) {
        kcpu_do_delete(kcpu);
        free(kcpu);
        return NULL;
    }

    kcpu->cmd_cpu_ptr = manvil_mem_cpu_ptr(kcpu->cmd_mem);
    memset(kcpu->cmd_cpu_ptr, 0, kcpu->cmd_buffer_bytes);

    /*
     * Allocate the shared result buffer.
     */
    size_t result_bytes = (size_t)MANVIL_KCPU_DEFAULT_RESULT_PAGES * 0x1000u;
    kcpu->result_mem = manvil_mem_alloc(kbase, result_bytes,
                                        MANVIL_MEM_FLAGS_RW);
    if (kcpu->result_mem == NULL) {
        manvil_mem_free(kcpu->cmd_mem);
        kcpu_do_delete(kcpu);
        free(kcpu);
        return NULL;
    }

    memset(manvil_mem_cpu_ptr(kcpu->result_mem), 0, result_bytes);

    kcpu->batch_count  = 0;
    kcpu->scratch_used = 0;
    kcpu->is_valid     = true;
    return kcpu;
}

void manvil_kcpu_destroy(manvil_kcpu *kcpu)
{
    if (kcpu == NULL) {
        return;
    }

    bool was_valid = kcpu->is_valid;
    kcpu->is_valid = false;

    if (was_valid) {
        /*
         * The kernel does not need a flush of pending commands
         * before delete. Commands already enqueued will complete or
         * be discarded according to kernel policy. Manvil just
         * releases the queue.
         */
        kcpu_do_delete(kcpu);
    }

    if (kcpu->result_mem != NULL) {
        manvil_mem_free(kcpu->result_mem);
        kcpu->result_mem = NULL;
    }

    if (kcpu->cmd_mem != NULL) {
        manvil_mem_free(kcpu->cmd_mem);
        kcpu->cmd_mem = NULL;
        kcpu->cmd_cpu_ptr = NULL;
    }

    memset(kcpu, 0, sizeof(*kcpu));
    free(kcpu);
}

void manvil_kcpu_reset_batch(manvil_kcpu *kcpu)
{
    if (kcpu == NULL || !kcpu->is_valid) {
        return;
    }

    /*
     * Do not clear the command buffer. The kernel has already copied
     * the previous batch, and the next batch will overwrite the
     * entries it uses. Only the count matters.
     */
    kcpu->batch_count = 0;
}

uint32_t manvil_kcpu_batch_count(const manvil_kcpu *kcpu)
{
    return kcpu != NULL ? kcpu->batch_count : 0;
}

uint8_t manvil_kcpu_id(const manvil_kcpu *kcpu)
{
    return kcpu != NULL ? kcpu->kcpu_id : 0;
}

bool manvil_kcpu_is_valid(const manvil_kcpu *kcpu)
{
    return kcpu != NULL && kcpu->is_valid;
}

/*
 * Return a pointer to the next free command slot, and advance the
 * batch count.
 *
 * Returns NULL if the batch is full or the queue is invalid.
 */
static struct manvil_base_kcpu_command *
kcpu_next_command(manvil_kcpu *kcpu)
{
    if (kcpu == NULL || !kcpu->is_valid) {
        errno = EINVAL;
        return NULL;
    }

    if (kcpu->batch_count >= MANVIL_KCPU_MAX_COMMANDS) {
        errno = ENOSPC;
        return NULL;
    }

    struct manvil_base_kcpu_command *cmds =
        (struct manvil_base_kcpu_command *)kcpu->cmd_cpu_ptr;
    struct manvil_base_kcpu_command *slot = &cmds[kcpu->batch_count];

    memset(slot, 0, sizeof(*slot));
    kcpu->batch_count++;

    return slot;
}

/*
 * Internal helper: allocate a scratch structure inside the result
 * buffer and return its offset in bytes.
 *
 * The result buffer is used for two purposes:
 *   - values that the kernel writes back (fence signal results, etc.)
 *   - scratch structures that need to live in GPU memory so that the
 *     kernel can read them by virtual address
 *
 * Both purposes share the same buffer. Manvil does not attempt to
 * sub-allocate within it; higher layers are expected to use distinct
 * offsets. The kernel copies the commands before returning, so the
 * scratch data must remain valid until the corresponding command has
 * been processed. For this reason the scratch area is currently
 * placed at fixed offsets from the start of the buffer and reserved
 * for the commands that Manvil uses.
 *
 * For CQS commands the payload is small enough to reference a value
 * inside the result buffer directly. Manvil reserves the first part
 * of the result buffer for command scratch data and the second part
 * for user results. The split is:
 *
 *   [ scratch region ]  MANVIL_KCPU_SCRATCH_BYTES
 *   [ user region    ]  rest of the buffer
 */

/*
 * Size of the scratch region inside the result buffer.
 *
 * This is enough for a small number of scratch structures used by
 * the commands implemented so far. It will grow as new command
 * types are added.
 */
#define MANVIL_KCPU_SCRATCH_BYTES 4096u

/*
 * Return the CPU pointer to the scratch region.
 */
static void *kcpu_scratch_cpu(manvil_kcpu *kcpu)
{
    return manvil_mem_cpu_ptr(kcpu->result_mem);
}

/*
 * Return the GPU address of the scratch region.
 */
static uint64_t kcpu_scratch_gpu(manvil_kcpu *kcpu)
{
    return manvil_mem_gpu_va(kcpu->result_mem);
}

/*
 * Copy a CQS object descriptor into the scratch region and return
 * its GPU address.
 *
 * The kernel expects a pointer to a struct base_cqs_wait_info or
 * struct base_cqs_set inside the command payload. The structure must
 * be readable from the kernel address space, which means it must be
 * in GPU memory that is mapped for the kernel.
 *
 * The layout of the scratch region is sequential. Callers append
 * their structures and receive the offset that was used. There is no
 * freeing; the buffer is reset whenever the whole KCPU queue is
 * recreated.
 */
struct kcpu_scratch_alloc {
    size_t offset;
    bool   valid;
};

static struct kcpu_scratch_alloc kcpu_scratch_offset(manvil_kcpu *kcpu,
                                                      size_t size)
{
    struct kcpu_scratch_alloc result = { 0, false };

    /*
     * Simple bump allocator over the scratch region. The offset lives
     * in the kcpu structure so that each queue has its own
     * independent allocator. The region is not reset between
     * submits, because the kernel may still hold references to
     * scratch structures from a previous batch until they are
     * processed.
     *
     * Higher layers submit at most a few dozen commands per batch,
     * so the fixed region size is more than enough for the workloads
     * Manvil expects.
     */
    if (size == 0 || kcpu->scratch_used + size > MANVIL_KCPU_SCRATCH_BYTES) {
        return result;
    }

    result.offset = kcpu->scratch_used;
    result.valid  = true;
    kcpu->scratch_used += size;
    return result;
}

/*
 * Add commands to the batch.
 */

int manvil_kcpu_add_cqs_set(manvil_kcpu *kcpu,
                             uint64_t cqs_gpu_va,
                             uint32_t value)
{
    struct manvil_base_kcpu_command *cmd = kcpu_next_command(kcpu);
    if (cmd == NULL) {
        return -1;
    }

    /*
     * Allocate scratch space for the base_cqs_set descriptor.
     */
    struct kcpu_scratch_alloc alloc =
        kcpu_scratch_offset(kcpu, sizeof(struct manvil_base_cqs_set));
    if (!alloc.valid) {
        kcpu->batch_count--;
        errno = ENOMEM;
        return -1;
    }

    struct manvil_base_cqs_set *obj =
        (struct manvil_base_cqs_set *)
        ((uint8_t *)kcpu_scratch_cpu(kcpu) + alloc.offset);

    obj->addr = cqs_gpu_va;

    /*
     * The command payload references the scratch object and the
     * number of objects. The value to write is stored in a second
     * scratch structure of type base_cqs_wait_info, which carries
     * both the address and the value to use.
     */
    struct kcpu_scratch_alloc walloc =
        kcpu_scratch_offset(kcpu, sizeof(struct manvil_base_cqs_wait_info));
    if (!walloc.valid) {
        kcpu->batch_count--;
        errno = ENOMEM;
        return -1;
    }

    struct manvil_base_cqs_wait_info *wobj =
        (struct manvil_base_cqs_wait_info *)
        ((uint8_t *)kcpu_scratch_cpu(kcpu) + walloc.offset);

    wobj->addr = cqs_gpu_va;
    wobj->val  = value;

    cmd->type = MANVIL_BASE_KCPU_COMMAND_TYPE_CQS_SET;
    cmd->info.cqs_set.objs    = kcpu_scratch_gpu(kcpu) + walloc.offset;
    cmd->info.cqs_set.nr_objs = 1;

    return 0;
}

int manvil_kcpu_add_cqs_set64(manvil_kcpu *kcpu,
                               uint64_t cqs_gpu_va,
                               uint64_t value)
{
    (void)cqs_gpu_va;
    (void)value;

    /*
     * The plain CQS set command only carries 32 bit values. The 64
     * bit variant must use the operation form with an explicit data
     * type of MANVIL_BASEP_CQS_DATA_TYPE_U64 and the SET operation.
     * Redirect to that form so that callers get consistent behavior.
     */
    return manvil_kcpu_add_cqs_set_operation(
        kcpu, cqs_gpu_va, value,
        MANVIL_BASEP_CQS_SET_OPERATION_SET,
        MANVIL_BASEP_CQS_DATA_TYPE_U64);
}

int manvil_kcpu_add_cqs_set_operation(manvil_kcpu *kcpu,
                                       uint64_t cqs_gpu_va,
                                       uint64_t value,
                                       uint8_t op,
                                       uint8_t data_type)
{
    if (op != MANVIL_BASEP_CQS_SET_OPERATION_ADD &&
        op != MANVIL_BASEP_CQS_SET_OPERATION_SET) {
        errno = EINVAL;
        return -1;
    }
    if (data_type != MANVIL_BASEP_CQS_DATA_TYPE_U32 &&
        data_type != MANVIL_BASEP_CQS_DATA_TYPE_U64) {
        errno = EINVAL;
        return -1;
    }

    struct manvil_base_kcpu_command *cmd = kcpu_next_command(kcpu);
    if (cmd == NULL) {
        return -1;
    }

    struct kcpu_scratch_alloc alloc =
        kcpu_scratch_offset(kcpu, sizeof(struct manvil_base_cqs_set_operation_info));
    if (!alloc.valid) {
        kcpu->batch_count--;
        errno = ENOMEM;
        return -1;
    }

    struct manvil_base_cqs_set_operation_info *obj =
        (struct manvil_base_cqs_set_operation_info *)
        ((uint8_t *)kcpu_scratch_cpu(kcpu) + alloc.offset);

    obj->addr      = cqs_gpu_va;
    obj->val       = value;
    obj->operation = op;
    obj->data_type = data_type;

    cmd->type = MANVIL_BASE_KCPU_COMMAND_TYPE_CQS_SET_OPERATION;
    cmd->info.cqs_set_operation.objs    = kcpu_scratch_gpu(kcpu) + alloc.offset;
    cmd->info.cqs_set_operation.nr_objs = 1;

    return 0;
}

int manvil_kcpu_add_cqs_wait(manvil_kcpu *kcpu,
                              uint64_t cqs_gpu_va,
                              uint32_t value)
{
    /*
     * The plain CQS wait tests for greater-or-equal. Map it to the
     * operation form so that the semantics are explicit.
     */
    return manvil_kcpu_add_cqs_wait_operation(
        kcpu, cqs_gpu_va, value,
        MANVIL_BASEP_CQS_WAIT_OPERATION_GE,
        MANVIL_BASEP_CQS_DATA_TYPE_U32);
}

int manvil_kcpu_add_cqs_wait_operation(manvil_kcpu *kcpu,
                                        uint64_t cqs_gpu_va,
                                        uint64_t value,
                                        uint8_t op,
                                        uint8_t data_type)
{
    if (op != MANVIL_BASEP_CQS_WAIT_OPERATION_LE &&
        op != MANVIL_BASEP_CQS_WAIT_OPERATION_GT) {
        errno = EINVAL;
        return -1;
    }
    if (data_type != MANVIL_BASEP_CQS_DATA_TYPE_U32 &&
        data_type != MANVIL_BASEP_CQS_DATA_TYPE_U64) {
        errno = EINVAL;
        return -1;
    }

    struct manvil_base_kcpu_command *cmd = kcpu_next_command(kcpu);
    if (cmd == NULL) {
        return -1;
    }

    struct kcpu_scratch_alloc alloc =
        kcpu_scratch_offset(kcpu, sizeof(struct manvil_base_cqs_wait_operation_info));
    if (!alloc.valid) {
        kcpu->batch_count--;
        errno = ENOMEM;
        return -1;
    }

    struct manvil_base_cqs_wait_operation_info *obj =
        (struct manvil_base_cqs_wait_operation_info *)
        ((uint8_t *)kcpu_scratch_cpu(kcpu) + alloc.offset);

    obj->addr      = cqs_gpu_va;
    obj->val       = value;
    obj->operation = op;
    obj->data_type = data_type;

    cmd->type = MANVIL_BASE_KCPU_COMMAND_TYPE_CQS_WAIT_OPERATION;
    cmd->info.cqs_wait_operation.objs    = kcpu_scratch_gpu(kcpu) + alloc.offset;
    cmd->info.cqs_wait_operation.nr_objs = 1;

    return 0;
}

int manvil_kcpu_add_fence_signal(manvil_kcpu *kcpu,
                                  uint64_t fence_va)
{
    struct manvil_base_kcpu_command *cmd = kcpu_next_command(kcpu);
    if (cmd == NULL) {
        return -1;
    }

    cmd->type = MANVIL_BASE_KCPU_COMMAND_TYPE_FENCE_SIGNAL;
    cmd->info.fence.fence = fence_va;

    return 0;
}

int manvil_kcpu_add_fence_wait(manvil_kcpu *kcpu,
                                int64_t timeout_ns)
{
    (void)timeout_ns;

    struct manvil_base_kcpu_command *cmd = kcpu_next_command(kcpu);
    if (cmd == NULL) {
        return -1;
    }

    /*
     * The timeout is a kernel side parameter. The UAPI form does not
     * carry a timeout value in the payload; the kernel uses its own
     * configured value for how long to wait before giving up. The
     * parameter here is kept in the API for future extension and for
     * symmetry with other runtimes, but is not transmitted.
     */
    cmd->type = MANVIL_BASE_KCPU_COMMAND_TYPE_FENCE_WAIT;
    cmd->info.fence.fence = 0;

    return 0;
}

int manvil_kcpu_add_error_barrier(manvil_kcpu *kcpu)
{
    struct manvil_base_kcpu_command *cmd = kcpu_next_command(kcpu);
    if (cmd == NULL) {
        return -1;
    }

    cmd->type = MANVIL_BASE_KCPU_COMMAND_TYPE_ERROR_BARRIER;
    return 0;
}

int manvil_kcpu_submit(manvil_kcpu *kcpu)
{
    if (kcpu == NULL || !kcpu->is_valid) {
        errno = EINVAL;
        return -1;
    }

    if (kcpu->batch_count == 0) {
        errno = EINVAL;
        return -1;
    }

    struct manvil_kbase_ioctl_kcpu_queue_enqueue args;
    memset(&args, 0, sizeof(args));

    args.addr        = manvil_mem_gpu_va(kcpu->cmd_mem);
    args.nr_commands = kcpu->batch_count;
    args.id          = kcpu->kcpu_id;

    int rc = manvil_kbase_ioctl(kcpu->kbase,
                                MANVIL_KBASE_IOCTL_KCPU_QUEUE_ENQUEUE,
                                &args,
                                "KCPU_QUEUE_ENQUEUE");
    if (rc < 0) {
        return -1;
    }

    /*
     * The kernel copied the batch. Reset the count so that new
     * commands start from the beginning of the command buffer. The
     * scratch region is not reset for the reason explained above.
     */
    kcpu->batch_count = 0;

    return 0;
}

/*
 * Accessors.
 */

manvil_mem *manvil_kcpu_result_mem(const manvil_kcpu *kcpu)
{
    return kcpu != NULL ? kcpu->result_mem : NULL;
}

void *manvil_kcpu_result_cpu_ptr(const manvil_kcpu *kcpu)
{
    return kcpu != NULL ? manvil_mem_cpu_ptr(kcpu->result_mem) : NULL;
}

uint64_t manvil_kcpu_result_gpu_va(const manvil_kcpu *kcpu)
{
    return kcpu != NULL ? manvil_mem_gpu_va(kcpu->result_mem) : 0;
}

uint64_t manvil_kcpu_result_size(const manvil_kcpu *kcpu)
{
    return kcpu != NULL ? manvil_mem_size(kcpu->result_mem) : 0;
}
