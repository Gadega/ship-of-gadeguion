#!/usr/bin/env python3
"""Dump the scene/room commands of an O2R scene the way the PS3 loader reads them.

Usage: o2r_scene_dump.py <oot.o2r> <scene path> [layer]
  e.g. o2r_scene_dump.py oot.o2r scenes/shared/link_home_scene/link_home_scene 5
Byte layouts follow Shipwright's resource/importer/scenecommand factories.
"""
import struct
import sys
import zipfile

HEADER_SIZE = 0x40

NAMES = {
    0x00: "SpawnList", 0x01: "ActorList", 0x02: "CsCamera", 0x03: "CollisionHeader", 0x04: "RoomList",
    0x05: "Wind", 0x06: "EntranceList", 0x07: "SpecialFiles", 0x08: "RoomBehavior", 0x09: "Undefined9",
    0x0A: "Mesh", 0x0B: "ObjectList", 0x0C: "LightList", 0x0D: "PathList", 0x0E: "TransitionActors",
    0x0F: "LightSettings", 0x10: "Time", 0x11: "Skybox", 0x12: "SkyboxDisables", 0x13: "ExitList",
    0x14: "End", 0x15: "Sound", 0x16: "Echo", 0x17: "Cutscene", 0x18: "AlternateHeaders", 0x19: "Misc",
}


class Reader:
    def __init__(self, data):
        self.d, self.o = data, 0

    def u(self, fmt):
        v = struct.unpack_from("<" + fmt, self.d, self.o)
        self.o += struct.calcsize("<" + fmt)
        return v if len(v) > 1 else v[0]

    def s(self):
        n = self.u("I")
        v = self.d[self.o:self.o + n].decode(errors="replace")
        self.o += n
        return v


def dump(z, path, layer, indent="", depth=0):
    data = z.read(path)[HEADER_SIZE:]
    r = Reader(data)
    count = r.u("I")
    print(f"{indent}{path}: {count} commands")
    for _ in range(count):
        cid = r.u("I")
        name = NAMES.get(cid, f"?{cid:#x}")
        if cid in (0x00, 0x01):
            n = r.u("I")
            entries = [r.u("Hhhhhhhh") for _ in range(n)]
            print(f"{indent}  {name}: {n}")
            for e in entries:
                print(f"{indent}    actor={e[0]:#05x} pos=({e[1]},{e[2]},{e[3]}) rot=({e[4]},{e[5]},{e[6]}) params={e[7] & 0xFFFF:#06x}")
        elif cid in (0x03, 0x17):
            print(f"{indent}  {name}: {r.s()}")
        elif cid == 0x04:
            n = r.u("I")
            rooms = []
            for _ in range(n):
                rooms.append(r.s())
                r.u("ii")
            print(f"{indent}  {name}: {rooms}")
        elif cid == 0x06:
            n = r.u("I")
            print(f"{indent}  {name}: {[r.u('bb') for _ in range(n)]}")
        elif cid in (0x0B, 0x13):
            n = r.u("I")
            print(f"{indent}  {name}: {[hex(r.u('H')) for _ in range(n)]}")
        elif cid == 0x0C:
            n = r.u("I")
            print(f"{indent}  {name}: {[r.u('BhhhBBBBh') for _ in range(n)]}")
        elif cid == 0x0D:
            n = r.u("I")
            print(f"{indent}  {name}: {[r.s() for _ in range(n)]}")
        elif cid == 0x0E:
            n = r.u("I")
            print(f"{indent}  {name}: {n}")
            for _ in range(n):
                print(f"{indent}    {r.u('BBBBhhhhhH')}")
        elif cid == 0x0F:
            n = r.u("I")
            r.o += 22 * n
            print(f"{indent}  {name}: {n} settings")
        elif cid == 0x0A:
            dataByte, mtype = r.u("bb")
            if mtype != 1:
                polys = r.u("B")
                for _ in range(polys):
                    r.u("b")  # polygon type
                    if mtype == 2:
                        r.u("hhhh")  # pos, unk_06
                    r.s()
                    r.s()
                print(f"{indent}  {name}: type={mtype} polys={polys}")
                continue
            else:
                fmt = r.u("B")
                r.s()
                r.s()
                n = r.u("I")
                print(f"{indent}  {name}: type=1 (prerendered) format={fmt} images={n}")
                for _ in range(n):
                    unk0, iid = r.u("HB")
                    img = r.s()
                    unkc, tlut, w, h, fm, sz, mode0, tlutCount = r.u("IIHHBBHH")
                    print(f"{indent}    image id={iid} {img} {w}x{h} fmt={fm} siz={sz}")
                    if fmt == 1:
                        break
                rest = r.d[r.o:]
                print(f"{indent}    (remaining {len(rest)} bytes not decoded)")
            return
        elif cid == 0x18:
            n = r.u("I")
            headers = [r.s() for _ in range(n)]
            print(f"{indent}  {name}: {headers}")
            if layer and depth == 0 and layer - 1 < len(headers) and headers[layer - 1]:
                print(f"{indent}  -> layer {layer}:")
                dump(z, headers[layer - 1], layer, indent + "    ", depth + 1)
                return
        elif cid == 0x14:
            print(f"{indent}  End")
            return
        else:
            size = {0x02: 5, 0x05: 4, 0x07: 3, 0x08: 5, 0x10: 3, 0x11: 4, 0x12: 2, 0x15: 3, 0x16: 1, 0x19: 5}.get(cid)
            if size is None:
                print(f"{indent}  {name}: unknown size; stopping")
                return
            raw = r.d[r.o:r.o + size]
            r.o += size
            print(f"{indent}  {name}: {raw.hex()}")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    z = zipfile.ZipFile(sys.argv[1])
    layer = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    dump(z, sys.argv[2], layer)


if __name__ == "__main__":
    main()
