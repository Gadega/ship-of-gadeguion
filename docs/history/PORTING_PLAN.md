# Native PS3 port plan

## Milestone 0 — HEN hardware bring-up (this repository)

Build and install the bootstrap PKG. Acceptance criteria:

- HEN installs and launches it.
- HDMI output is stable at the console's selected resolution.
- The moving pattern is visible without corruption.
- Each face button changes color.
- Start and XMB Quit Game both exit cleanly.

No Shipwright code should be added until this passes on real hardware or RPCS3.

## Milestone 1 — Toolchain capability probe

Run `./scripts/run-toolchain-probes.sh` for the C++ library features used by
current LibUltraShip: smart pointers, filesystem, threads, atomics, unordered
containers, variants, and selected C++20 syntax. The script produces a pass/fail
report at `probe-build/toolchain-probes/report.txt` against the installed PPU
compiler and includes early probes for local compatibility helpers.

If compiler support is insufficient, prefer small compatibility changes in a
PS3 branch over downgrading the entire upstream project blindly.

Runtime V10 turned the first compatibility helper into part of the actual PS3
program. Runtime V11 attempted a zlib-backed synthetic archive loader but exposed
that the installed PPU toolchain lacks `libz`. Runtime V12 kept moving with a
stored archive and internal CRC32. Runtime V13 expands that into a named
multi-entry resource archive with lookup, text/config resources, and visible
color data loaded from the archive. Runtime V46 extracts that logic into a
small `shipwright_probe::ResourceArchive` C++17 API so the PS3 runtime starts
looking like the future LibUltraShip resource path instead of a one-off test.
Runtime V46 also adds a synthetic `.o2r` table probe with scene/object/texture
style names so the installed program starts exercising the archive access
pattern Shipwright will need. Runtime V46 adds a second `.o2r` probe backed by
a real ZIP container with stored entries, matching modern Shipwright's
ZIP/libzip archive model while keeping decompression as a separate dependency
task. Runtime V46 adds Shipwright-compatible CRC64 path hashing and hash-based
resource lookup over that ZIP-backed archive. Runtime V46 adds multi-archive
priority so a later `.o2r` can override a resource from an earlier one.
Runtime V46 also adds ZIP method 8 deflate reads using a tiny vendored STB
raw-DEFLATE decoder, avoiding the missing PS3 `libz` dependency while proving
compressed resource reads on the target runtime. Runtime V46 adds an optional
central-directory-only scanner for a user-provided `oot_user.o2r` on USB or
`/dev_hdd0/tmp`, so real Shipwright archive shape can be inspected without
loading the whole file into RAM and without distributing any game data.
Runtime V46 extends that scanner with bounded reads for `version`,
`manifest.json`, and one small resource, which mirrors the first files
LibUltraShip's archive load path requests. Runtime V46 adds a small
LibUltraShip-shaped shell with `O2rArchive`, `ArchiveManager`, `File`, path
loads, hash loads, and last-archive-wins priority, using the PS3 ZIP/deflate
reader underneath. Runtime V46 also routes the optional user-provided archive
through that shell, checking hash-to-path resolution and a bounded sample load
by CRC64 hash. Runtime V46 adds a primary resource bootstrap probe that mounts
the installed archive set through the LUS shell and validates startup-style
version, manifest, hash-to-path, hash-load, and priority behavior. Runtime V46
adds a tiny engine bootstrap probe above that layer, proving a platform-ready,
resources-ready, startup-scene-ready, tickable state sequence before any real
upstream engine loop is linked. Runtime V46 also adds a small SoH runtime bridge
with `InitOTR`-like resource init, a heap-ready placeholder, a `Main`-like
startup-scene step, five frame ticks, and a shutdown-ready state. Runtime V46
also exports upstream-style `InitOTR`, `Main`, and `DeinitOTR` C-linkage hooks
over that bridge and validates a re-init cycle. Runtime V46 adds a small
exported `ResourceMgr_*` C API backed by the PS3 archive bridge, covering file
existence, CRC64 hashing, hash-to-path resolution, load-by-name, load-by-hash,
game-version reads, and archive priority. Runtime V46 adds a tiny plain-C
upstream-style slice that calls the exported lifecycle and `ResourceMgr_*`
surfaces without depending on probe-only C++ classes. Runtime V46 adds a more
upstream-shaped minimal init slice with game-style screen globals, heap-size
bookkeeping, startup resource loading, frame stepping, and shutdown. Runtime V46
adds a shared upstream-compat declarations header consumed by plain C probes,
covering globals, lifecycle hooks, constants, and resource APIs. Runtime V46
adds upstream-style `RomFile`, `PlayState`, and `Play_LoadFile` surfaces backed
by the PS3 resource bridge. Runtime V46 adds the first PS3-owned display-list
command queue stub with clear, rectangle, and present commands. Runtime V46
also connects that queue to a tiny software framebuffer replay model and
validates clear pixels, rectangle pixels, present count, and framebuffer
checksum.

## Milestone 2 — Headless LibUltraShip shell

Create a PS3 CMake toolchain file and a dependency-minimized LibUltraShip target
that reaches `main`, logs to TTY, discovers the install directory, opens a
small test resource archive, and advances a minimal engine bootstrap tick. Use
the V46 resource bootstrap, engine bootstrap, SoH bridge, exported lifecycle
hooks, `ResourceMgr_*` API, C-side slice, minimal init slice, compat
declarations, `Play_LoadFile` shim, display-list queue stub, and software
framebuffer replay as the staging layer for this work: they already exercise
the archive-manager contract and the first renderer command path against
synthetic archives without GUI, networking, speech, audio, or desktop
renderers.

Expected installed layout:

```text
/dev_hdd0/game/OOTP30001/
├── PARAM.SFO
└── USRDIR/
    ├── EBOOT.BIN
    ├── soh.o2r
    └── oot.o2r       # user-generated; never distributed here
```

## Milestone 3 — PS3 platform services

Implement these behind LibUltraShip interfaces:

- lifecycle and XMB exit callbacks
- monotonic clock and frame pacing
- DualShock 3 input through `ioPad`
- install/save/config paths
- threaded PCM output through PSL1GHT audio APIs

## Milestone 4 — native RSX Fast3D backend

Implement the rendering contract in vertical slices:

1. clear and present;
2. untextured triangles;
3. textured triangles;
4. depth and alpha;
5. the shader combinations needed for the title screen;
6. framebuffer effects used during normal gameplay.

Use precompiled vertex/fragment programs for early builds. Log unknown Fast3D
shader IDs and add them deliberately; a runtime GLSL compiler is not a viable
first implementation on PS3.

## Milestone 5 — first game boot

Load PC-generated `soh.o2r` and `oot.o2r` supplied by the user, disable optional
enhancements, and reach the title screen at 720p/30. Then add audio and play
from file select to Kokiri Forest before optimizing.

## Milestone 6 — compatibility and performance

Profile CPU, RSX, main-memory, and VRAM pressure separately. Only after stable
vanilla gameplay should the project attempt 60 FPS, 1080p, widescreen, GUI,
mods, or enhanced texture filtering.

## Test report template

```text
Console model:
HEN/version:
Connection/resolution:
PKG installed: yes/no
Launch result:
Pattern animates: yes/no
Face buttons change color: yes/no
Start exits: yes/no
XMB Quit Game exits: yes/no
Photo/video or exact symptom:
```
