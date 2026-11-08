# Shipwright PS3 branch start

This is the first source-level plan after inspecting the local Shipwright tree.
The goal is not to make the whole upstream project compile in one pass. The
first PS3 branch should isolate the resource/runtime path that must work before
the renderer can boot the game.

## Confirmed upstream pressure points

| Area | Current upstream assumption | PS3 first branch response |
| --- | --- | --- |
| Language | Top-level C++20 and C23 | Build PS3 support in C++17 first; avoid C++20-only code until toolchain changes |
| Archives | `.o2r` is ZIP/libzip-backed, with zlib needed for normal compressed entries | Read stored ZIP entries first; port/build zlib or vendor a small inflater later |
| Files | Heavy `std::filesystem` use in `OTRGlobals.cpp` and path discovery | Route PS3 paths through `ps3_compat::Path` and simple install/save directories |
| Threads | `std::thread` appears in OTR audio and platform services | Start single-threaded; add PSL1GHT thread wrappers deliberately |
| Graphics | Non-Darwin desktop path defines `ENABLE_OPENGL` | Add an explicit PS3 platform branch so OpenGL/ImGui are not selected by accident |
| Game init | `soh/src/code/main.c` calls `InitOTR`, then full engine init | Write a PS3-specific minimal OTR init path before attempting full `Main(0)` |

## First branch shape

1. Add a `PS3` platform option/toolchain path rather than relying on the
   desktop default branch.
2. Compile a minimized LibUltraShip/resource target without ImGui, OpenGL,
   SDL, networking, speech, MPQ, GUI debugger, or desktop audio.
3. Open an archive from the installed package directory and resolve named
   resources by path.
4. Only after that reaches real hardware, connect `.o2r` parsing and the first
   Fast3D/RSX renderer slice.

## V46 runtime proof

Runtime V46 uses `shipwright_probe::ResourceArchive`, a tiny C++17 resource
archive API shaped like the LibUltraShip resource layer we need first:

- `Open(path)`
- `Has(name)`
- `Read(name, bytes)`
- `ReadText(name, string)`

Runtime V46 also adds `ShipCrc64Name`, matching upstream `CRC64(path)`, and a
hash-based lookup path through the ZIP-backed `.o2r` probe. This mirrors the
way `ArchiveManager` maps resource path hashes to archive entries.
V46 adds a small multi-archive manager as well: later archives override earlier
ones while base-only entries remain visible. It also adds ZIP method 8 deflated
entry reads through a vendored STB raw-DEFLATE decoder, which proves compressed
`.o2r` resource access without relying on a missing PS3 `libz`. V46 then adds
an optional user archive scanner for `oot_user.o2r` on USB or `/dev_hdd0/tmp`;
it reads the ZIP tail and central directory only, then logs real-world entry
counts and compression methods. V46 adds bounded reads for `version`,
`manifest.json`, and one small resource, allowing the PS3 runtime to test the
same first archive files that upstream `Archive::Load()` asks for. V46 also
adds a small LibUltraShip-shaped shell that exposes `O2rArchive`,
`ArchiveManager`, and `File` concepts over the working PS3 ZIP reader. The
optional user archive path now goes through that shell as well, including
hash-to-path resolution and a small hash-based resource load. V46 introduces a
primary resource bootstrap probe that uses the shell for installed archive
mounting and startup resource validation. V46 also introduces an engine
bootstrap probe that sits above the resource bootstrap and advances through
platform-ready, resources-ready, startup-scene-ready, and tickable states. V46
adds `soh_runtime_bridge` above that, mirroring the upstream order of
`InitOTR`, heap preparation, `Main`, first scene load, frame ticks, and
shutdown readiness while keeping all data synthetic. V46 also exports
upstream-style `InitOTR`, `Main`, and `DeinitOTR` C-linkage hooks over that
bridge and validates a re-init cycle. V46 exports a small `ResourceMgr_*` C API
over the PS3 archive bridge, so the next upstream C files can ask for resource
existence, version, priority, name hashes, and raw loads without depending on
the probe-only C++ classes. V46 adds a plain C-side slice that calls those
exported lifecycle and resource APIs, proving the next upstream-derived C file
can link against the PS3 bridge. V46 adds a more upstream-shaped minimal init
slice with game-style globals, heap-size bookkeeping, startup resource loading,
frame stepping, and shutdown. V46 adds a shared upstream-compat declarations
header consumed by plain C probes, so future slices can include one stable
PS3-owned surface instead of redeclaring hooks by hand. V46 adds upstream-style
`RomFile`, `PlayState`, and `Play_LoadFile` surfaces backed by the PS3 resource
bridge. V46 adds a PS3-owned display-list command queue stub with clear,
rectangle, and present commands. V46 also replays that queue into a tiny
software framebuffer model and validates the expected clear/rect/present
result.

The demo still uses synthetic archives and original assets only. It now includes
a synthetic `.o2r` table probe and a ZIP-backed synthetic `.o2r` with
scene/object/texture-style names, but no Nintendo ROM data, extracted game
assets, or Shipwright-generated `.o2r` content is included.

If V46 passes on the PS3 and a user-generated `.o2r` indexes and samples
cleanly, the next useful riskier build is connecting the queue replay to the
actual PS3 framebuffer as a visible renderer precursor. It should still avoid
bundling any user ROM or extracted Nintendo assets.
