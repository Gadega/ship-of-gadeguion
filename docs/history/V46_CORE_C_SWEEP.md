# V46 Shipwright Core C Sweep

This check mirrors the decompiled C source selection in Shipwright's own
`soh/CMakeLists.txt`: it excludes N64 hardware I/O/OS/RMON implementations,
DMA tables, message extraction, and superseded duplicate `gu` implementations.
It adds Shipwright's portable C-side files while deferring `mixer.c`, whose Opus
dependency belongs to the later audio-backend phase.

The resulting 696 source units cover the full game-side C implementation,
including actors, effects, scenes, gameplay states, audio engine, animation,
collision, message, save, camera, and rendering command generation. They are
compiled as real PS3 PPU objects and combined with a relocatable link.

The bundled `soh/assets/**/*.h` files are upstream-generated declarations: OTR
resource path strings and symbols used while compiling. The package excludes
YAML extraction recipes, textures, models, fonts, JSON, ROMs, and O2R archives.
Runtime resources must eventually be generated locally from the user's own ROM.

Passing this check does not create an executable. Remaining undefined symbols
are expected to be primarily Shipwright/LibUltraShip C++ services and platform
bindings, which form the next integration phase.
