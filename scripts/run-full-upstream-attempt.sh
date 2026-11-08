#!/usr/bin/env bash
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM="$ROOT/upstream/shipwright"
OUT="$ROOT/upstream-attempt"
LOGS="$OUT/logs"
MANIFEST="$OUT/manifest"

mkdir -p "$LOGS" "$MANIFEST"
find "$LOGS" -type f -name '*.log' -delete

if [ ! -d "$UPSTREAM/soh" ] || [ ! -d "$UPSTREAM/libultraship" ]; then
    echo "missing upstream Shipwright snapshot at $UPSTREAM"
    exit 1
fi

CC_BIN="${CC:-ppu-gcc}"
CXX_BIN="${CXX:-ppu-g++}"
if ! command -v "$CC_BIN" >/dev/null 2>&1; then
    CC_BIN="gcc"
fi
if ! command -v "$CXX_BIN" >/dev/null 2>&1; then
    CXX_BIN="g++"
fi
if command -v ppu-g++ >/dev/null 2>&1 && [ "$CXX_BIN" = "g++" ]; then
    echo "note: ppu-g++ exists but CXX resolved to g++; set CXX=ppu-g++ for PS3-accurate diagnostics" >&2
fi

INCLUDES=(
    "-I$ROOT/upstream_compat/include"
    "-I$UPSTREAM/soh"
    "-I$UPSTREAM/soh/include"
    "-I$UPSTREAM/soh/soh"
    "-I$UPSTREAM/soh/src"
    "-I$UPSTREAM/soh/src/overlays"
    "-I$UPSTREAM/libultraship/include"
    "-I$UPSTREAM/libultraship/src"
    "-I$UPSTREAM/torch/src"
    "-I$ROOT/ps3_compat/include"
    "-I$ROOT/shipwright_probe/include"
)

DEFINES=(
    "-D__PS3__"
    "-DOOT_PS3_FULL_UPSTREAM_ATTEMPT=1"
    "-DCONTROLLERBUTTONS_T=uint32_t"
    "-DGBI_UCODE=F3DEX_GBI_2"
)

COMMON_FLAGS=("-Wall" "-Wextra" "-Wno-unused-parameter" "-Wno-missing-field-initializers" "-fsyntax-only")
CFLAGS=("-std=gnu11" "${COMMON_FLAGS[@]}" "-include" "ps3_upstream_prefix.h")
CXXFLAGS=("-std=gnu++17" "${COMMON_FLAGS[@]}" "-include" "ps3_upstream_prefix.h" "-include" "unordered_map")
MODE="${OOT_PS3_UPSTREAM_MODE:-boot}"

if [ "$MODE" = "full" ]; then
    SEARCH_ROOTS=("$UPSTREAM/soh/src" "$UPSTREAM/soh/soh" "$UPSTREAM/libultraship/src")
else
    SEARCH_ROOTS=(
        "$UPSTREAM/libultraship/src/fast"
        "$UPSTREAM/libultraship/src/ship/resource"
        "$UPSTREAM/libultraship/src/ship/utils"
        "$UPSTREAM/soh/src/boot"
        "$UPSTREAM/soh/src/code"
        "$UPSTREAM/soh/src/overlays/gamestates/ovl_title"
        "$UPSTREAM/soh/src/overlays/gamestates/ovl_opening"
        "$UPSTREAM/soh/soh"
    )
fi

find "${SEARCH_ROOTS[@]}" \
    -type f \( -name '*.c' -o -name '*.cpp' \) \
    ! -path '*/soh/assets/*' \
    ! -path '*/roms/*' \
    ! -path '*/build*/*' \
    ! -path '*/Enhancements/*' \
    ! -path '*/randomizer/*' \
    ! -path '*/ObjectExtension/*' \
    ! -path '*/Notification/*' \
    ! -path '*/SohGui/*' \
    ! -path '*/window/gui/*' \
    ! -path '*/ship/controller/*' \
    ! -path '*/libultraship/controller/*' \
    ! -path '*/ship/audio/*' \
    ! -path '*/libultraship/bridge/audio*' \
    ! -path '*/libultraship/bridge/controller*' \
    ! -path '*/libultraship/bridge/window*' \
    ! -path '*/libultraship/bridge/gfxdebugger*' \
    ! -path '*/libultraship/bridge/scripting*' \
    ! -path '*/Network/*' \
    ! -path '*/Extractor/*' \
    ! -path '*/speechsynthesizer/*' \
    ! -path '*/backends/gfx_direct3d*' \
    ! -path '*/backends/gfx_dxgi*' \
    ! -path '*/backends/gfx_metal*' \
    ! -path '*/backends/gfx_opengl*' \
    ! -path '*/backends/gfx_sdl2*' \
    | sort > "$MANIFEST/all_sources.txt"

grep -E '\.c$' "$MANIFEST/all_sources.txt" > "$MANIFEST/c_sources.txt" || true
grep -E '\.cpp$' "$MANIFEST/all_sources.txt" > "$MANIFEST/cpp_sources.txt" || true

cat > "$OUT/report.txt" <<EOF
OoT PS3 Runtime V46 full upstream attempt
Generated: $(date -u +"%Y-%m-%dT%H:%M:%SZ")
Root: $ROOT
Upstream: $UPSTREAM
CC: $CC_BIN
CXX: $CXX_BIN
Mode: $MODE

This is intentionally aggressive. It syntax-compiles real Shipwright and
LibUltraShip source with the PS3-oriented compiler settings, records per-file
logs, and keeps going after failures.

Default mode is boot-focused: it excludes ROMs, generated asset directories,
enhancements, randomizer, GUI, networking, extractor tools, speech synthesizers,
desktop input/audio, and desktop graphics backends. Set
OOT_PS3_UPSTREAM_MODE=full for the broad collision scan.

EOF

printf "%-7s %-6s %s\n" "Lang" "Result" "Source" | tee -a "$OUT/report.txt"
printf "%-7s %-6s %s\n" "----" "------" "------" | tee -a "$OUT/report.txt"

pass=0
fail=0
limit="${OOT_PS3_UPSTREAM_LIMIT:-0}"
seen=0

compile_one() {
    local lang="$1"
    local src="$2"
    local base
    base="$(echo "${src#$UPSTREAM/}" | tr '/.' '__')"
    local log="$LOGS/$base.log"
    local result="FAIL"

    if [ "$lang" = "C" ]; then
        "$CC_BIN" "${CFLAGS[@]}" "${DEFINES[@]}" "${INCLUDES[@]}" "$src" >"$log" 2>&1
    else
        "$CXX_BIN" "${CXXFLAGS[@]}" "${DEFINES[@]}" "${INCLUDES[@]}" "$src" >"$log" 2>&1
    fi

    if [ $? -eq 0 ]; then
        result="PASS"
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
    fi
    printf "%-7s %-6s %s\n" "$lang" "$result" "${src#$UPSTREAM/}" | tee -a "$OUT/report.txt"
}

while IFS= read -r src; do
    [ -n "$src" ] || continue
    seen=$((seen + 1))
    if [ "$limit" != "0" ] && [ "$seen" -gt "$limit" ]; then
        break
    fi
    case "$src" in
        *.c) compile_one "C" "$src" ;;
        *.cpp) compile_one "C++" "$src" ;;
    esac
done < "$MANIFEST/all_sources.txt"

{
    echo
    echo "Summary: $pass passed, $fail failed, $seen attempted"
    echo
    echo "Most common first diagnostics:"
    for log in "$LOGS"/*.log; do
        [ -s "$log" ] || continue
        first="$(grep -m 1 -E 'fatal error:|error:' "$log" || true)"
        [ -n "$first" ] && printf '%s\n' "$first"
    done | sed 's/^[[:space:]]*//' | sort | uniq -c | sort -nr | head -n 25
    echo
    echo "Detailed logs: $LOGS"
} | tee -a "$OUT/report.txt"

if [ "$fail" -gt 0 ]; then
    exit 2
fi
exit 0
