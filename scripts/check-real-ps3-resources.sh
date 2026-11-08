#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM="$ROOT/upstream/shipwright"
BASE="$ROOT/ps3-os-check/shipwright-core-ps3-os.o"
BASE_UNDEFINED="$ROOT/ps3-os-check/undefined.txt"
OUT="$ROOT/ps3-resource-check"
CXX_BIN="${OOT_PS3_CXX:-ppu-g++}"
LD_BIN="${OOT_PS3_LD:-ppu-ld}"
NM_BIN="${OOT_PS3_NM:-ppu-nm}"
mkdir -p "$OUT/objects" "$OUT/logs"
REPORT="$OUT/summary.txt"

main() {
echo 'V46 native PS3 streaming resource bridge check (NOT a game executable)'
for tool in "$CXX_BIN" "$LD_BIN" "$NM_BIN"; do command -v "$tool" >/dev/null 2>&1 || { echo "STOP: missing $tool"; exit 1; }; done
if [ ! -f "$BASE" ] || [ ! -f "$BASE_UNDEFINED" ]; then echo 'STOP: run bash scripts/check-real-ps3-os.sh first.'; exit 1; fi
machine="$("$CXX_BIN" -dumpmachine)"
case "$machine" in powerpc64*ps3*) ;; *) echo "STOP: compiler target is $machine, not PS3 PPU."; exit 1;; esac
echo "Compiler: $CXX_BIN ($machine)"
cxx_run=("$CXX_BIN")
if [ "${OOT_PS3_NO_CCACHE:-0}" != 1 ] && command -v ccache >/dev/null 2>&1; then
    cxx_run=(ccache "$CXX_BIN")
    echo 'Compiler cache enabled for PS3 resource bridge'
fi

flags=(-std=gnu++17 -O2 -mminimal-toc -mcpu=cell -Wall -Wextra -Werror -Wno-unknown-pragmas
    -D__PS3__ -DF3DEX_GBI_2 -DGBI_UCODE=F3DEX_GBI_2 -DIS_BIGENDIAN
    -DCONTROLLERBUTTONS_T=uint32_t -DLOG_LEVEL_GAME_PRINTS=6
    "-I$ROOT/shipwright_probe/include" "-I$ROOT/third_party/stb"
    "-I$ROOT/upstream_compat/include"
    -include "$ROOT/upstream_compat/include/ps3_upstream_prefix.h"
    "-I$UPSTREAM/soh" "-I$UPSTREAM/soh/include" "-I$UPSTREAM/soh/src"
    "-I$UPSTREAM/soh/soh" "-I$UPSTREAM/soh/assets"
    "-I$UPSTREAM/libultraship/include" "-I$UPSTREAM/libultraship/src")
cvars=(RANDOMIZER_ENHANCEMENT=gRandoEnhancements RANDOMIZER_SETTING=gRandoSettings COSMETIC=gCosmetics
    AUDIO=gAudioEditor CHEAT=gCheats ENHANCEMENT=gEnhancements SETTING=gSettings WINDOW=gOpenWindows
    TRACKER=gTrackers DEVELOPER_TOOLS=gDeveloperTools GENERAL=gGeneral REMOTE=gRemote
    GAMEPLAY_STATS=gGameplayStats TIME_DISPLAY=gTimeDisplay)
for cvar in "${cvars[@]}"; do flags+=("-DCVAR_PREFIX_${cvar%%=*}=\"${cvar#*=}\""); done

sources=(ps3_platform/source/shipwright_resource_ps3.cpp shipwright_probe/source/ship_hash.cpp third_party/stb/stb_zlib.cpp)
objects=()
for source in "${sources[@]}"; do
    safe="${source//\//_}"; object="$OUT/objects/${safe%.cpp}.o"; log="$OUT/logs/${safe%.cpp}.log"
    if ! "${cxx_run[@]}" "${flags[@]}" -c "$ROOT/$source" -o "$object" >"$log" 2>&1; then
        echo "BLOCKED compiling: $source"; grep -m1 -E 'fatal error:|error:' "$log" || true; exit 2
    fi
    objects+=("$object")
done
echo "Streaming resource objects compiled: ${#objects[@]}/${#sources[@]}"
if ! "$LD_BIN" -r -o "$OUT/shipwright-core-ps3-resources.o" "$BASE" "${objects[@]}" >"$OUT/logs/link.log" 2>&1; then
    echo 'BLOCKED: resource relocatable link failed'; grep -m3 -E 'multiple definition|undefined reference|error:' "$OUT/logs/link.log" || true; exit 3
fi
"$NM_BIN" -u "$OUT/shipwright-core-ps3-resources.o" | awk 'NF { print $NF }' | sort -u > "$OUT/undefined.txt"
comm -23 "$BASE_UNDEFINED" "$OUT/undefined.txt" > "$OUT/resolved.txt"
echo 'PS3 resource relocatable link: OK'
echo "Resource symbols resolved: $(wc -l < "$OUT/resolved.txt")"
echo "Current unresolved frontier: $(wc -l < "$OUT/undefined.txt")"
echo 'First resolved resource services:'
head -n 35 "$OUT/resolved.txt"
echo 'Share only this summary. Full diagnostics stay in ps3-resource-check/.'
}

main 2>&1 | tee "$REPORT"
