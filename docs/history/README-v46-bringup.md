# OoT PS3 native-port runtime v46

## V46: Real Boot Compilation Gate

Run `bash scripts/check-real-boot.sh` in WSL. Share the short console output,
or `boot-check/summary.txt`. No console install is needed for this step.

This attempts actual PPU object compilation of upstream `game.c`, `graph.c`,
`z_title.c`, and `z_opening.c`. It deliberately excludes the earlier
`upstream_compat` scan stubs and refuses to silently use a desktop compiler.
Failures are expected and identify dependencies that must genuinely be ported.
Successful object compilation is not linking, game boot, or hardware execution.

The optional PKG remains the V45 synthetic demonstration with a version bump.
Its scene, actor, and display-list formats are custom tests, not the original
game's scene/actor/display-list implementation. The Makefile does not yet link
upstream game sources into the PKG. More colored test bars do not establish
that the game is closer to booting. The text below describes accumulated demo
features; references to this version do not mean they were all added in V46.

This repository is the first, deliberately small milestone toward a native PS3
port of Ship of Harkinian. It targets a retail PS3 running HEN through the
open-source PSL1GHT SDK.

The current program is **not the game yet**. It is a hardware bring-up test that
proves the toolchain, SELF/PKG packaging, RSX video output, DualShock input,
clean XMB exit path, basic runtime logging, bundled text asset loading, larger
binary asset loading, and a small reusable software renderer that draws a
packaged sprite texture. It also includes toolchain probes for the C/C++
features needed by later Shipwright/LibUltraShip work and starts a tiny C++17
compatibility layer for PS3 paths. Runtime V46 links C++17 compatibility code
into the installed PS3 program, routes named resource lookup through
`shipwright_probe::ResourceArchive`, adds a synthetic `.o2r` table probe, and
adds a read-only ZIP-backed `.o2r` probe for stored entries. Current Shipwright
uses ZIP/libzip for `.o2r`, so this is the first parser shaped like the real
container while avoiding zlib until the PS3 dependency story is settled.
Runtime V46 also adds Shipwright-compatible CRC64 path hashing and hash-based
resource lookup. Runtime V46 adds a tiny archive manager that mounts a base
archive plus an override archive and gives the later archive priority, mirroring
Shipwright's virtual filesystem behavior. Runtime V46 also reads ZIP method 8
deflated entries through a small vendored STB raw-DEFLATE decoder, so the probe
now covers both stored and compressed `.o2r` resources without depending on a
missing PS3 `libz`. Runtime V46 adds an optional user `.o2r` scanner that looks
for `oot_user.o2r` on USB or `/dev_hdd0/tmp`, reads only the ZIP tail and
central directory, and logs entry/method counts without shipping or requiring
Nintendo-owned data. Runtime V46 also performs bounded reads of `version`,
`manifest.json`, and one small resource from that optional archive, matching the
first files LibUltraShip reads during archive load while limiting each sample to
small payloads. Runtime V46 adds a small LibUltraShip-shaped shell with
`O2rArchive`, `ArchiveManager`, `File`, `LoadFile(path)`, `LoadFile(hash)`, and
last-archive-wins priority over `.o2r` mounts. Runtime V46 also routes the
optional `oot_user.o2r` test through that shell, including hash-to-path
resolution and sample loading by hash. Runtime V46 adds a primary resource
bootstrap probe that mounts installed archives through the LUS shell and
validates startup-style resource access. Runtime V46 also adds a tiny
engine-bootstrap state machine layered on that resource bootstrap: platform
ready, resources ready, startup scene ready, and tickable. Runtime V46 adds a
small SoH runtime bridge with `InitOTR`-like resource init, a heap-ready
placeholder, a `Main`-like startup-scene step, five frame ticks, and a
shutdown-ready state. Runtime V46 also exports and probes upstream-style
`InitOTR`, `Main`, and `DeinitOTR` C-linkage hooks over that bridge, including
a re-init cycle. Runtime V46 also exports a small `ResourceMgr_*` C API backed
by the PS3 archive bridge, covering file existence, path hashing, hash-to-path,
load-by-name, load-by-hash, game version, and priority checks. Runtime V46 adds
a tiny C-side upstream slice that calls `InitOTR`, `Main`, `DeinitOTR`, and
`ResourceMgr_*` from a plain C translation unit. Runtime V46 adds a more
upstream-shaped minimal init slice with game-style screen globals, heap-size
bookkeeping, startup resource load, frame stepping, and shutdown. Runtime V46
adds a shared upstream-compat declarations header consumed by plain C probes,
covering globals, lifecycle hooks, constants, and resource APIs. Runtime V46
adds upstream-style `RomFile`, `PlayState`, and `Play_LoadFile` surfaces backed
by the PS3 resource bridge. Runtime V46 adds the first PS3-owned display-list
command queue stub with clear, rectangle, and present commands. Runtime V46
also replays that queued clear/rect/present stream into a tiny software
framebuffer model and verifies the resulting pixels and checksum. Runtime V46
also carries a filtered real Shipwright/LibUltraShip source snapshot and a
`make upstream-attempt` collision harness that pushes the PS3-oriented compiler
directly into upstream source files, records per-file logs, and keeps going
after failures. Keeping this milestone independent of a default failing build
makes display/input/filesystem/resource/engine/renderer failures diagnosable
while still letting us collect real upstream blockers.

## Project layout

- `ps3_platform/` contains reusable PS3-specific services:
  video framebuffer setup, software drawing, and runtime logging.
- `demo_runtime/` contains the current visible test application and asset
  loading checks.
- `pkgfiles/USRDIR/assets/` contains packaged test assets that are copied into
  the PKG.
- `scripts/run-toolchain-probes.sh` compiles small PPU test programs and writes
  a report under `probe-build/toolchain-probes/`.
- `ps3_compat/` contains focused C++17 replacements for desktop-only standard
  library assumptions.
- `shipwright_probe/` contains the first reusable C++17 resource archive layer
  shaped for later LibUltraShip/Shipwright integration.
- `upstream/shipwright/` contains a filtered source-only Shipwright snapshot
  for the V46 collision attempt. It excludes ROMs, generated asset folders, and
  git history.
- `upstream_compat/` contains temporary compile stubs for desktop libraries so
  the collision attempt can reach deeper PS3/source incompatibilities.
- `third_party/` contains small open-source compatibility dependencies used by
  the runtime. It must never contain ROMs or Nintendo assets.

## What the bootstrap does

- Starts at the PS3's currently selected output resolution.
- Uses two RSX display buffers with vertical-sync flips.
- Displays a moving diagnostic marker and four input blocks.
- Lights the blocks for Square, Cross, Circle, and Triangle.
- Shows a green top bar when `USRDIR/assets/test.txt` loads successfully.
- Shows a cyan second bar when `USRDIR/assets/blob.bin` loads and checksums.
- Shows a magenta third bar when `USRDIR/assets/sprite.spg` loads.
- Shows an orange fourth bar when the C++17 PS3 compatibility self-test passes.
- Shows a light-blue fifth bar when a packaged synthetic archive opens,
  parses, verifies CRC, and matches expected content.
- Shows a white sixth bar when a synthetic `.o2r` resource table opens,
  resolves named scene/object/texture entries, verifies CRC, and matches
  expected content.
- Shows a mint seventh bar when a ZIP-backed synthetic `.o2r` opens, indexes
  central-directory entries, reads stored payloads, verifies CRC, and matches
  expected content.
- Shows a pink eighth bar when a Shipwright-style CRC64 path hash resolves and
  reads a resource from the ZIP-backed `.o2r`.
- Shows a gold ninth bar when the archive manager mounts two `.o2r` files,
  keeps base-only resources visible, and lets the later archive override an
  earlier resource.
- Shows a blue tenth bar when a ZIP-backed synthetic `.o2r` containing deflated
  entries indexes, inflates, verifies CRCs, and matches expected content.
- Shows a purple eleventh bar if an optional user-provided `oot_user.o2r` is
  found and its ZIP central directory indexes successfully. It stays dark gray
  when no user archive is present.
- Shows a green twelfth bar if the optional user-provided archive's
  `manifest.json` is found and read through the PS3 ZIP/inflate path.
- Shows a pale-blue thirteenth bar when the LibUltraShip-shaped shell mounts
  two `.o2r` files, reads `version` and `manifest.json`, loads a scene by CRC64
  hash, and confirms override priority.
- Shows a lime fourteenth bar if an optional user-provided `oot_user.o2r`
  mounts through the LibUltraShip-shaped shell and a small resource loads by
  CRC64 hash.
- Shows an amber fifteenth bar when the primary resource bootstrap mounts the
  installed archive set through the LUS shell and validates startup resources.
- Shows a yellow-green sixteenth bar when the engine bootstrap reaches a
  tickable startup-scene state on top of the resource bootstrap.
- Shows a bright blue seventeenth bar when the SoH runtime bridge reaches
  init/resource/heap/main/scene/tick/shutdown-ready phases.
- Shows an orange eighteenth bar when exported upstream-style `InitOTR`, `Main`,
  and `DeinitOTR` hooks initialize, tick, deinitialize, and reinitialize.
- Shows a green nineteenth bar when exported `ResourceMgr_*` calls resolve and
  load startup resources by name and hash while coexisting with the lifecycle
  hooks.
- Shows a pink twentieth bar when a plain C upstream-style slice calls the
  lifecycle and `ResourceMgr_*` APIs successfully.
- Shows a lime twenty-first bar when the minimal init slice sets game-style
  globals, tracks heap size, loads the startup resource, advances frames, and
  shuts down.
- Shows a gold twenty-second bar when plain C code consumes the shared
  upstream-compat declarations header successfully.
- Shows a mint twenty-third bar when a `Play_LoadFile`-style shim loads the
  startup resource into a `PlayState`.
- Shows a violet twenty-fourth bar when the display-list command queue accepts
  and processes clear/rect/present commands after resource loading.
- Shows a blue twenty-fifth bar when the queued clear/rect/present commands
  replay into a software framebuffer model with the expected pixels.
- Shows three small color chips loaded by name from the archive.
- Draws an animated sprite from the packaged texture file.
- Shows a red bar if any bundled asset test fails.
- Exits with Start or with "Quit Game" from the XMB.
- Attempts to write a log at `/dev_hdd0/tmp/oot_ps3_runtime.log`.

## Build

Install the current [ps3dev environment](https://github.com/ps3dev/ps3dev), then
export the paths expected by PSL1GHT:

```sh
export PS3DEV=/usr/local/ps3dev
export PSL1GHT="$PS3DEV"
export PATH="$PATH:$PS3DEV/bin:$PS3DEV/ppu/bin:$PS3DEV/spu/bin"
./scripts/check-env.sh
make
make pkg
```

Expected outputs are `oot_ps3_runtime_v46.elf`, `oot_ps3_runtime_v46.self`, and one
or more `oot_ps3_runtime_v46*.pkg` files, depending on the SDK revision.

## Toolchain probes

Before pulling in larger upstream code, run the probe suite:

```sh
./scripts/run-toolchain-probes.sh
```

It checks whether the installed PPU GCC can compile and link the C++ standard
library features most likely to affect a Shipwright-derived port. The report is
written to `probe-build/toolchain-probes/report.txt`.

## Full Upstream Attempt

Runtime V46 starts the reckless path: it includes a filtered upstream source
snapshot and a harness that syntax-compiles real Shipwright/LibUltraShip files
with PS3-oriented flags. By default, V46 uses a boot-focused source set instead
of compiling every enhancement and randomizer file.

```sh
make upstream-attempt
```

The report is written to `upstream-attempt/report.txt`, with one log per source
file under `upstream-attempt/logs/`. This command is expected to fail right now;
the point is to replace guessing with a concrete blocker list. In local testing,
the V46 boot-focused pass reduced the V35 broad result from 91 passing and 1290
failing files to 204 passing and 96 failing files over 300 boot/resource/title
oriented files. To make a quick sample run instead of the whole focused source
set:

```sh
OOT_PS3_UPSTREAM_LIMIT=40 make upstream-attempt
```

To reproduce the broad V35-style scan:

```sh
OOT_PS3_UPSTREAM_MODE=full make upstream-attempt
```

The upstream snapshot excludes ROMs, generated assets, and `.o2r` files. The
temporary `upstream_compat` headers are not a port of SDL, ImGui, spdlog,
tinyxml2, nlohmann-json, libzip, or C++20 library pieces; they are collision
stubs so the compiler can reach deeper files before we decide what to replace,
vendor, or remove. The most important remaining boot-focused failures now point
at generated asset headers, C++20 comparison syntax, and async/resource-manager
surfaces.

Runtime V46 adds a synthetic actor/object table referenced by the synthetic
title scene descriptor. It is still legal test data, not Nintendo content, but
the boot path now loads scene metadata from the packaged O2R, validates the
display-list and actor-table paths, reads phase/FPS metadata, loads the
referenced display list, loads simple actor rows, converts display commands into
a bounded frame queue, and replays both queue and actor state to the PS3
framebuffer as a larger title-like panel. This is still not the game intro; it
is the next visible proof that scene selection, resource loading, actor/object
spawn, display-list decode, command submission, and graphics replay are becoming
one boot path.

## Test on HEN

1. Copy the generated PKG to a FAT32 USB drive or transfer it by your normal
   HEN workflow.
2. Enable HEN.
3. Install it with **Package Manager > Install Package Files**.
4. Launch **OoT PS3 Runtime V46** from the XMB.
5. Verify that face buttons light their blocks and Start returns to XMB.
6. Verify that the top bar is green, the second bar is cyan, the third bar is
   magenta, the fourth bar is orange, the fifth bar is light blue, the sixth
   bar is white, the seventh bar is mint, the eighth bar is pink, the ninth bar
   is gold, the tenth bar is blue, the thirteenth bar is pale blue, and the
   fifteenth bar is amber, the sixteenth bar is yellow-green, the seventeenth
   bar is bright blue, the eighteenth bar is orange, and the nineteenth bar is
   green, the twentieth bar is pink, the twenty-first bar is lime, and the
   twenty-second bar is gold, the twenty-third bar is mint, the twenty-fourth
   bar is violet, and the twenty-fifth bar is blue. The eleventh, twelfth, and
   fourteenth bars may remain dark gray unless you provide an optional
   `oot_user.o2r`.
7. Verify that a small sprite moves near the lower part of the screen.
8. Verify that a small panel appears near the lower-right area. Its border
   should be green when the packaged synthetic O2R resource bootstrap path
   passes. Inside it, several short horizontal bars represent archive mount,
   manifest, scene load, and priority override state, plus one moving pulse.
9. Verify that a larger synthetic title-like panel appears near the center of
   the screen. It should have a white/green border, gold and cyan bars, a
   flashing block, and a moving green strip. That panel is loaded from
   `scenes/title/title_display_list.ps3gfx` inside the packaged `.o2r`.
10. If you have FTP or a file manager, check for `/dev_hdd0/tmp/oot_ps3_runtime.log`.

Optional real-archive scanner test:

1. Put a user-generated Shipwright archive at the root of a USB drive as
   `oot_user.o2r`, or copy it to `/dev_hdd0/tmp/oot_user.o2r`.
2. Launch the runtime again.
3. A purple eleventh bar means the PS3 indexed the central directory.
4. A green twelfth bar means the PS3 read `manifest.json` from the archive.
5. A lime fourteenth bar means the LibUltraShip-shaped shell mounted the archive
   and loaded one small resource by CRC64 hash.
6. Check `/dev_hdd0/tmp/oot_ps3_runtime.log` for entry counts, stored/deflated
   counts, unsupported method count, largest low-32-bit uncompressed size, the
   CRC64 hash of the first entry name, version-file value, manifest sample hash,
   one small-resource sample hash, and the LUS user-archive result line.

Record whether the console is HDMI or analog, its selected resolution, and any
symptom (black screen, immediate return, freeze, color corruption). That result
determines the next engineering step.

## Legal boundary

This repository contains no Nintendo code, ROM, keys, or copyrighted game
assets. Ship of Harkinian requires the user to supply a legally obtained,
supported OoT ROM and extracts assets locally. The V46 upstream
snapshot excludes `soh/assets`, `roms`, and generated `.o2r` output. Do not
commit ROMs, `.o2r` files, save data, SDK binaries, or proprietary Sony
material.

## Next work

See [docs/PORTING_PLAN.md](docs/PORTING_PLAN.md),
[docs/SHIPWRIGHT_AUDIT.md](docs/SHIPWRIGHT_AUDIT.md), and
[docs/TOOLCHAIN_PROBES.md](docs/TOOLCHAIN_PROBES.md). The next code milestone is
to replace the synthetic title command decoder with the first real SoH title/opening
resource structures, while keeping the PKG test cheap and visible.

## License

The bootstrap is GPL-3.0-or-later because its low-level screen setup is adapted
from PSL1GHT's GPL-3.0 `videoTest` sample. See `COPYING`.
