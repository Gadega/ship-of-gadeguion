#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare struct/union layouts of the PS3 build against a PC (x86-64, little-endian) build.

Ship of Harkinian has only ever run on little-endian PCs. Headers that choose a layout
with `#ifdef IS_BIGENDIAN` (or similar) can silently take the wrong branch on the PS3
(audio commands, cutscene macros, Color_RGBA8_u32 were all this). Pointers are 8 bytes
on both targets, so every layout should be identical; this finds where it isn't.

Input: two object files compiled from the same probe source with -g (DWARF), read with
`readelf --debug-dump=info`. Checks, per named struct/union type:
  1. size differs
  2. member set / order / offsets / sizes differ
  3. ENDIAN-SENSITIVE OVERLAYS: in a union that overlays an integer with smaller fields
     (struct members or bit-fields), which bits of the integer each field occupies.
     PC code was written for the PC mapping; if the PS3 mapping differs, code that
     writes the integer and reads the fields (or the reverse) behaves differently.
  4. bit-field layout of plain structs (informational: only matters if the struct is
     also accessed as raw bytes/integers)

Usage: layout_parity.py <ps3.o> <pc.o> [report.txt]
"""
import re
import subprocess
import sys
from collections import defaultdict

DIE_RE = re.compile(r"^\s*<(\d+)><([0-9a-f]+)>: Abbrev Number: (\d+)(?: \((DW_TAG_\w+)\))?")
ATTR_RE = re.compile(r"^\s*<[0-9a-f]+>\s+(DW_AT_\w+)\s*:\s*(.*)$")


def parse(path, readelf):
    out = subprocess.run([readelf, "--debug-dump=info", "-W", path], capture_output=True, text=True,
                         errors="replace", check=True).stdout
    dies = {}
    stack = []
    cu_base = 0
    for line in out.splitlines():
        if "Compilation Unit @ offset" in line:
            stack = []
            continue
        m = DIE_RE.match(line)
        if m:
            depth, off, abbrev, tag = int(m.group(1)), int(m.group(2), 16), int(m.group(3)), m.group(4)
            if abbrev == 0:
                continue
            die = {"tag": tag, "attrs": {}, "children": [], "off": off}
            dies[off] = die
            del stack[depth:]
            if stack:
                stack[-1]["children"].append(die)
            stack.append(die)
            continue
        m = ATTR_RE.match(line)
        if m and stack:
            stack[-1]["attrs"][m.group(1)] = m.group(2).strip()
    return dies


def attr_name(die):
    v = die["attrs"].get("DW_AT_name")
    if v is None:
        return None
    # "(indirect string, offset: 0x123): name", "(string) name", "(strp) name" or plain
    v = v.strip()
    while v.startswith("("):  # one or more "(form)" / "(offset: 0x..)" prefixes, then an optional ':'
        v = re.sub(r"^\([^)]*\)\s*:?\s*", "", v, count=1)
    return v.strip()


def attr_int(die, key):
    v = die["attrs"].get(key)
    if v is None:
        return None
    m = re.search(r"(-?(?:0x[0-9a-f]+|\d+))\s*$", v)
    if not m:
        m = re.search(r"DW_OP_plus_uconst:\s*(\d+)", v)
        if not m:
            return None
    s = m.group(1)
    return int(s, 16) if s.startswith("0x") or s.startswith("-0x") else int(s)


def attr_ref(die, key="DW_AT_type"):
    v = die["attrs"].get(key)
    if v is None:
        return None
    m = re.search(r"<0x([0-9a-f]+)>", v)
    return int(m.group(1), 16) if m else None


class Types:
    def __init__(self, dies):
        self.dies = dies

    def strip(self, off):
        """Follow typedef/const/volatile to the underlying type."""
        seen = 0
        while off is not None and seen < 50:
            d = self.dies.get(off)
            if d is None:
                return None
            if d["tag"] in ("DW_TAG_typedef", "DW_TAG_const_type", "DW_TAG_volatile_type"):
                off = attr_ref(d)
                seen += 1
                continue
            return d
        return None

    def size(self, off):
        d = self.strip(off)
        if d is None:
            return None
        if d["tag"] == "DW_TAG_pointer_type":
            return attr_int(d, "DW_AT_byte_size") or 8
        if d["tag"] == "DW_TAG_array_type":
            el = self.size(attr_ref(d))
            n = 1
            for c in d["children"]:
                if c["tag"] == "DW_TAG_subrange_type":
                    ub = attr_int(c, "DW_AT_upper_bound")
                    cnt = attr_int(c, "DW_AT_count")
                    n *= (ub + 1) if ub is not None else (cnt or 0)
            return el * n if el is not None else None
        return attr_int(d, "DW_AT_byte_size")

    def desc(self, off):
        d = self.strip(off)
        if d is None:
            return "?"
        t = d["tag"]
        if t == "DW_TAG_base_type":
            return attr_name(d) or "base"
        if t == "DW_TAG_pointer_type":
            return "ptr"
        if t == "DW_TAG_array_type":
            return "array(%s)[%s]" % (self.desc(attr_ref(d)), self.size(off))
        if t in ("DW_TAG_structure_type", "DW_TAG_union_type"):
            return ("struct " if t == "DW_TAG_structure_type" else "union ") + (attr_name(d) or "<anon>")
        if t == "DW_TAG_enumeration_type":
            return "enum"
        return t.replace("DW_TAG_", "")

    def is_integer(self, off):
        d = self.strip(off)
        return d is not None and d["tag"] in ("DW_TAG_base_type", "DW_TAG_enumeration_type") and \
            "float" not in (attr_name(d) or "") and "double" not in (attr_name(d) or "")

    def flat(self, die, base=0, prefix="", depth=0, out=None):
        """Flattened members: (path, byte offset, size, bit_offset_in_storage, bit_size, type desc)."""
        if out is None:
            out = []
        if depth > 6:
            return out
        for c in die["children"]:
            if c["tag"] != "DW_TAG_member":
                continue
            name = attr_name(c) or "<anon>"
            path = prefix + name
            loc = attr_int(c, "DW_AT_data_member_location") or 0
            tref = attr_ref(c)
            sz = self.size(tref)
            bit_size = attr_int(c, "DW_AT_bit_size")
            if bit_size is not None:
                # DWARF4: DW_AT_data_bit_offset (from struct start) or DW_AT_bit_offset (from MSB of storage)
                dbo = attr_int(c, "DW_AT_data_bit_offset")
                if dbo is not None:
                    out.append((path, base + dbo // 8, sz, dbo % 8 if sz is None else dbo - loc * 8 if loc else dbo, bit_size, "bitfield", dbo))
                else:
                    bo = attr_int(c, "DW_AT_bit_offset") or 0
                    out.append((path, base + loc, sz, bo, bit_size, "bitfield-msb", None))
                continue
            sub = self.strip(tref)
            out.append((path, base + loc, sz, None, None, self.desc(tref), sub["off"] if sub is not None else None))
            if sub is not None and sub["tag"] in ("DW_TAG_structure_type", "DW_TAG_union_type"):
                self.flat(sub, base + loc, path + ".", depth + 1, out)
        return out


def named_types(dies):
    """name -> struct/union DIE (by tag name or by typedef of an anonymous one)."""
    t = Types(dies)
    res = {}
    for off, d in dies.items():
        if d["tag"] in ("DW_TAG_structure_type", "DW_TAG_union_type") and d["children"]:
            n = attr_name(d)
            if n:
                res.setdefault(("struct " if d["tag"] == "DW_TAG_structure_type" else "union ") + n, d)
        elif d["tag"] == "DW_TAG_typedef":
            n = attr_name(d)
            s = t.strip(attr_ref(d))
            if n and s is not None and s["tag"] in ("DW_TAG_structure_type", "DW_TAG_union_type") and s["children"]:
                res.setdefault(n, s)
    return t, res


def union_bitmap(types, udie, big_endian):
    """For a union holding an integer and smaller fields: field -> (first bit, last bit) of that integer,
    numbered from the least significant bit. Bit positions are what code that writes the integer
    (e.g. RGBA8() packing) and reads the fields relies on."""
    members = [c for c in udie["children"] if c["tag"] == "DW_TAG_member"]
    # "force_structure_alignment" / "force_union_align" style members only align the union;
    # nobody reads them as data, so they are not overlays.
    ints = [(attr_name(c) or "<anon>", types.size(attr_ref(c))) for c in members if types.is_integer(attr_ref(c))
            and "align" not in (attr_name(c) or "")]
    if not ints:
        return None
    iname, isize = max(ints, key=lambda x: x[1] or 0)
    if not isize:
        return None
    result = {}
    for c in members:
        name = attr_name(c) or "<anon>"
        tref = attr_ref(c)
        sub = types.strip(tref)
        if sub is not None and sub["tag"] in ("DW_TAG_structure_type", "DW_TAG_union_type"):
            for path, off, sz, bo, bsz, kind, dbo in types.flat(sub, 0, name + "."):
                if off >= isize:
                    continue
                if kind.startswith("bitfield"):
                    if dbo is not None:  # bit offset from the start of the struct, in memory bit order
                        start = dbo
                    else:  # DW_AT_bit_offset: from the MSB of the storage unit at off
                        start = off * 8 + bo
                    # memory bit index -> integer bit index
                    if big_endian:
                        lo = isize * 8 - (start + bsz)
                    else:
                        lo = start if dbo is not None else (off * 8 + (sz or 4) * 8 - bo - bsz)
                    result[path] = (lo, lo + bsz - 1)
                elif sz and sz < isize and "struct" not in kind and "union" not in kind:
                    if off + sz > isize:
                        continue
                    lo = (isize - off - sz) * 8 if big_endian else off * 8
                    result[path] = (lo, lo + sz * 8 - 1)
    return (iname, isize, result) if result else None


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    ps3_path, pc_path = sys.argv[1], sys.argv[2]
    report_path = sys.argv[3] if len(sys.argv) > 3 else None
    ps3_t, ps3 = named_types(parse(ps3_path, "ppu-readelf"))
    pc_t, pc = named_types(parse(pc_path, "readelf"))
    lines = []
    problems = defaultdict(list)

    common = sorted(set(ps3) & set(pc))
    only_ps3 = sorted(set(ps3) - set(pc))
    only_pc = sorted(set(pc) - set(ps3))
    overlay_ok = set()
    bitfield_types = []
    ps3_names = {d["off"]: n for n, d in ps3.items()}
    order_items = []  # (name, ps3 flat, pc flat) checked after all overlays are known
    for name in common:
        a, b = ps3[name], pc[name]
        if any(kind.startswith("bitfield") for _, _, _, _, _, kind, _ in ps3_t.flat(a)):
            bitfield_types.append(name)
        sa, sb = attr_int(a, "DW_AT_byte_size"), attr_int(b, "DW_AT_byte_size")
        fa = [(p, o, s) for p, o, s, *_ in ps3_t.flat(a)]
        fb = [(p, o, s) for p, o, s, *_ in pc_t.flat(b)]
        if sa != sb:
            problems["SIZE"].append("%s: PS3 %s bytes, PC %s bytes" % (name, sa, sb))
        if [p for p, _, _ in fa] != [p for p, _, _ in fb]:
            da, db = [p for p, _, _ in fa], [p for p, _, _ in fb]
            if sorted(da) == sorted(db):
                order_items.append((name, ps3_t.flat(a), pc_t.flat(b)))
                problems["ORDER"].append("%s: member order differs (PS3 %s | PC %s)" % (name, ", ".join(da[:8]), ", ".join(db[:8])))
            else:
                problems["MEMBERS"].append("%s: only PS3 %s | only PC %s" % (name, sorted(set(da) - set(db))[:6],
                                                                            sorted(set(db) - set(da))[:6]))
        else:
            for (p, oa, za), (_, ob, zb) in zip(fa, fb):
                if oa != ob or za != zb:
                    problems["OFFSET"].append("%s.%s: PS3 offset %s size %s, PC offset %s size %s" % (name, p, oa, za, ob, zb))
                    break
        if a["tag"] == "DW_TAG_union_type":
            ma = union_bitmap(ps3_t, a, True)
            mb = union_bitmap(pc_t, b, False)
            if ma and mb:
                diffs = [f for f in sorted(set(ma[2]) & set(mb[2])) if ma[2][f] != mb[2][f]]
                if not diffs:
                    overlay_ok.add(name)
                if diffs:
                    problems["ENDIAN OVERLAY"].append(
                        "%s: integer '%s' (%d bytes): %s" % (name, ma[0], ma[1], "; ".join(
                            "%s PS3 bits %d-%d vs PC bits %d-%d" % (f, ma[2][f][0], ma[2][f][1], mb[2][f][0], mb[2][f][1])
                            for f in diffs[:6])))
    lines.append("Layout parity: PS3 (%s) vs PC x86-64 (%s)" % (ps3_path, pc_path))
    lines.append("types compared: %d (only PS3: %d, only PC: %d)" % (len(common), len(only_ps3), len(only_pc)))
    # a member-order difference is deliberate when the type (or the union inside it) has
    # the same integer bit mapping on both: a big-endian branch written for that purpose
    def is_deliberate(name, fa, fb):
        """Every member whose offset differs lies inside a union whose bit mapping matches PC."""
        if name in overlay_ok:
            return True
        pos_b = {e[0]: (e[1], e[2]) for e in fb}
        composite = {e[0]: ps3_names.get(e[6]) for e in fa if not e[5].startswith("bitfield") and e[6] is not None}
        for e in fa:
            if pos_b.get(e[0]) == (e[1], e[2]):
                continue
            parts = e[0].split(".")
            if not any(composite.get(".".join(parts[:k])) in overlay_ok for k in range(1, len(parts))):
                return False
        return True
    deliberate_names = {n for n, fa, fb in order_items if is_deliberate(n, fa, fb)}
    deliberate = [x for x in problems.get("ORDER", []) if x.split(":", 1)[0] in deliberate_names]
    problems["ORDER"] = [x for x in problems.get("ORDER", []) if x not in deliberate]
    problems["ORDER (deliberate big-endian layout, bit mapping matches PC)"] = deliberate
    problems["BIT-FIELD TYPES (bit order flips between big/little endian: check raw/integer access)"] = bitfield_types
    for kind in ("ENDIAN OVERLAY", "SIZE", "MEMBERS", "ORDER", "OFFSET",
                 "ORDER (deliberate big-endian layout, bit mapping matches PC)",
                 "BIT-FIELD TYPES (bit order flips between big/little endian: check raw/integer access)"):
        items = problems.get(kind, [])
        lines.append("")
        lines.append("== %s: %d ==" % (kind, len(items)))
        lines.extend("  " + x for x in items)
    if only_ps3:
        lines.append("")
        lines.append("== types only in the PS3 build: %d ==" % len(only_ps3))
        lines.extend("  " + x for x in only_ps3[:200])
    if only_pc:
        lines.append("")
        lines.append("== types only in the PC build: %d ==" % len(only_pc))
        lines.extend("  " + x for x in only_pc[:200])
    text = "\n".join(lines) + "\n"
    if report_path:
        with open(report_path, "w") as f:
            f.write(text)
    print(text if len(text) < 20000 else text[:20000] + "\n... (see the report file)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
