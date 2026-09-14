/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * CSF command builders.
 *
 * This module constructs command entries that are written into a GPU
 * command queue ring. Each entry is a pair of 64-bit words that the
 * CSF firmware decodes and executes. The bit level layout of each
 * command is described in the public genxml specification distributed
 * with Mesa 3D for the Valhall architecture.
 *
 * Acknowledgments:
 *   The command layouts implemented here are derived from the public
 *   specification in Mesa's panfrost/genxml/v10.xml through v14.xml.
 *   Mesa 3D is distributed under the MIT license. No Mesa source code
 *   is copied into Manvil; only the bit level specification is used,
 *   which is public hardware documentation.
 *
 * All commands are 16 bytes (128 bits). The opcode occupies bits
 * 56-63 of the command, which is the top byte of the first 64-bit
 * word. Low bit fields (masks, addresses, data) are in the first
 * word. The second word is unused by the commands implemented so far
 * and is written as zero.
 */

#ifndef MANVIL_CMD_H
#define MANVIL_CMD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A command entry occupies two 64-bit words.
 *
 * A command buffer is an array of these. Callers write entries
 * sequentially and submit the whole array to a queue.
 */
typedef uint64_t manvil_cmd[2];

/*
 * Size of a command entry in bytes.
 */
#define MANVIL_CMD_SIZE_BYTES (16u)

/*
 * Size of a command entry in 64-bit words.
 */
#define MANVIL_CMD_SIZE_WORDS (2u)

/*
 * CSF opcodes for the commands implemented by this module.
 *
 * These values match the CS Opcode enum in the genxml specification.
 */
#define MANVIL_CS_OPCODE_NOP             0u
#define MANVIL_CS_OPCODE_MOVE32          2u
#define MANVIL_CS_OPCODE_WAIT            3u
#define MANVIL_CS_OPCODE_RUN_COMPUTE     4u
#define MANVIL_CS_OPCODE_RUN_FULLSCREEN  8u
#define MANVIL_CS_OPCODE_CALL            32u
#define MANVIL_CS_OPCODE_JUMP            33u
#define MANVIL_CS_OPCODE_REQ_RESOURCE    34u
#define MANVIL_CS_OPCODE_FLUSH_CACHE2    36u
#define MANVIL_CS_OPCODE_ERROR_BARRIER   47u
#define MANVIL_CS_OPCODE_HEAP_SET        48u
#define MANVIL_CS_OPCODE_HEAP_OPERATION  49u
#define MANVIL_CS_OPCODE_SYNC_ADD64      51u
#define MANVIL_CS_OPCODE_SYNC_SET64      52u
#define MANVIL_CS_OPCODE_SYNC_WAIT64     53u
#define MANVIL_CS_OPCODE_ENOP            128u

/*
 * Bit position of the opcode field inside the command.
 */
#define MANVIL_CS_OPCODE_SHIFT 56u

/*
 * Enumerations used by the command fields.
 */

/*
 * Flush modes for L2 and LSC cache.
 */
enum manvil_cs_flush_mode {
    MANVIL_CS_FLUSH_NONE                = 0,
    MANVIL_CS_FLUSH_CLEAN               = 1,
    MANVIL_CS_FLUSH_CLEAN_AND_INVALIDATE = 3,
};

/*
 * Flush modes for other caches (the "other" group in FLUSH_CACHE2).
 */
enum manvil_cs_other_flush_mode {
    MANVIL_CS_OTHER_FLUSH_NONE       = 0,
    MANVIL_CS_OTHER_FLUSH_INVALIDATE = 2,
};

/*
 * Scope of a sync operation.
 *
 * System scope is visible to the whole GPU and to system memory. CSG
 * scope is visible only within the command stream group that issued
 * the operation.
 */
enum manvil_cs_sync_scope {
    MANVIL_CS_SYNC_SCOPE_SYSTEM = 0,
    MANVIL_CS_SYNC_SCOPE_CSG    = 2,
};

/*
 * Defer mode for commands that can defer their completion.
 *
 * Defer Immediate means the command completes as soon as it is
 * issued. Defer Indirect means the command completes only after all
 * scoreboards in the wait mask have been satisfied.
 */
enum manvil_cs_defer_mode {
    MANVIL_CS_DEFER_IMMEDIATE = 0,
    MANVIL_CS_DEFER_INDIRECT  = 1,
};

/*
 * Heap operation types used with HEAP_OPERATION.
 *
 * The three values correspond to the three render pass boundaries
 * that the tiler heap tracks. Manvil currently emits only
 * VERTEX_TILER_STARTED in the graphics stream, per the analysis
 * documented in reference/_research/04_kernel_internals/tiler_heap_model.md.
 */
enum manvil_cs_heap_operation {
    MANVIL_CS_HEAP_OP_VERTEX_TILER_STARTED   = 0,
    MANVIL_CS_HEAP_OP_VERTEX_TILER_COMPLETED = 1,
    MANVIL_CS_HEAP_OP_FRAGMENT_COMPLETED     = 3,
};

/*
 * Sync wait conditions used with SYNC_WAIT64.
 */
enum manvil_cs_condition {
    MANVIL_CS_COND_LEQUAL  = 0,
    MANVIL_CS_COND_GREATER = 1,
    MANVIL_CS_COND_EQUAL   = 2,
    MANVIL_CS_COND_NEQUAL  = 3,
    MANVIL_CS_COND_LESS    = 4,
    MANVIL_CS_COND_GEQUAL  = 5,
    MANVIL_CS_COND_ALWAYS  = 6,
};

/*
 * Wait mode for the WAIT command.
 */
enum manvil_cs_wait_mode {
    MANVIL_CS_WAIT_IMMEDIATE = 0,
    MANVIL_CS_WAIT_INDIRECT  = 1,
};

/*
 * Command builders.
 *
 * Each function takes a pointer to a two word buffer and writes the
 * encoded command into it. The buffer must be writable and aligned to
 * 8 bytes. No bounds checking is done; the caller is responsible for
 * ensuring that the buffer has room for MANVIL_CMD_SIZE_BYTES bytes.
 */

/*
 * No operation. Uses the NOP opcode. The bytes are all zero except
 * for the opcode.
 */
void manvil_cmd_nop(manvil_cmd cmd);

/*
 * Extended no-op. Carries 56 bits of driver metadata that do not
 * affect execution. Used to embed a marker that a decoder can
 * identify, for example during capture and replay.
 */
void manvil_cmd_enop(manvil_cmd cmd, uint64_t driver_metadata);

/*
 * Wait for the given scoreboards to become zero.
 *
 * wait_mask   16 bit mask of scoreboard entries to wait on.
 * wait_mode   MANVIL_CS_WAIT_IMMEDIATE or _INDIRECT.
 */
void manvil_cmd_wait(manvil_cmd cmd, uint16_t wait_mask,
                     uint8_t wait_mode);

/*
 * Request the specified resources for the current group.
 *
 * This tells the scheduler which endpoint types the command stream is
 * about to use. The exact semantics of resource negotiation are
 * handled by the firmware.
 */
void manvil_cmd_req_resource(manvil_cmd cmd,
                              bool compute,
                              bool fragment,
                              bool tiler,
                              bool idvs,
                              bool rt);

/*
 * Call a subroutine at the given address.
 *
 * The CALL command transfers control to a stream of commands stored
 * elsewhere in GPU memory, executes them, and returns. The length
 * field describes how many instruction words the called block
 * contains, which lets the firmware prefetch the block.
 *
 * address     GPU virtual address of the target block, in bytes.
 *             The low 4 bits are ignored because the command stream
 *             is aligned to 16 bytes.
 * length      Number of 16-byte entries in the target block, encoded
 *             in the low 8 bits of the length field.
 */
void manvil_cmd_call(manvil_cmd cmd, uint64_t address, uint8_t length);

/*
 * Jump to a subroutine at the given address without saving a return
 * address. Same field layout as CALL.
 */
void manvil_cmd_jump(manvil_cmd cmd, uint64_t address, uint8_t length);

/*
 * Flush caches and wait for a specific latest flush identifier.
 *
 * The command ensures that the caches named by the mode fields are
 * flushed, and that the operation is deferred until the latest flush
 * identifier matches the value encoded in the command. It is used to
 * establish ordering between CPU writes and GPU reads.
 *
 * l2_mode           MANVIL_CS_FLUSH_*
 * lsc_mode          MANVIL_CS_FLUSH_*
 * other_mode        MANVIL_CS_OTHER_FLUSH_*
 * wait_mask         16 bit scoreboard mask to defer on.
 * latest_flush_id   8 bit value from LATEST_FLUSH at the time of issue.
 * signal_slot       scoreboard entry that receives the flush completion.
 * defer_mode        MANVIL_CS_DEFER_*
 */
void manvil_cmd_flush_cache2(manvil_cmd cmd,
                              uint8_t l2_mode,
                              uint8_t lsc_mode,
                              uint8_t other_mode,
                              uint16_t wait_mask,
                              uint8_t latest_flush_id,
                              uint8_t signal_slot,
                              uint8_t defer_mode);

/*
 * Set the current tiler heap context.
 *
 * The firmware uses the address provided here as the heap context
 * structure for subsequent tiler operations. The address is the
 * gpu_heap_va returned by the CS_TILER_HEAP_INIT ioctl.
 *
 * address     GPU virtual address of the heap context, in bytes.
 */
void manvil_cmd_heap_set(manvil_cmd cmd, uint64_t address);

/*
 * Emit a tiler heap operation.
 *
 * The operation updates the firmware side heap statistics tracked in
 * the stream's output page. The available operations correspond to
 * the render pass boundaries that the kernel validates.
 *
 * operation    MANVIL_CS_HEAP_OP_*
 * wait_mask    16 bit scoreboard mask to defer on.
 * signal_slot  scoreboard entry that receives the operation completion.
 * defer_mode   MANVIL_CS_DEFER_*
 */
void manvil_cmd_heap_operation(manvil_cmd cmd,
                                uint8_t operation,
                                uint16_t wait_mask,
                                uint8_t signal_slot,
                                uint8_t defer_mode);

/*
 * Signal a 64-bit sync object by adding a value to it.
 *
 * The sync object is a 64-bit counter in GPU memory. Adding value
 * raises it by value, which is the mechanism used to signal fences
 * and timeline semaphores.
 *
 * data              value to add to the sync object.
 * address           GPU virtual address of the object, in bytes.
 * scope             MANVIL_CS_SYNC_SCOPE_SYSTEM or _CSG.
 * wait_mask         16 bit scoreboard mask to defer on.
 * signal_slot       scoreboard entry that receives the operation completion.
 * defer_mode        MANVIL_CS_DEFER_*
 * error_propagate   if true, propagate the error state of the sync
 *                   object into the queue error state.
 */
void manvil_cmd_sync_add64(manvil_cmd cmd,
                            uint64_t data,
                            uint64_t address,
                            uint8_t  scope,
                            uint16_t wait_mask,
                            uint8_t  signal_slot,
                            uint8_t  defer_mode,
                            bool     error_propagate);

/*
 * Signal a 64-bit sync object by setting it to a value.
 *
 * Same as SYNC_ADD64 but assigns the value directly instead of
 * adding to the current value. Used to reset a timeline to a known
 * value.
 */
void manvil_cmd_sync_set64(manvil_cmd cmd,
                            uint64_t data,
                            uint64_t address,
                            uint8_t  scope,
                            uint16_t wait_mask,
                            uint8_t  signal_slot,
                            uint8_t  defer_mode,
                            bool     error_propagate);

/*
 * Wait for a 64-bit sync object to satisfy a condition.
 *
 * The wait blocks the command stream until the condition is
 * satisfied. The condition compares the current value of the object
 * against the given data value.
 *
 * data         value to compare against.
 * address      GPU virtual address of the object, in bytes.
 * condition    MANVIL_CS_COND_*
 * error_reject if true, reject the operation if the sync object's
 *              error state is set.
 */
void manvil_cmd_sync_wait64(manvil_cmd cmd,
                             uint64_t data,
                             uint64_t address,
                             uint8_t  condition,
                             bool     error_reject);

/*
 * Error barrier.
 *
 * The firmware stops processing the command stream until the error
 * state of the queue is resolved. No payload.
 */
void manvil_cmd_error_barrier(manvil_cmd cmd);

#ifdef __cplusplus
}
#endif

#endif /* MANVIL_CMD_H */
