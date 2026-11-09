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


def get_src(d1, d2, d3, k):
    """17-bit source field k (RPCS3 RSXVertexProgram.h layout)."""
    if k == 0:
        return ((d1 & 0xFF) << 9) | (d2 >> 23)
    if k == 1:
        return (d2 >> 6) & 0x1FFFF
    return ((d2 & 0x3F) << 11) | (d3 >> 21)


def set_src(d1, d2, d3, k, src):
    if k == 0:
        d1 = (d1 & ~0xFF) | (src >> 9)
        d2 = (d2 & 0x007FFFFF) | ((src & 0x1FF) << 23)
    elif k == 1:
        d2 = (d2 & ~(0x1FFFF << 6)) | (src << 6)
    else:
        d2 = (d2 & ~0x3F) | (src >> 11)
        d3 = (d3 & 0x001FFFFF) | ((src & 0x7FF) << 21)
    return d1, d2 & 0xFFFFFFFF, d3 & 0xFFFFFFFF


def relative_operands(line):
    """(negate, abs, 8-bit swizzle) for each c[A0.x ...] operand, in order."""
    out = []
    for m in re.finditer(r"(-?)(\|?)c\[A0\.x[^\]]*\](?:\.([xyzw]{1,4}))?", line):
        comps = m.group(3) or "xyzw"
        comps = comps + comps[-1] * (4 - len(comps))
        swizzle = 0
        for shift, c in zip((6, 4, 2, 0), comps):
            swizzle |= "xyzw".index(c) << shift
        out.append((1 if m.group(1) else 0, 1 if m.group(2) else 0, swizzle))
    return out


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
        # cgcomp also leaves each relative constant operand as register type 0 (invalid)
        # with the default swizzle, no negation and no abs: the component selector
        # (e.g. the .z of c[A0.x + 5].z) is lost. Rebuild those sources from the listing.
        # The type-0 slots (src0, src1, src2 in order) match the operands in order.
        ops = relative_operands(line)
        slots = [k for k in range(3) if get_src(d1, d2, d3, k) & 3 == 0]
        if len(slots) != len(ops):
            sys.exit("patch-vp-relative: %s: %d type-0 sources, %d relative operands: %s" % (vpo_path, len(slots), len(ops), line))
        for k, (neg, absolute, swizzle) in zip(slots, ops):
            src = get_src(d1, d2, d3, k)
            src = (src & ~0x1FF03) | 3 | (swizzle << 8) | (neg << 16)
            d1, d2, d3 = set_src(d1, d2, d3, k, src)
            d0 = (d0 & ~(1 << (21 + k))) | (absolute << (21 + k))
        struct.pack_into(">4I", data, at, d0, d1, d2, d3)
        patched += 1
    open(vpo_path, "wb").write(bytes(data))
    print("patch-vp-relative: %s: %d instruction(s) patched" % (vpo_path, patched))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
