#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="$ROOT_DIR/probe-build/toolchain-probes"
LOG_DIR="$OUT_DIR/logs"
REPORT="$OUT_DIR/report.txt"
CC="${CC:-ppu-gcc}"
CXX="${CXX:-ppu-g++}"
COMMON_FLAGS="-mcpu=cell -O2"
INCLUDE_FLAGS=""
LIBRARY_FLAGS=""

if [ -n "${PS3DEV:-}" ]; then
    INCLUDE_FLAGS="-I$PS3DEV/ppu/include -I$PS3DEV/portlibs/ppu/include"
    LIBRARY_FLAGS="-L$PS3DEV/ppu/lib -L$PS3DEV/portlibs/ppu/lib"
fi

mkdir -p "$LOG_DIR"
: >"$REPORT"

pass_count=0
fail_count=0
prereq_failed=0

write_header() {
    {
        printf 'OoT PS3 Runtime V46 toolchain probe report\n'
        printf 'Generated: %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
        printf 'Root: %s\n' "$ROOT_DIR"
        printf 'CC: %s\n' "$CC"
        printf 'CXX: %s\n' "$CXX"
        printf 'PS3DEV: %s\n' "${PS3DEV:-"(unset)"}"
        printf 'PSL1GHT: %s\n' "${PSL1GHT:-"(unset)"}"
        printf '\n'
        printf '%-24s %-6s %s\n' 'Probe' 'Result' 'Notes'
        printf '%-24s %-6s %s\n' '-----' '------' '-----'
    } >>"$REPORT"
}

record_result() {
    local name="$1"
    local result="$2"
    local notes="$3"

    printf '%-24s %-6s %s\n' "$name" "$result" "$notes" >>"$REPORT"
    if [ "$result" = "PASS" ]; then
        pass_count=$((pass_count + 1))
    else
        fail_count=$((fail_count + 1))
    fi
}

run_cmd() {
    local name="$1"
    shift
    local log="$LOG_DIR/$name.log"

    {
        printf '$'
        printf ' %q' "$@"
        printf '\n\n'
        "$@"
    } >"$log" 2>&1
}

probe_c() {
    local name="$1"
    local flags="$2"
    local libs="$3"
    local source="$OUT_DIR/$name.c"
    local output="$OUT_DIR/$name.elf"

    cat >"$source"
    if run_cmd "$name" "$CC" $COMMON_FLAGS $INCLUDE_FLAGS $flags "$source" -o "$output" $LIBRARY_FLAGS $libs; then
        record_result "$name" "PASS" "linked"
    else
        record_result "$name" "FAIL" "see logs/$name.log"
    fi
}

probe_cpp() {
    local name="$1"
    local flags="$2"
    local libs="$3"
    local source="$OUT_DIR/$name.cpp"
    local output="$OUT_DIR/$name.elf"

    cat >"$source"
    if run_cmd "$name" "$CXX" $COMMON_FLAGS -I"$ROOT_DIR/ps3_compat/include" $INCLUDE_FLAGS $flags "$source" -o "$output" $LIBRARY_FLAGS $libs; then
        record_result "$name" "PASS" "linked"
    else
        record_result "$name" "FAIL" "see logs/$name.log"
    fi
}

write_header

if ! command -v "$CC" >/dev/null 2>&1; then
    record_result "ppu-gcc" "FAIL" "compiler not found in PATH"
    prereq_failed=1
fi
if ! command -v "$CXX" >/dev/null 2>&1; then
    record_result "ppu-g++" "FAIL" "compiler not found in PATH"
    prereq_failed=1
fi

if [ "$prereq_failed" -ne 0 ]; then
    {
        printf '\nSummary: %u passed, %u failed\n' "$pass_count" "$fail_count"
        printf '\nInstall ps3dev and make sure ppu-gcc and ppu-g++ are in PATH.\n'
    } >>"$REPORT"
    cat "$REPORT"
    exit 127
fi

probe_c "c_gnu11" "-std=gnu11" "" <<'EOF'
#include <stdint.h>
typedef struct Probe { uint32_t a; uint32_t b; } Probe;
static uint32_t mix(Probe p) { return (p.a << 4) ^ p.b; }
int main(void) {
    Probe p = { 0x12u, 0x34u };
    return mix(p) == 0x114u ? 0 : 1;
}
EOF

probe_cpp "cpp_basic" "-std=gnu++17" "" <<'EOF'
struct Probe {
    constexpr Probe(int value) : value(value) {}
    int value;
};
int main() {
    constexpr Probe p(7);
    return p.value == 7 ? 0 : 1;
}
EOF

probe_cpp "cpp17_memory" "-std=gnu++17" "" <<'EOF'
#include <memory>
int main() {
    auto unique = std::make_unique<int>(41);
    auto shared = std::make_shared<int>(*unique + 1);
    return *shared == 42 ? 0 : 1;
}
EOF

probe_cpp "cpp17_containers" "-std=gnu++17" "" <<'EOF'
#include <string>
#include <unordered_map>
#include <vector>
int main() {
    std::vector<std::string> names = {"kokiri", "goron", "zora"};
    std::unordered_map<std::string, int> values;
    for (int i = 0; i < (int)names.size(); ++i) {
        values[names[(unsigned)i]] = i;
    }
    return values["zora"] == 2 ? 0 : 1;
}
EOF

probe_cpp "cpp17_variant_optional" "-std=gnu++17" "" <<'EOF'
#include <optional>
#include <variant>
int main() {
    std::optional<int> value = 5;
    std::variant<int, float> tagged = *value;
    return std::get<int>(tagged) == 5 ? 0 : 1;
}
EOF

probe_cpp "cpp20_language" "-std=gnu++20" "" <<'EOF'
#include <type_traits>
template <typename T>
concept Integral = std::is_integral_v<T>;
constexpr int add_one(Integral auto value) {
    return value + 1;
}
int main() {
    return add_one(41) == 42 ? 0 : 1;
}
EOF

probe_cpp "cpp_atomic" "-std=gnu++17" "" <<'EOF'
#include <atomic>
int main() {
    std::atomic<int> value{1};
    value.fetch_add(41);
    return value.load() == 42 ? 0 : 1;
}
EOF

probe_cpp "cpp_exceptions_rtti" "-std=gnu++17" "" <<'EOF'
#include <stdexcept>
#include <typeinfo>
struct Base { virtual ~Base() = default; };
struct Derived : Base {};
int main() {
    try {
        Derived d;
        Base* b = &d;
        if (typeid(*b) != typeid(Derived)) {
            return 1;
        }
        throw std::runtime_error("ok");
    } catch (const std::runtime_error&) {
        return 0;
    }
}
EOF

probe_cpp "cpp_thread" "-std=gnu++17" "-pthread" <<'EOF'
#include <thread>
int main() {
    int value = 0;
    std::thread worker([&value]() { value = 42; });
    worker.join();
    return value == 42 ? 0 : 1;
}
EOF

probe_cpp "cpp_filesystem" "-std=gnu++17" "" <<'EOF'
#include <filesystem>
int main() {
    std::filesystem::path p("/dev_hdd0/game/OOTP30046/USRDIR");
    return p.filename() == "USRDIR" ? 0 : 1;
}
EOF

probe_cpp "ps3_compat_path" "-std=gnu++17" "" <<'EOF'
#include <ps3_compat/path.h>
#include <string>
int main() {
    oot::ps3_compat::Path root("/dev_hdd0/game/OOTP30046/USRDIR/");
    oot::ps3_compat::Path asset = root.join("assets").join("oot.o2r");
    if (root.filename() != "USRDIR") {
        return 1;
    }
    if (asset.parent_path().filename() != "assets") {
        return 2;
    }
    return asset.string() == "/dev_hdd0/game/OOTP30046/USRDIR/assets/oot.o2r" ? 0 : 3;
}
EOF

probe_c "zlib_link" "-std=gnu11" "-lz" <<'EOF'
#include <zlib.h>
int main(void) {
    return zlibVersion() != 0 ? 0 : 1;
}
EOF

{
    printf '\nSummary: %u passed, %u failed\n' "$pass_count" "$fail_count"
    printf '\nA failure here is a porting signal, not necessarily a broken PS3 setup.\n'
} >>"$REPORT"

cat "$REPORT"
