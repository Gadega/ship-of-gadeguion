# Toolchain probes

Runtime V46 includes a host-side probe suite for the installed ps3dev PPU compiler.
The goal is to learn which modern C/C++ features are available before importing
or adapting a larger Shipwright/LibUltraShip slice.

Run it from WSL after exporting the normal PS3 environment:

```sh
export PS3DEV=/usr/local/ps3dev
export PSL1GHT="$PS3DEV"
export PATH="$PATH:$PS3DEV/bin:$PS3DEV/ppu/bin:$PS3DEV/spu/bin"
./scripts/check-env.sh
./scripts/run-toolchain-probes.sh
```

The script writes:

```text
probe-build/toolchain-probes/report.txt
probe-build/toolchain-probes/logs/*.log
```

The report is the important file. The individual logs are there when a probe
fails and we need compiler/linker detail.

## What is tested

| Probe | Why it matters |
| --- | --- |
| `c_gnu11` | Current PS3 runtime code and compatibility C helpers. |
| `cpp_basic` | Minimal C++ link path through `ppu-g++`. |
| `cpp17_memory` | Smart pointers used throughout modern C++ code. |
| `cpp17_containers` | `vector`, `string`, and unordered containers. |
| `cpp17_variant_optional` | Resource/config code often uses tagged values and optional results. |
| `cpp20_language` | Basic C++20 syntax support, separate from library support. |
| `cpp_atomic` | Thread-safe state used by engines and platform layers. |
| `cpp_exceptions_rtti` | Confirms whether default exception and RTTI paths are usable. |
| `cpp_thread` | Likely blocker for desktop-style threading abstractions. |
| `cpp_filesystem` | Likely blocker; PS3 paths may need a small compatibility layer. |
| `ps3_compat_path` | Confirms the first local path compatibility helper builds as C++17. |
| `zlib_link` | Resource archives commonly need compression support. |

Some failures are expected and useful. A failed `filesystem` or `thread` probe
does not stop the port; it tells us which compatibility layer to write first.
