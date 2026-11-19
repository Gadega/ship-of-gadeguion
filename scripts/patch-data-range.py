#!/usr/bin/env python3
# Save states (ps3_savestate.c gOotPs3GameData): writes into the ELF the address ranges
# of the game code's global variables (.data/.bss of the decomp objects), taken from the
# linker map. A save copies all of them and a load writes them back. Engine/port globals (mixer, Fast3D, PS3 platform, libc)
# are left out: they hold live state of this run.
import re, struct, sys
elf, mapfile = sys.argv[1], sys.argv[2]
MAX = 8192
GAME = re.compile(r'^(src_(code|overlays|buffers|libultra)_.*|game-(?!ps3).*)\.o$')
# Thread/OS/engine state inside the game objects: graph loop, game state handover, pad,
# audio manager thread, scheduler/IRQ/fault threads, DMA manager, the render thread's
# matrix stack, Map Select (unused), the zelda arena lock.
SKIP = re.compile(r'^(game-(graph|main|pad-mgr|audio-mgr|state-init|arena)|src_code_(irqmgr|sched|fault|fault_drawer|'
                  r'sys_matrix|padmgr|audioMgr|main|graph|game)|src_overlays_gamestates_ovl_select_.*)\.o$')
lines = open(mapfile).read().split('\n')
ranges = []
i = 0
while i < len(lines):
    l = lines[i]
    m = re.match(r'^ (\.(?:data|bss)(?:\.\S+)?)\s*$', l)
    if m and i + 1 < len(lines):
        l = ' ' + m.group(1) + ' ' + lines[i + 1].strip()
        i += 1
    m = re.match(r'^ \.(?:data|bss)(?:\.\S+)?\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)', l)
    if m:
        a, s, o = int(m.group(1), 16), int(m.group(2), 16), m.group(3)
        if s and a >= 0x10000000 and GAME.match(o.split('/')[-1]) and not SKIP.match(o.split('/')[-1]):
            ranges.append((a, a + s))
    i += 1
ranges.sort()
merged = []
for a, b in ranges:
    if merged and a <= merged[-1][1]:
        merged[-1][1] = max(merged[-1][1], b)
    else:
        merged.append([a, b])
if not merged or len(merged) > MAX:
    sys.exit("patch-data-range: %d ranges (max %d)" % (len(merged), MAX))
data = bytearray(open(elf, "rb").read())
magic = b"OOTPS3GAMEDATA!!"
pos = data.find(magic)
if pos < 0 or data.find(magic, pos + 1) >= 0:
    sys.exit("patch-data-range: marker missing or not unique")
blob = struct.pack(">IIII", len(merged), 0, 0, 0) + b"".join(struct.pack(">II", a, b - a) for a, b in merged)
data[pos:pos + len(blob)] = blob
open(elf, "wb").write(data)
print("patch-data-range: %d game data ranges, %d bytes" % (len(merged), sum(b - a for a, b in merged)))
