# V46 Provider Wave

This update adds the upstream `F3DEX_GBI_2` compiler definition used by the real
Shipwright CMake build. It restores `gameplay_field_keep.h`, which contains
aligned OTR resource-path strings but no extracted texture pixels or geometry.

The provider wave deliberately uses Shipwright's `soh/stubs.c` for its existing
desktop N64 compatibility behavior. These compatibility functions are enough to
resolve/link parts of the upstream engine but are not all acceptable final PS3
implementations. Threads, timing, controller input, graphics submission, audio,
and filesystem behavior will be replaced or connected to PSL1GHT as the closure
reaches them.
