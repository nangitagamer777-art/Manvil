/*
 * Manvil - Native Vulkan driver for Mali Valhall GPUs on Kbase
 * Copyright (c) 2026 Noin Haxel / Mecca OpenSource
 * SPDX-License-Identifier: MIT
 *
 * CSF command builders implementation.
 *
 * Acknowledgments:
 *   The bit level layouts implemented here are derived from the public
 *   genxml specification in Mesa 3D (panfrost/genxml/v10.xml through
 *   v14.xml), which is distributed under the MIT license. No Mesa
 *   source code is copied. Only the bit level specification is used.
 *
 * Encoding conventions:
 *   A command is 16 bytes, stored as two 64-bit words. Bit 0 of the
 *   command corresponds to bit 0 of word 0. Bit 63 of word 0 is the
 *   top byte, which holds the opcode (bits 56-63 of the command).
 *   Word 1 holds bits 64-127 and is currently zero for all commands
 *   implemented here.
 *
 *   Field offsets in the specification are given as "start" (bit
 *   position) and "size" (bit count). A field at start S of size N
 *   occupies bits S through S+N-1 of the command.
 */

#include "cmd.h"

#include <string.h>

/*
 * Internal helper: write a field of "size" bits into a command
 * buffer at the given bit offset.
 *
 * The field is placed in word 0 when it fits entirely within the
 * first 64 bits. Bits above 63 are written to word 1.
 */
static void cmd_write_field(manvil_cmd cmd, unsigned start,
                             unsigned size, uint64_t value)
{
    unsigned i;
    for (i = 0; i < size; i++) {
        unsigned bit = start + i;
        uint64_t bit_val = (value >> i) & 1ull;
        unsigned word = bit / 64u;
        unsigned shift = bit % 64u;

        if (bit_val) {
            cmd[word] |= (1ull << shift);
        } else {
            cmd[word] &= ~(1ull << shift);
        }
    }
}

/*
 * Internal helper: set the opcode field of a command.
 */
static void cmd_set_opcode(manvil_cmd cmd, uint8_t opcode)
{
    cmd_write_field(cmd, MANVIL_CS_OPCODE_SHIFT, 8u, opcode);
}

/*
 * Internal helper: reset a command buffer to all zeroes.
 */
static void cmd_zero(manvil_cmd cmd)
{
    cmd[0] = 0;
    cmd[1] = 0;
}

/*
 * Internal helper: set or clear a single bit.
 */
static void cmd_set_bit(manvil_cmd cmd, unsigned bit, bool value)
{
    cmd_write_field(cmd, bit, 1u, value ? 1ull : 0ull);
}

/*
 * Public builders.
 */

void manvil_cmd_nop(manvil_cmd cmd)
{
    cmd_zero(cmd);
    cmd_set_opcode(cmd, MANVIL_CS_OPCODE_NOP);
}

void manvil_cmd_enop(manvil_cmd cmd, uint64_t driver_metadata)
{
    cmd_zero(cmd);

    /*
     * Driver Meta Data occupies bits 0-55 (56 bits). If the supplied
     * value has bits above 55 they are truncated.
     */
    cmd_write_field(cmd, 0u, 56u, driver_metadata);
    cmd_set_opcode(cmd, MANVIL_CS_OPCODE_ENOP);
}

void manvil_cmd_move48(manvil_cmd cmd, uint8_t dest_reg, uint64_t value)
{
    cmd_zero(cmd);

    /*
     * Immediate: bits 0-47 (48 bits).
     *
     * The value is truncated to 48 bits. GPU virtual addresses on the
     * tested hardware fit, but callers should check before using
     * this function with larger values.
     */
    cmd_write_field(cmd, 0u, 48u, value & 0xFFFFFFFFFFFFull);

    /*
     * Destination: bits 48-55 (8 bits).
     */
    cmd_write_field(cmd, 48u, 8u, dest_reg);

    cmd_set_opcode(cmd, MANVIL_CS_OPCODE_MOVE48);
}

void manvil_cmd_move32(manvil_cmd cmd, uint8_t dest_reg, uint32_t value)
{
    cmd_zero(cmd);

    /*
     * Immediate: bits 0-31 (32 bits).
     */
    cmd_write_field(cmd, 0u, 32u, value);

    /*
     * Destination: bits 48-55 (8 bits).
     */
    cmd_write_field(cmd, 48u, 8u, dest_reg);

    cmd_set_opcode(cmd, MANVIL_CS_OPCODE_MOVE32);
}

void manvil_cmd_wait(manvil_cmd cmd, uint16_t wait_mask, uint8_t wait_mode)
{
    cmd_zero(cmd);

    /*
     * Wait mask: bits 16-31.
     */
    cmd_write_field(cmd, 16u, 16u, wait_mask);

    /*
     * Wait Mode: bit 33.
     */
    cmd_set_bit(cmd, 33u, wait_mode == MANVIL_CS_WAIT_INDIRECT);

    cmd_set_opcode(cmd, MANVIL_CS_OPCODE_WAIT);
}

void manvil_cmd_req_resource(manvil_cmd cmd,
                              bool compute,
                              bool fragment,
                              bool tiler,
                              bool idvs,
                              bool rt)
{
    cmd_zero(cmd);

    cmd_set_bit(cmd, 0u, compute);
    cmd_set_bit(cmd, 1u, fragment);
    cmd_set_bit(cmd, 2u, tiler);
    cmd_set_bit(cmd, 3u, idvs);
    cmd_set_bit(cmd, 4u, rt);

    cmd_set_opcode(cmd, MANVIL_CS_OPCODE_REQ_RESOURCE);
}

void manvil_cmd_call(manvil_cmd cmd, uint64_t address, uint8_t length)
{
    cmd_zero(cmd);

    /*
     * Length: bits 32-39 (8 bits).
     */
    cmd_write_field(cmd, 32u, 8u, length);

    /*
     * Address: bits 40-47 (8 bits). The address field is only 8 bits
     * wide in the specification, which means the target address is
     * encoded in units of 256 bytes (the low byte of the address is
     * dropped). This matches the format expected by the firmware.
     */
    cmd_write_field(cmd, 40u, 8u, (address >> 8) & 0xffu);

    cmd_set_opcode(cmd, MANVIL_CS_OPCODE_CALL);
}

void manvil_cmd_jump(manvil_cmd cmd, uint64_t address, uint8_t length)
{
    cmd_zero(cmd);

    cmd_write_field(cmd, 32u, 8u, length);
    cmd_write_field(cmd, 40u, 8u, (address >> 8) & 0xffu);

    cmd_set_opcode(cmd, MANVIL_CS_OPCODE_JUMP);
}

void manvil_cmd_flush_cache2(manvil_cmd cmd,
                              uint8_t l2_mode,
                              uint8_t lsc_mode,
                              uint8_t other_mode,
                              uint16_t wait_mask,
                              uint8_t latest_flush_id,
                              uint8_t signal_slot,
                              uint8_t defer_mode)
{
    cmd_zero(cmd);

    /*
     * L2 Flush Mode: bits 0-3.
     */
    cmd_write_field(cmd, 0u, 4u, l2_mode);

    /*
     * LSC Flush Mode: bits 4-7.
     */
    cmd_write_field(cmd, 4u, 4u, lsc_mode);

    /*
     * Other Flush Mode: bits 8-11.
     */
    cmd_write_field(cmd, 8u, 4u, other_mode);

    /*
     * Wait Mask: bits 16-31.
     */
    cmd_write_field(cmd, 16u, 16u, wait_mask);

    /*
     * Latest Flush ID: bits 40-47.
     */
    cmd_write_field(cmd, 40u, 8u, latest_flush_id);

    /*
     * Signal slot: bits 48-51.
     */
    cmd_write_field(cmd, 48u, 4u, signal_slot);

    /*
     * Defer Mode: bit 52.
     */
    cmd_set_bit(cmd, 52u, defer_mode == MANVIL_CS_DEFER_INDIRECT);

    cmd_set_opcode(cmd, MANVIL_CS_OPCODE_FLUSH_CACHE2);
}

void manvil_cmd_heap_set(manvil_cmd cmd, uint64_t address)
{
    cmd_zero(cmd);

    /*
     * Address: bits 40-47 (8 bits).
     *
     * The address field is 8 bits wide, encoded in units of 256
     * bytes. The heap context structures allocated by the kernel are
     * aligned to a page boundary, so the low byte is always zero.
     * The firmware reconstructs the full address by shifting left.
     */
    cmd_write_field(cmd, 40u, 8u, (address >> 8) & 0xffu);

    cmd_set_opcode(cmd, MANVIL_CS_OPCODE_HEAP_SET);
}

void manvil_cmd_heap_operation(manvil_cmd cmd,
                                uint8_t operation,
                                uint16_t wait_mask,
                                uint8_t signal_slot,
                                uint8_t defer_mode)
{
    cmd_zero(cmd);

    /*
     * Wait Mask: bits 16-31.
     */
    cmd_write_field(cmd, 16u, 16u, wait_mask);

    /*
     * Operation: bits 32-34 (3 bits).
     */
    cmd_write_field(cmd, 32u, 3u, operation);

    /*
     * Signal slot: bits 48-51.
     */
    cmd_write_field(cmd, 48u, 4u, signal_slot);

    /*
     * Defer Mode: bit 52.
     */
    cmd_set_bit(cmd, 52u, defer_mode == MANVIL_CS_DEFER_INDIRECT);

    cmd_set_opcode(cmd, MANVIL_CS_OPCODE_HEAP_OPERATION);
}

void manvil_cmd_sync_add64(manvil_cmd cmd,
                            uint8_t  address_reg,
                            uint8_t  data_reg,
                            uint8_t  scope,
                            uint16_t wait_mask,
                            uint8_t  signal_slot,
                            uint8_t  defer_mode,
                            bool     error_propagate)
{
    cmd_zero(cmd);

    /*
     * Error Propagate: bit 0.
     */
    cmd_set_bit(cmd, 0u, error_propagate);

    /*
     * Scope: bits 1-2 (2 bits).
     */
    cmd_write_field(cmd, 1u, 2u, scope);

    /*
     * Wait Mask: bits 16-31.
     */
    cmd_write_field(cmd, 16u, 16u, wait_mask);

    /*
     * Data: bits 32-39 (8 bits). Index of the register that holds
     * the value to add.
     */
    cmd_write_field(cmd, 32u, 8u, data_reg);

    /*
     * Address: bits 40-47 (8 bits). Index of the register that
     * holds the address of the sync object.
     */
    cmd_write_field(cmd, 40u, 8u, address_reg);

    /*
     * Signal slot: bits 48-51.
     */
    cmd_write_field(cmd, 48u, 4u, signal_slot);

    /*
     * Defer Mode: bit 52.
     */
    cmd_set_bit(cmd, 52u, defer_mode == MANVIL_CS_DEFER_INDIRECT);

    cmd_set_opcode(cmd, MANVIL_CS_OPCODE_SYNC_ADD64);
}

void manvil_cmd_sync_set64(manvil_cmd cmd,
                            uint8_t  address_reg,
                            uint8_t  data_reg,
                            uint8_t  scope,
                            uint16_t wait_mask,
                            uint8_t  signal_slot,
                            uint8_t  defer_mode,
                            bool     error_propagate)
{
    cmd_zero(cmd);

    cmd_set_bit(cmd, 0u, error_propagate);
    cmd_write_field(cmd, 1u, 2u, scope);
    cmd_write_field(cmd, 16u, 16u, wait_mask);
    cmd_write_field(cmd, 32u, 8u, data_reg);
    cmd_write_field(cmd, 40u, 8u, address_reg);
    cmd_write_field(cmd, 48u, 4u, signal_slot);
    cmd_set_bit(cmd, 52u, defer_mode == MANVIL_CS_DEFER_INDIRECT);

    cmd_set_opcode(cmd, MANVIL_CS_OPCODE_SYNC_SET64);
}

void manvil_cmd_sync_wait64(manvil_cmd cmd,
                             uint64_t data,
                             uint64_t address,
                             uint8_t  condition,
                             bool     error_reject)
{
    cmd_zero(cmd);

    /*
     * Error Reject: bit 0.
     */
    cmd_set_bit(cmd, 0u, error_reject);

    /*
     * Condition: bits 28-31 (4 bits).
     */
    cmd_write_field(cmd, 28u, 4u, condition);

    /*
     * Data: bits 32-39 (8 bits).
     */
    cmd_write_field(cmd, 32u, 8u, data & 0xffu);

    /*
     * Address: bits 40-47 (8 bits, in units of 256 bytes).
     */
    cmd_write_field(cmd, 40u, 8u, (address >> 8) & 0xffu);

    cmd_set_opcode(cmd, MANVIL_CS_OPCODE_SYNC_WAIT64);
}

void manvil_cmd_error_barrier(manvil_cmd cmd)
{
    cmd_zero(cmd);
    cmd_set_opcode(cmd, MANVIL_CS_OPCODE_ERROR_BARRIER);
}
