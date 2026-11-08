# Shipwright-to-PS3 audit

## Decision

Use current Shipwright/LibUltraShip as the game-side base, but introduce PS3 in
layers. A direct "add PS3 to the existing CMake file and fix errors" attempt
would mix toolchain, dependencies, platform APIs, rendering, and memory failures
into one opaque problem.

Shipwright already carries console-specific branches for Nintendo Switch and
Wii U (`CMAKE_SYSTEM_NAME` values `NintendoSwitch` and `CafeOS`). The PS3 target
should follow that structure with `CMAKE_SYSTEM_NAME=PS3` and an explicit
`SHIP_ENABLE_PS3` feature set.

## Confirmed architecture

- Shipwright is currently C++20 and C23.
- The game is built on LibUltraShip and its Fast3D display-list interpreter.
- LibUltraShip separates rendering (`GfxRenderingAPI`) from window/event
  management (`GfxWindowBackend`), which gives the PS3 port a clean seam.
- Existing graphics backends are Direct3D 11, OpenGL, and Metal. PSL1GHT does
  not provide one of those backends; PS3 needs a native RSX implementation.
- Shipwright generates/loads `.o2r` archives made from a user-supplied ROM.
  Console releases already expect archives generated on a PC, which is exactly
  the right model for PS3.

## Main blockers, in order

1. **Compiler and standard library:** V8 proved the installed PPU GCC can link
   C++17 smart pointers, containers, optional/variant, atomics, exceptions,
   RTTI, and zlib, but not C++20 concepts, `std::thread`, or
   `std::filesystem`. The PS3 branch should use C++17 plus focused
   compatibility helpers. Runtime V10 linked the first helper into the real PS3
   SELF to prove C++17 compatibility code can live inside the installed app.
   Runtime V11 exposed missing PPU `libz`; Runtime V12 kept the resource-loader
   slice moving with a stored archive and internal CRC32 while zlib became a
   dependency task instead of a blocker. Runtime V13 adds named multi-entry
   archive lookup, closer to the eventual `.o2r` access pattern. Runtime V46
   splits that loader into a reusable `shipwright_probe::ResourceArchive` layer
   so the next step can swap the synthetic archive parser for an `.o2r` table
   probe without rewriting the runtime. Runtime V46 adds that synthetic `.o2r`
   table probe and proves the runtime can resolve scene/object/texture-style
   resource names from an installed package file. Runtime V46 also adds a
   ZIP-backed stored-entry `.o2r` parser, matching the current upstream
   `O2rArchive` container direction without requiring libzip yet. Runtime V46
   adds Shipwright-compatible CRC64 path hashing and hash-based resource lookup,
   matching `Archive::IndexFile` and `ArchiveManager::LoadFile(uint64_t)`.
   Runtime V46 adds a tiny multi-archive manager and proves later archives can
   override earlier ones while base-only entries remain readable. Runtime V46
   also adds a tiny engine bootstrap probe above the resource bootstrap, so the
   installed app now proves a startup-scene-ready, tickable state before the
   real upstream loop is linked. Runtime V46 adds a small SoH runtime bridge
   above that probe, using an upstream-shaped `InitOTR`/heap/`Main`/scene/tick
   sequence with synthetic archives. Runtime V46 exports upstream-style
   `InitOTR`, `Main`, and `DeinitOTR` hooks over that sequence, making the next
   replacement point explicit. Runtime V46 adds a small exported `ResourceMgr_*`
   C API over the PS3 archive bridge, making raw resource calls available to
   upcoming upstream C slices. Runtime V46 adds a plain C slice that calls those
   exported lifecycle and resource APIs successfully. Runtime V46 adds a more
   upstream-shaped minimal init slice with game-style globals, heap-size
   bookkeeping, startup resource loading, frame stepping, and shutdown.
   Runtime V46 adds a shared upstream-compat declarations header consumed by
   plain C probes. Runtime V46 adds upstream-style `RomFile`, `PlayState`, and
   `Play_LoadFile` surfaces over the PS3 resource bridge. Runtime V46 adds a
   PS3-owned display-list command queue stub with clear, rectangle, and present
   commands. Runtime V46 also replays that queue into a tiny software
   framebuffer model and validates the resulting clear pixels, rectangle
   pixels, present count, and checksum.
2. **Dependency closure:** `.o2r` is ZIP/libzip-backed. Runtime V46 adds a tiny
   read-only parser for stored ZIP entries so resource indexing can move forward
   before zlib/libzip are available on PPU. SDL2, audio codecs, GUI, networking,
   speech, and debugging features should be optional until the first boot.
3. **Platform shell:** paths, clock, threads, controller, audio, and lifecycle.
4. **RSX renderer:** implement `GfxRenderingAPI` and `GfxWindowBackend` for a
   fixed 720p-first path. Dynamic Fast3D shader combinations are the hardest
   part; start with a small precompiled shader cache, then expand from captured
   shader IDs.
5. **Memory:** PS3 has separate, constrained CPU and RSX memory pools. Avoid
   loading optional GUI/debug/network systems and stream textures/resources.

## Features to disable for first game image

- ImGui menus and graphics debugger
- SDL_net, crowd control, and multiplayer/network features
- text-to-speech and platform speech synthesizers
- custom music codecs not required by vanilla gameplay
- ROM extraction on console (generate `.o2r` on PC)
- MSAA, framebuffer readback, depth extraction, and enhanced texture filters

These are bring-up cuts, not permanent product decisions.

## First renderer contract

The minimal RSX backend must support shader selection, texture allocation and
upload, sampler wrap/filter state, viewport/scissor, depth test/write, alpha,
triangle draws, double buffering, and color/depth clears. Optional framebuffer
features can initially return conservative fallbacks as long as vanilla scenes
do not call them.

The first visual target is the title screen at 1280x720, 30 FPS, with vanilla
assets and no ImGui. Audio can remain silent for that milestone.
