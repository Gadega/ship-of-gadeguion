#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM="$ROOT/upstream/shipwright"
OUT="$ROOT/core-c-check"
CC_BIN="${OOT_PS3_CC:-ppu-gcc}"
LD_BIN="${OOT_PS3_LD:-${CC_BIN%gcc}ld}"
NM_BIN="${OOT_PS3_NM:-${CC_BIN%gcc}nm}"
mkdir -p "$OUT/logs" "$OUT/objects"
REPORT="$OUT/summary.txt"

main() {
echo 'V46 real Shipwright core C check (NOT a game executable)'
for tool in "$CC_BIN" "$LD_BIN" "$NM_BIN"; do
    command -v "$tool" >/dev/null 2>&1 || { echo "STOP: missing $tool"; exit 1; }
done
machine="$("$CC_BIN" -dumpmachine)"
case "$machine" in
    powerpc64*ps3*) ;;
    *) echo "STOP: compiler target is $machine, not PS3 PPU."; exit 1 ;;
esac
echo "Compiler: $CC_BIN ($machine)"

flags=(-std=gnu11 -O2 -fno-strict-aliasing -mminimal-toc -mcpu=cell -ffunction-sections -fdata-sections
    -D__PS3__ -DF3DEX_GBI_2 -DGBI_UCODE=F3DEX_GBI_2
    -DCONTROLLERBUTTONS_T=uint32_t -DLOG_LEVEL_GAME_PRINTS=6
    "-I$ROOT/ps3_platform/include"
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

# Match soh/CMakeLists.txt's decompiled-C selection.
mapfile -t sources < <(find "$UPSTREAM/soh/src" -type f -name '*.c' \
    ! -path '*/dmadata/*' ! -path '*/elf_message/*' \
    ! -path '*/libultra/io/*' ! -path '*/libultra/libc/*' \
    ! -path '*/libultra/os/*' ! -path '*/libultra/rmon/*' | sort)
sources+=("$UPSTREAM/soh/src/libultra/libc/sprintf.c")
excluded_gu=(cosf.c lookat.c lookathil.c perspective.c position.c sinf.c sqrtf.c us2dex.c)
filtered=()
for source in "${sources[@]}"; do
    skip=0
    for excluded in "${excluded_gu[@]}"; do
        if [ "$source" = "$UPSTREAM/soh/src/libultra/gu/$excluded" ]; then
            skip=1
            break
        fi
    done
    [ "$skip" -eq 1 ] || filtered+=("$source")
done
mapfile -t port_c < <(find "$UPSTREAM/soh/soh" -type f -name '*.c' \
    ! -path '*/Extractor/*' ! -name 'mixer.c' | sort)
sources=("${filtered[@]}" "${port_c[@]}")

find "$OUT/objects" -type f -name '*.o' -delete
: > "$OUT/failures.txt"
objects=()
passed=0
failed=0
# Compile in parallel (one job per CPU); results are collected in source order below.
jobs_max="$(nproc 2>/dev/null || echo 4)"
started=0
for source in "${sources[@]}"; do
    relative="${source#$UPSTREAM/soh/}"
    safe="${relative//\//_}"
    "$CC_BIN" "${flags[@]}" -c "$source" -o "$OUT/objects/${safe%.c}.o" >"$OUT/logs/${safe%.c}.log" 2>&1 &
    started=$((started + 1))
    while [ "$(jobs -rp | wc -l)" -ge "$jobs_max" ]; do wait -n || true; done
    if [ $((started % 100)) -eq 0 ]; then
        echo "Progress: $started/${#sources[@]} started"
    fi
done
wait || true
for source in "${sources[@]}"; do
    relative="${source#$UPSTREAM/soh/}"
    safe="${relative//\//_}"
    object="$OUT/objects/${safe%.c}.o"
    log="$OUT/logs/${safe%.c}.log"
    if [ -s "$object" ] && ! grep -qE 'fatal error:|error:' "$log"; then
        objects+=("$object")
        passed=$((passed + 1))
    else
        failed=$((failed + 1))
        diagnostic="$(grep -m1 -E 'fatal error:|error:' "$log" || true)"
        printf '%s: %s\n' "$relative" "${diagnostic//$ROOT\//}" >> "$OUT/failures.txt"
    fi
done
echo "Objects compiled: $passed/${#sources[@]}"
if [ "$failed" -ne 0 ]; then
    echo "Blocked source files: $failed"
    echo 'First blockers:'
    head -n 15 "$OUT/failures.txt"
    echo 'Share only this summary. Full diagnostics stay in core-c-check/logs/.'
    exit 2
fi

if ! "$LD_BIN" -r -o "$OUT/shipwright-core-c.o" "${objects[@]}" >"$OUT/logs/partial-link.log" 2>&1; then
    echo 'BLOCKED: combined relocatable link failed'
    grep -m3 -E 'multiple definition|undefined reference|error:' "$OUT/logs/partial-link.log" || true
    exit 3
fi
"$NM_BIN" -u "$OUT/shipwright-core-c.o" | awk 'NF { print $NF }' | sort -u > "$OUT/undefined.txt"
echo 'Combined relocatable link: OK'
echo "C/C++ bridge symbols remaining: $(wc -l < "$OUT/undefined.txt")"
echo 'First bridge symbols:'
head -n 20 "$OUT/undefined.txt"
echo 'Share only this summary. Full list is core-c-check/undefined.txt.'
}

main 2>&1 | tee "$REPORT"
