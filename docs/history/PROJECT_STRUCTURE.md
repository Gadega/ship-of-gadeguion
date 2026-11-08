# Project structure

This checkpoint separates reusable PS3 runtime code from the current hardware
demo. The visual output should match Runtime V7, but the source layout is now
closer to a real port and includes host-side toolchain probes.

## `ps3_platform/`

Reusable PS3-specific code. This should become the platform boundary used by
future OoT integration work.

- `rsx_screen`: framebuffer setup, buffer allocation, and flips.
- `software_renderer`: simple CPU-side drawing into the framebuffer.
- `runtime_log`: lightweight file logging.

## `demo_runtime/`

The current diagnostic application. It proves video, input, asset loading,
binary loading, texture loading, and drawing. Demo-only asset formats and tests
belong here until they graduate into platform code.

## `pkgfiles/`

Files copied into the PKG through PSL1GHT's `PKGFILES` hook. Current assets are
synthetic test files only. Do not put ROMs, extracted Nintendo assets, `.o2r`
files, SDK binaries, keys, or proprietary Sony files here.

## `scripts/`

Environment checks and host-side development helpers. `run-toolchain-probes.sh`
builds disposable PPU C/C++ snippets under `probe-build/toolchain-probes/` and writes
the compatibility report for the next porting decision.

## `ps3_compat/`

Small C++17 compatibility helpers for upstream code that assumes desktop C++20
or missing standard-library features. Runtime V46 starts this with a path helper
that can replace the subset of `std::filesystem::path` needed for early archive
and install-directory work.

## `shipwright_probe/`

Small resource and archive probes shaped toward the future LibUltraShip
integration. Runtime V46 includes an optional user `.o2r` central-directory
scanner for real archive diagnostics without loading the entire archive, plus
bounded reads for `version`, `manifest.json`, and one small resource. It also
contains the first LibUltraShip-shaped shell classes used by the runtime.
Runtime V46 also includes a user-archive probe that exercises the shell against
an externally supplied `.o2r`, plus a resource bootstrap probe that treats the
shell as the primary installed archive service. It also includes an engine
bootstrap probe that turns those resource checks into a small startup state
sequence: platform ready, resources ready, startup scene ready, and tickable.
Runtime V46 adds `soh_runtime_bridge`, a PS3-owned shim shaped after the
upstream `InitOTR`/heap/`Main`/frame/shutdown order while still using synthetic
archives and no game data. Runtime V46 also adds `soh_upstream_hooks_probe`,
which exports upstream-style `InitOTR`, `Main`, and `DeinitOTR` symbols over the
bridge so later builds can replace hook bodies with minimized upstream code.
Runtime V46 adds `soh_resource_api_probe`, exporting a small `ResourceMgr_*`
C API that upstream C files can call before the full LibUltraShip resource
manager is linked. Runtime V46 adds `soh_c_side_slice_probe.c`, a plain C
integration slice that calls those exported lifecycle and resource APIs.
Runtime V46 adds `soh_minimal_init_slice_probe.c`, a more upstream-shaped C
slice with game-style globals, heap-size bookkeeping, resource lookup, frame
stepping, and shutdown. Runtime V46 adds `soh_upstream_compat.h` and
`soh_upstream_decls_probe.c`, giving plain C slices a shared declarations
surface for globals, lifecycle hooks, constants, and resource APIs. Runtime V46
adds `soh_play_loadfile_probe.c`, providing upstream-style `RomFile`,
`PlayState`, and `Play_LoadFile` surfaces over the PS3 resource bridge.
Runtime V46 adds `soh_gfx_queue_probe.c`, a tiny display-list command queue
stub with clear, rectangle, and present commands. Runtime V46 also adds
`soh_gfx_soft_bridge_probe.c`, which replays that queue into a small software
framebuffer model and verifies the clear/rect/present result before a visible
PS3 renderer is wired to it.

## `third_party/`

Small open-source dependencies or staged upstream source snapshots. Runtime V46
uses STB's raw-DEFLATE decoder for ZIP method 8 `.o2r` entries. Nothing here
should require Nintendo-owned data to build.
