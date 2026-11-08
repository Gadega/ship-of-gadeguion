#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM="$ROOT/upstream/shipwright"
OUT="$ROOT/closure-check"
CC_BIN="${OOT_PS3_CC:-ppu-gcc}"
LD_BIN="${OOT_PS3_LD:-${CC_BIN%gcc}ld}"
NM_BIN="${OOT_PS3_NM:-${CC_BIN%gcc}nm}"
mkdir -p "$OUT/logs" "$OUT/objects"
REPORT="$OUT/summary.txt"

main() {
echo 'V46 real Shipwright boot closure check (NOT a game build)'
for tool in "$CC_BIN" "$LD_BIN" "$NM_BIN"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "STOP: missing $tool"
        exit 1
    fi
done
machine="$("$CC_BIN" -dumpmachine)"
case "$machine" in
    powerpc64*ps3*) ;;
    *) echo "STOP: compiler target is $machine, not PS3 PPU."; exit 1 ;;
esac
echo "Compiler: $CC_BIN ($machine)"

flags=(-std=gnu11 -O0 -mcpu=cell -ffunction-sections -fdata-sections
    -D__PS3__ -DF3DEX_GBI_2 -DCONTROLLERBUTTONS_T=uint32_t -DGBI_UCODE=F3DEX_GBI_2
    -DLOG_LEVEL_GAME_PRINTS=6
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

base_sources=(
    soh/src/boot/boot_main.c
    soh/src/boot/idle.c
    soh/src/boot/is_debug.c
    soh/src/boot/logutils.c
    soh/src/boot/stackcheck.c
    soh/src/boot/viconfig.c
    soh/src/boot/yaz0.c
    soh/src/boot/z_locale.c
    soh/src/boot/z_std_dma.c
    soh/src/code/game.c
    soh/src/code/graph.c
    soh/src/code/sched.c
    soh/src/overlays/gamestates/ovl_title/z_title.c
    soh/src/overlays/gamestates/ovl_opening/z_opening.c
)
provider_sources=(
    soh/soh/stubs.c
    soh/src/code/code_800EC960.c
    soh/src/code/debug_malloc.c
    soh/src/code/z_kankyo.c
    soh/src/code/fault_drawer.c
    soh/src/code/fault.c
    soh/src/code/gamealloc.c
    soh/src/code/gfxprint.c
    soh/src/code/z_rcp.c
    soh/src/code/irqmgr.c
    soh/src/code/sys_matrix.c
    soh/src/code/z_DLF.c
    soh/src/code/padmgr.c
    soh/src/code/z_prenmi.c
    soh/src/code/z_prenmi_buff.c
    soh/src/code/speed_meter.c
    soh/src/code/z_sram.c
    soh/src/code/sys_cfb.c
    soh/src/code/sys_ucode.c
    soh/src/code/system_malloc.c
    soh/src/code/TwoHeadArena.c
    soh/src/code/title_setup.c
    soh/src/code/z_vimode.c
    soh/src/code/z_view.c
    soh/src/code/z_viscvg.c
    soh/src/code/z_vismono.c
    soh/src/code/z_viszbuf.c
    soh/src/code/code_800A9F30.c
    soh/src/code/code_800C3C20.c
    soh/src/code/code_800D31A0.c
    soh/src/code/code_800FBCE0.c
    soh/src/code/z_game_dlftbls.c
    soh/src/buffers/gfxbuffers.c
    soh/src/buffers/zbuffer.c
)
sources=("${base_sources[@]}" "${provider_sources[@]}")

objects=()
base_objects=()
rm -f "$OUT/objects"/*.o "$OUT/boot-cluster.o" "$OUT/base-cluster.o" \
    "$OUT/undefined.txt" "$OUT/base-undefined.txt" "$OUT/resolved.txt"
for source in "${sources[@]}"; do
    relative="${source//\//_}"
    object="$OUT/objects/${relative%.c}.o"
    log="$OUT/logs/${relative%.c}.log"
    if ! "$CC_BIN" "${flags[@]}" -c "$UPSTREAM/$source" -o "$object" >"$log" 2>&1; then
        echo "BLOCKED compiling: $source"
        diagnostic="$(grep -m1 -E 'fatal error:|error:' "$log" || true)"
        [ -n "$diagnostic" ] && printf '%s\n' "${diagnostic//$ROOT\//}"
        exit 2
    fi
    objects+=("$object")
    if [ "${#objects[@]}" -le "${#base_sources[@]}" ]; then
        base_objects+=("$object")
    fi
done
echo "Objects: ${#objects[@]}/${#sources[@]} compiled"

if ! "$LD_BIN" -r -o "$OUT/base-cluster.o" "${base_objects[@]}" >"$OUT/logs/base-link.log" 2>&1; then
    echo 'BLOCKED: baseline relocatable link failed'
    exit 3
fi
if ! "$LD_BIN" -r -o "$OUT/boot-cluster.o" "${objects[@]}" >"$OUT/logs/partial-link.log" 2>&1; then
    echo 'BLOCKED: relocatable link failed'
    diagnostic="$(grep -m1 -E 'multiple definition|undefined reference|error:' "$OUT/logs/partial-link.log" || true)"
    [ -n "$diagnostic" ] && printf '%s\n' "${diagnostic//$ROOT\//}"
    exit 3
fi
echo 'Relocatable link: OK'

"$NM_BIN" -u "$OUT/base-cluster.o" | awk 'NF { print $NF }' | sort -u > "$OUT/base-undefined.txt"
"$NM_BIN" -u "$OUT/boot-cluster.o" | awk 'NF { print $NF }' | sort -u > "$OUT/undefined.txt"
comm -23 "$OUT/base-undefined.txt" "$OUT/undefined.txt" > "$OUT/resolved.txt"
base_count="$(wc -l < "$OUT/base-undefined.txt")"
count="$(wc -l < "$OUT/undefined.txt")"
resolved_count="$(wc -l < "$OUT/resolved.txt")"
echo "Baseline unresolved symbols: $base_count"
echo "Baseline symbols resolved by provider wave: $resolved_count"
echo "Current unresolved frontier: $count"
echo 'First resolved baseline symbols:'
head -n 15 "$OUT/resolved.txt"
echo 'First unresolved symbols:'
head -n 15 "$OUT/undefined.txt"
echo 'Share only this summary. Full lists are in closure-check/.'
}

main 2>&1 | tee "$REPORT"
