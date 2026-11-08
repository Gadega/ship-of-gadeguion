#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fix relative constant addressing in PSL1GHT cgcomp vertex programs.

cgcomp compiles `c[A0.x + n]` to an instruction that reads plain c[0]: it leaves the
relative-addressing flag (index_const, bit 1 of the instruction's 4th word) clear and
drops the offset n from the constant field (D1 bits 12..21). The RSX then reads the
wrong constants (the GPU vertex path drew nothing). This reads the assembly listing
cgcomp writes with -d (<output>.dump), finds the instructions that use c[A0.x + n] and
patches the .vpo: index_const = 1 and const_src = n. Instruction layout from RPCS3
(rpcs3/Emu/RSX/Program/RSXVertexProgram.h); the address register component is A0.x
(addr_swz 0), which is all our shaders use.

usage: patch-vp-relative.py <program.vpo> <program.vpo.dump>
"""

import re
import struct
import sys

HEADER = ">HHHHHHIIIIIHH"


def instructions(dump_text):
    """The assembly instruction lines, in order (declarations and labels skipped)."""
    out = []
    in_body = False
    for raw in dump_text.splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        if line.endswith(":"):
            in_body = True
            continue
        if not in_body or line == "END":
            continue
        word = line.split()[0]
        if word in ("TEMP", "ADDRESS", "PARAM", "OUTPUT", "ATTRIB", "OPTION"):
            continue
        out.append(line)
    return out


def main(vpo_path, dump_path):
    data = bytearray(open(vpo_path, "rb").read())
    (_magic, _pad, _regs, _attrs, _consts, num_insn, _attr_off, _const_off, ucode_off, _in_mask, _out_mask,
     _const_start, _insn_start) = struct.unpack_from(HEADER, data, 0)
    asm = instructions(open(dump_path, encoding="utf-8", errors="replace").read())
    if len(asm) != num_insn:
        sys.exit("patch-vp-relative: %s: %d listed instructions, %d in the program" % (vpo_path, len(asm), num_insn))
    patched = 0
    for index, line in enumerate(asm):
        refs = re.findall(r"c\[A0\.([xyzw])\s*(?:([+-])\s*(\d+))?\]", line)
        if not refs:
            continue
        if any(r[0] != "x" for r in refs) or len(set(refs)) != 1:
            sys.exit("patch-vp-relative: %s: unsupported relative access: %s" % (vpo_path, line))
        sign, number = refs[0][1], refs[0][2]
        offset = int(number) if number else 0
        if sign == "-":
            offset = -offset
        if not 0 <= offset < 1024:
            sys.exit("patch-vp-relative: %s: offset %d out of range: %s" % (vpo_path, offset, line))
        at = ucode_off + index * 16
        d0, d1, d2, d3 = struct.unpack_from(">4I", data, at)
        d1 = (d1 & ~(0x3FF << 12)) | (offset << 12)  # const_src
        d3 |= 1 << 1                                   # index_const
        # cgcomp also leaves the constant operand's register type at 0 (invalid) instead of
        # 3 (constant). Source register type = low 2 bits of each 17-bit source field:
        # src0 at D2 bit 23, src1 at D2 bit 6, src2 at D3 bit 21.
        fixed = 0
        for word, bit in ((2, 23), (2, 6), (3, 21)):
            value = d2 if word == 2 else d3
            if (value >> bit) & 3 == 0:
                value |= 3 << bit
                fixed += 1
            if word == 2:
                d2 = value
            else:
                d3 = value
        if fixed != len(re.findall(r"c\[A0\.", line)):
            sys.exit("patch-vp-relative: %s: %d type-0 sources for %s" % (vpo_path, fixed, line))
        struct.pack_into(">4I", data, at, d0, d1, d2, d3)
        patched += 1
    open(vpo_path, "wb").write(bytes(data))
    print("patch-vp-relative: %s: %d instruction(s) patched" % (vpo_path, patched))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
