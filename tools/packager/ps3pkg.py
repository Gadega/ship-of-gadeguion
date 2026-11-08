# SPDX-License-Identifier: MIT
"""Debug PS3 PKG writer: a Python port of PSL1GHT's tools/sfo_pkg/pkg.c (pack mode).

Produces the same bytes as `pkg --contentid <id> <dir> <out>` (verified against the
PSL1GHT tool). HEN installs these debug packages; no console keys are involved.
Files are given in memory as {"USRDIR/EBOOT.BIN": bytes, ...} plus directory names.
"""

import hashlib
import struct

TYPE_NPDRMSELF = 0x1
TYPE_RAW = 0x3
TYPE_DIRECTORY = 0x4
TYPE_OVERWRITE_ALLOWED = 0x80000000
PKG_MAGIC = 0x7F504B47
SELF_MAGIC = bytes([0x53, 0x43, 0x45, 0x00, 0x00, 0x00, 0x00, 0x02, 0x80])


def _key_to_context(key16):
    return bytearray(key16[0:8] + key16[0:8] + key16[8:16] + key16[8:16] + bytes(32))


def _crypt(ctx, data, progress=None):
    """XOR stream cipher: SHA1(ctx) per 16-byte block, 64-bit counter at ctx[0x38:0x40]."""
    out = bytearray(len(data))
    counter = int.from_bytes(ctx[0x38:0x40], "big")
    prefix = bytes(ctx[0:0x38])
    sha1 = hashlib.sha1
    total = len(data)
    step = 1 << 20
    next_report = step
    for offset in range(0, total, 16):
        h = sha1(prefix + counter.to_bytes(8, "big")).digest()
        block = data[offset:offset + 16]
        out[offset:offset + len(block)] = bytes(a ^ b for a, b in zip(h, block))
        counter = (counter + 1) & 0xFFFFFFFFFFFFFFFF
        if progress is not None and offset >= next_report:
            progress(offset, total)
            next_report += step
    ctx[0x38:0x40] = counter.to_bytes(8, "big")
    return bytes(out)


def _align16(n):
    return (n + 0x0F) & ~0x0F


def _collect(files, dirs):
    """Same order as pkg.c: per directory level, files (sorted) then subdirectories (sorted)."""
    entries = []

    def walk(prefix):
        names = sorted(n for n in files if n.rsplit("/", 1)[0] == prefix.rstrip("/") and "/" in n
                       or (prefix == "" and "/" not in n))
        # files directly in this folder
        level_files = sorted(n for n in files if (n.rsplit("/", 1)[0] if "/" in n else "") == prefix)
        level_dirs = sorted(d for d in dirs if (d.rsplit("/", 1)[0] if "/" in d else "") == prefix)
        del names
        for name in level_files:
            size = len(files[name])
            if name == "USRDIR/EBOOT.BIN":
                size = ((size - 0x30 + 63) & ~63) + 0x30
                flags = TYPE_OVERWRITE_ALLOWED | TYPE_NPDRMSELF
            else:
                flags = TYPE_OVERWRITE_ALLOWED | TYPE_RAW
            entries.append({"name": name, "size": size, "flags": flags})
        for d in level_dirs:
            entries.append({"name": d, "size": 0, "flags": TYPE_OVERWRITE_ALLOWED | TYPE_DIRECTORY})
            walk(d)

    walk("")
    return entries


def _npdrm_self_digest_offset(data):
    """Offset of the NPD metadata block in an NPDRM SELF, or None (pkg.c logic)."""
    if len(data) < 0x68 or data[:9] != SELF_MAGIC:
        return None
    app_info_off = struct.unpack_from(">Q", data, 40)[0]
    digest_off = struct.unpack_from(">Q", data, 88)[0]
    app_type = 0
    if app_info_off + 0x18 <= len(data):
        app_type = struct.unpack_from(">I", data, app_info_off + 12)[0]
    found = False
    doff = digest_off
    while doff + 0x10 <= len(data):
        dtype, dsize = struct.unpack_from(">II", data, doff)
        is_next = struct.unpack_from(">Q", data, doff + 8)[0]
        if dtype == 3:
            found = True
            break
        doff += dsize
        if is_next != 1:
            break
    doff += 0x10
    return doff if (app_type == 8 and found) else None


def build_pkg(files, dirs, contentid, progress=None):
    """files: {"PARAM.SFO": bytes, "USRDIR/EBOOT.BIN": bytes, ...}; dirs: ["USRDIR", ...]."""
    entries = _collect(files, dirs)
    count = len(entries)
    cid = contentid.encode("ascii")[:0x30]

    # Name offsets, then data offsets (after all names).
    cur = 0x20 * count
    for e in entries:
        e["name_off"] = cur
        cur += _align16(len(e["name"]))
    for e in entries:
        e["data_off"] = cur
        if (e["flags"] & 0xFF) != TYPE_DIRECTORY:
            cur += _align16(e["size"])

    buf = bytearray()
    for e in entries:
        buf += struct.pack(">IIQQII", e["name_off"], len(e["name"]), e["data_off"], e["size"], e["flags"], 0)
    for e in entries:
        name = e["name"].encode("ascii")
        buf += name + bytes(_align16(len(name)) - len(name))
    file_desc_length = len(buf)

    for e in entries:
        if (e["flags"] & 0xFF) == TYPE_DIRECTORY:
            continue
        data = files[e["name"]]
        digest_off = _npdrm_self_digest_offset(data)
        if digest_off is not None:
            file_sha1 = hashlib.sha1(data).digest()
            meta = bytearray(0x80)
            struct.pack_into(">IIII", meta, 0, 0x4E504400, 1, 3, 1)
            meta[16:16 + len(cid)] = cid
            for j in range(0x10):
                meta[0x40 + j] = file_sha1[j]
                meta[0x50 + j] = (~file_sha1[j]) & 0xFF
                ns = meta[0x50 + j]
                meta[0x60 + j] = ((1 if j == 0x0F else 0) ^ ns ^ 0xAA) & 0xFF
                meta[0x70 + j] = 0
            buf += data[:digest_off] + meta
            if digest_off + 0x80 <= len(data):
                buf += data[digest_off + 0x80:]
        else:
            buf += data
        padded = _align16(e["size"])
        if len(data) < padded:
            buf += bytes(padded - len(data))

    hdr = bytearray(0x80)
    struct.pack_into(">IIIIIIQQQ", hdr, 0, PKG_MAGIC, 0x01, 0xC0, 0x05, 0x80, count,
                     len(buf) + 0x1A0, 0x140, len(buf))
    hdr[48:48 + len(cid)] = cid

    qa = hashlib.sha1()
    for e in entries:
        if (e["flags"] & 0xFF) != TYPE_DIRECTORY:
            qa.update(files[e["name"]])
    qa.update(bytes(hdr[0:0x30]))
    qa.update(bytes(hdr[0x60:0x80]))
    qa.update(bytes(hdr[0x60:0x80]))
    qa.update(bytes(hdr[0x60:0x70]))
    qa.update(bytes(buf[:file_desc_length]))
    hdr[0x60:0x70] = qa.digest()[:0x10]

    klic_ctx = _key_to_context(bytes(hdr[0x60:0x70]))
    klic_ctx[0x38:0x40] = (0xFFFFFFFFFFFFFFFF).to_bytes(8, "big")
    hdr[0x70:0x80] = _crypt(klic_ctx, bytes(16))

    meta_hdr = struct.pack(">IIIIIIIIIIIHHIIIHH", 1, 4, 3, 2, 4, 5, 3, 4, 0x0E, 4, 8, 0, 0, len(buf), 5, 4,
                           0x1061, 0)
    assert len(meta_hdr) == 0x40

    out = bytearray()
    out += hdr
    hdr_sha = hashlib.sha1(bytes(hdr)).digest()
    out += hdr_sha[3:19]
    meta_sha16 = hashlib.sha1(meta_hdr).digest()[3:19]
    enc1 = _crypt(_key_to_context(meta_sha16), bytes(0x30))
    enc2 = _crypt(_key_to_context(hdr_sha[3:19]), enc1)
    out += enc2
    out += meta_hdr
    out += meta_sha16
    out += enc1
    out += _crypt(_key_to_context(bytes(hdr[0x60:0x70])), bytes(buf), progress)
    out += bytes(0x60)
    return bytes(out)


def build_pkg_from_folder(folder, contentid):
    """Convenience for testing against the PSL1GHT tool: pack a folder on disk."""
    import os
    files, dirs = {}, []
    for root, dnames, fnames in os.walk(folder):
        rel = os.path.relpath(root, folder).replace(os.sep, "/")
        rel = "" if rel == "." else rel
        for d in dnames:
            dirs.append((rel + "/" + d) if rel else d)
        for f in fnames:
            name = (rel + "/" + f) if rel else f
            with open(os.path.join(root, f), "rb") as handle:
                files[name] = handle.read()
    return build_pkg(files, dirs, contentid)


if __name__ == "__main__":
    import sys
    data = build_pkg_from_folder(sys.argv[1], sys.argv[2])
    with open(sys.argv[3], "wb") as handle:
        handle.write(data)
