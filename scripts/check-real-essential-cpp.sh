#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM="$ROOT/upstream/shipwright"
CORE="$ROOT/core-c-check/shipwright-core-c.o"
BASE_UNDEFINED="$ROOT/core-c-check/undefined.txt"
OUT="$ROOT/essential-cpp-check"
CXX_BIN="${OOT_PS3_CXX:-ppu-g++}"
LD_BIN="${OOT_PS3_LD:-ppu-ld}"
NM_BIN="${OOT_PS3_NM:-ppu-nm}"
mkdir -p "$OUT/logs" "$OUT/objects"
REPORT="$OUT/summary.txt"

main() {
echo 'V46 essential Shipwright C++ bridge check (NOT a game executable)'
for tool in "$CXX_BIN" "$LD_BIN" "$NM_BIN"; do
    command -v "$tool" >/dev/null 2>&1 || { echo "STOP: missing $tool"; exit 1; }
done
if [ ! -f "$CORE" ] || [ ! -f "$BASE_UNDEFINED" ]; then
    echo 'STOP: run bash scripts/check-real-core-c.sh first.'
    exit 1
fi
machine="$("$CXX_BIN" -dumpmachine)"
case "$machine" in
    powerpc64*ps3*) ;;
    *) echo "STOP: compiler target is $machine, not PS3 PPU."; exit 1 ;;
esac
echo "Compiler: $CXX_BIN ($machine)"

flags=(-std=gnu++17 -O2 -fno-strict-aliasing -mminimal-toc -mcpu=cell -ffunction-sections -fdata-sections
    -D__PS3__ -DF3DEX_GBI_2 -DGBI_UCODE=F3DEX_GBI_2
    -DCONTROLLERBUTTONS_T=uint32_t -DLOG_LEVEL_GAME_PRINTS=6
    "-I$UPSTREAM/soh" "-I$UPSTREAM/soh/include" "-I$UPSTREAM/soh/src"
    "-I$UPSTREAM/soh/soh" "-I$UPSTREAM/soh/assets"
    "-I$UPSTREAM/libultraship/include" "-I$UPSTREAM/libultraship/src")
cvars=(RANDOMIZER_ENHANCEMENT=gRandoEnhancements RANDOMIZER_SETTING=gRandoSettings
    COSMETIC=gCosmetics AUDIO=gAudioEditor CHEAT=gCheats ENHANCEMENT=gEnhancements
    SETTING=gSettings WINDOW=gOpenWindows TRACKER=gTrackers
    DEVELOPER_TOOLS=gDeveloperTools GENERAL=gGeneral REMOTE=gRemote
    GAMEPLAY_STATS=gGameplayStats TIME_DISPLAY=gTimeDisplay)
for cvar in "${cvars[@]}"; do
    flags+=("-DCVAR_PREFIX_${cvar%%=*}=\"${cvar#*=}\"")
done

sources=(
    soh/soh/Enhancements/game-interactor/GameInteractor_Hooks.cpp
    soh/soh/Enhancements/game-interactor/GameInteractor_State.cpp
    soh/soh/Enhancements/randomizer/item_category_adj.cpp
    soh/soh/frame_interpolation.cpp
    soh/soh/ActorDB.cpp
    soh/soh/ObjectExtension/ActorListIndex.cpp
    soh/soh/ObjectExtension/ActorMaximumHealth.cpp
    soh/soh/ObjectExtension/ObjectExtension.cpp
    soh/soh/GbiWrap.cpp
)

objects=()
find "$OUT/objects" -type f -name '*.o' -delete
for source in "${sources[@]}"; do
    safe="${source//\//_}"
    object="$OUT/objects/${safe%.cpp}.o"
    log="$OUT/logs/${safe%.cpp}.log"
    if ! "$CXX_BIN" "${flags[@]}" -c "$UPSTREAM/$source" -o "$object" >"$log" 2>&1; then
        echo "BLOCKED compiling: $source"
        grep -m1 -E 'fatal error:|error:' "$log" || true
        exit 2
    fi
    objects+=("$object")
done
echo "C++ objects compiled: ${#objects[@]}/${#sources[@]}"

if ! "$LD_BIN" -r -o "$OUT/shipwright-core-essential.o" "$CORE" "${objects[@]}" \
        >"$OUT/logs/partial-link.log" 2>&1; then
    echo 'BLOCKED: C/C++ relocatable link failed'
    grep -m3 -E 'multiple definition|undefined reference|error:' "$OUT/logs/partial-link.log" || true
    exit 3
fi
"$NM_BIN" -u "$OUT/shipwright-core-essential.o" | awk 'NF { print $NF }' | sort -u > "$OUT/undefined.txt"
comm -23 "$BASE_UNDEFINED" "$OUT/undefined.txt" > "$OUT/resolved.txt"
echo 'C/C++ relocatable link: OK'
echo "C-core bridge symbols resolved: $(wc -l < "$OUT/resolved.txt")"
echo "Current unresolved frontier: $(wc -l < "$OUT/undefined.txt")"
echo 'First resolved bridge symbols:'
head -n 15 "$OUT/resolved.txt"
echo 'First unresolved symbols:'
head -n 15 "$OUT/undefined.txt"
echo 'Share only this summary. Full lists are in essential-cpp-check/.'
}

main 2>&1 | tee "$REPORT"
