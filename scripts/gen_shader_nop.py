#!/usr/bin/env python3
"""
Generate a minimal Valhall ISA shader consisting of four NOP
instructions. The last one carries flow=end which tells the firmware
to return from the shader.

The bit layout is documented in the public genxml specification
distributed with Mesa 3D (src/panfrost/compiler/kraid/isa-v9-v14.xml).

Usage:
  python3 scripts/gen_shader_nop.py > assets/shader_nop.bin

Or:
  python3 scripts/gen_shader_nop.py assets/shader_nop.bin
"""
import struct
import sys


def nop(flow_end: bool) -> bytes:
    """Build one 64-bit Valhall NOP instruction."""
    dest_width = 3    # enum dest_width_m::none
    opcode1 = 0       # enum N0_opcode1_t::NOP
    instr_class = 0   # enum instruction_class_t::N0
    fau_page = 0
    flow = 15 if flow_end else 0  # enum flow_control_m::end / none

    word1 = 0
    word1 |= (dest_width & 0x3) << (46 - 32)
    word1 |= (opcode1 & 0xF) << (48 - 32)
    word1 |= (instr_class & 0x1F) << (52 - 32)
    word1 |= (fau_page & 0x3) << (57 - 32)
    word1 |= (flow & 0xF) << (59 - 32)

    return struct.pack("<II", 0, word1)


def main() -> int:
    shader = nop(False) + nop(False) + nop(False) + nop(True)

    if len(sys.argv) > 1:
        with open(sys.argv[1], "wb") as f:
            f.write(shader)
    else:
        sys.stdout.buffer.write(shader)

    return 0


if __name__ == "__main__":
    sys.exit(main())
