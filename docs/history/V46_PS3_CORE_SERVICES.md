# V46 Functional PS3 Core Services

`shipwright_core_services.cpp` replaces the first desktop service dependency
with a small platform-owned implementation suitable for early PS3 boot.

- Fixed-capacity CVar storage supports integer, float, string, RGBA, and RGB
  values; registration, overwrite, lookup defaults, copy, clear, and prefix
  clear are implemented.
- It does not allocate from the heap and does not require JSON or nlohmann.
- CVar load/save entry points currently preserve runtime state only; disk-backed
  configuration belongs to the filesystem phase.
- Window state supplies the fixed PS3 display dimensions and fullscreen state.
- Timing uses `gettimeofday` for microsecond counters and `time` for Unix time.
- Controller rumble is enabled at the policy layer; actual pad actuation remains
  in the PS3 controller backend.

The CVar behavior was exercised locally for defaults, registration, mutation,
copy, clearing, prefix clearing, string lifetime, colors, and timing. The WSL
check verifies that PS3 newlib supplies the required time interfaces and that
the object links into the real Shipwright C/C++ cluster.
