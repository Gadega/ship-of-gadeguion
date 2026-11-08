#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM="$ROOT/upstream/shipwright"
OUT="$ROOT/boot-check"
CC_BIN="${OOT_PS3_CC:-ppu-gcc}"
mkdir -p "$OUT"
REPORT="$OUT/summary.txt"
main() {

echo 'V46 real Shipwright boot object check (NOT a game build)'
if ! command -v "$CC_BIN" >/dev/null 2>&1; then
    echo "STOP: missing $CC_BIN; load your PS3DEV environment. No host fallback."
    exit 1
fi
machine="$("$CC_BIN" -dumpmachine)"
case "$machine" in
    powerpc64*ps3*) ;;
    *) echo "STOP: compiler target is $machine, not PS3 PPU."; exit 1 ;;
esac
echo "Compiler: $CC_BIN ($machine)"

# Do not use upstream_compat: its scan-only substitutes are not a runtime.
flags=(-std=gnu11 -O0 -mcpu=cell -ffunction-sections -fdata-sections
    -D__PS3__ -DF3DEX_GBI_2 -DCONTROLLERBUTTONS_T=uint32_t -DGBI_UCODE=F3DEX_GBI_2
    -DLOG_LEVEL_GAME_PRINTS=6
    "-I$UPSTREAM/soh" "-I$UPSTREAM/soh/include" "-I$UPSTREAM/soh/src"
    "-I$UPSTREAM/soh/soh" "-I$UPSTREAM/soh/assets"
    "-I$UPSTREAM/libultraship/include" "-I$UPSTREAM/libultraship/src")
# Values from this snapshot's CMake/soh-cvars.cmake, not scan placeholders.
cvars=(RANDOMIZER_ENHANCEMENT=gRandoEnhancements RANDOMIZER_SETTING=gRandoSettings
    COSMETIC=gCosmetics AUDIO=gAudioEditor CHEAT=gCheats ENHANCEMENT=gEnhancements
    SETTING=gSettings WINDOW=gOpenWindows TRACKER=gTrackers
    DEVELOPER_TOOLS=gDeveloperTools GENERAL=gGeneral REMOTE=gRemote
    GAMEPLAY_STATS=gGameplayStats TIME_DISPLAY=gTimeDisplay)
for cvar in "${cvars[@]}"; do
    flags+=("-DCVAR_PREFIX_${cvar%%=*}=\"${cvar#*=}\"")
done
sources=(
    soh/src/code/game.c
    soh/src/code/graph.c
    soh/src/overlays/gamestates/ovl_title/z_title.c
    soh/src/overlays/gamestates/ovl_opening/z_opening.c
)
passed=0
failed=0
for source in "${sources[@]}"; do
    name="${source##*/}"
    object="$OUT/${name%.c}.o"
    log="$OUT/$name.log"
    rm -f "$object"
    if "$CC_BIN" "${flags[@]}" -c "$UPSTREAM/$source" -o "$object" >"$log" 2>&1; then
        echo "OBJECT OK: $source"
        passed=$((passed + 1))
    else
        rm -f "$object"
        echo "BLOCKED: $source"
        # Keep the full diagnostic privately; send only the first blocker.
        diagnostic="$(grep -m1 -E 'fatal error:|error:' "$log" || true)"
        if [ -n "$diagnostic" ]; then
            printf '%s\n' "${diagnostic//$ROOT\//}"
        else
            head -n 2 "$log"
        fi
        failed=$((failed + 1))
    fi
done
echo "Result: $passed objects, $failed blocked. No link or execution tested."
echo 'Share only this summary. Detailed diagnostics stay in boot-check/*.log.'
test "$failed" -eq 0
}

main 2>&1 | tee "$REPORT"
