#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM="$ROOT/upstream/shipwright"
BASE="$ROOT/ps3-services-check/shipwright-core-services.o"
BASE_UNDEFINED="$ROOT/ps3-services-check/undefined.txt"
OUT="$ROOT/ps3-os-check"
CC_BIN="${OOT_PS3_CC:-ppu-gcc}"
LD_BIN="${OOT_PS3_LD:-ppu-ld}"
NM_BIN="${OOT_PS3_NM:-ppu-nm}"
PS3_SDK_ROOT="${PSL1GHT:-${PS3DEV:-/usr/local/ps3dev}}"
PS3_SDK_INCLUDE="$PS3_SDK_ROOT/ppu/include"
mkdir -p "$OUT"
REPORT="$OUT/summary.txt"

main() {
echo 'V46 native PS3 OS bridge check (NOT a game executable)'
for tool in "$CC_BIN" "$LD_BIN" "$NM_BIN"; do
    command -v "$tool" >/dev/null 2>&1 || { echo "STOP: missing $tool"; exit 1; }
done
[ -f "$PS3_SDK_INCLUDE/ppu-types.h" ] || { echo "STOP: missing $PS3_SDK_INCLUDE/ppu-types.h"; exit 1; }
machine="$("$CC_BIN" -dumpmachine)"
case "$machine" in
    powerpc64*ps3*) ;;
    *) echo "STOP: compiler target is $machine, not PS3 PPU."; exit 1 ;;
esac
echo "Compiler: $CC_BIN ($machine)"

# types.h and stubs.c are part of this patch, so old cached core objects must
# never be linked against the new native implementation.
echo 'Rebuilding patched C core and bridge prerequisites...'
if ! bash "$ROOT/scripts/check-real-core-c.sh" >"$OUT/prereq-core.log" 2>&1; then
    echo 'BLOCKED rebuilding C core; see ps3-os-check/prereq-core.log'
    tail -n 8 "$OUT/prereq-core.log"
    exit 2
fi
if ! bash "$ROOT/scripts/check-real-essential-cpp.sh" >"$OUT/prereq-cpp.log" 2>&1; then
    echo 'BLOCKED rebuilding C++ bridge; see ps3-os-check/prereq-cpp.log'
    tail -n 8 "$OUT/prereq-cpp.log"
    exit 2
fi
if ! bash "$ROOT/scripts/check-real-ps3-services.sh" >"$OUT/prereq-services.log" 2>&1; then
    echo 'BLOCKED rebuilding PS3 services; see ps3-os-check/prereq-services.log'
    tail -n 8 "$OUT/prereq-services.log"
    exit 2
fi
if [ ! -f "$BASE" ] || [ ! -f "$BASE_UNDEFINED" ]; then
    echo 'STOP: prerequisite bridge output was not produced.'
    exit 2
fi
echo 'Patched prerequisites: OK'

source="$ROOT/ps3_platform/source/shipwright_os_ps3.c"
object="$OUT/shipwright_os_ps3.o"
log="$OUT/compile.log"
if ! "$CC_BIN" -std=gnu11 -O2 -mcpu=cell -Wall -Wextra -Werror \
        -D__PS3__ -DF3DEX_GBI_2 -DGBI_UCODE=F3DEX_GBI_2 \
        -DCONTROLLERBUTTONS_T=uint32_t \
        -I"$PS3_SDK_INCLUDE" \
        -I"$UPSTREAM/soh" -I"$UPSTREAM/soh/include" -I"$UPSTREAM/soh/src" \
        -I"$UPSTREAM/soh/soh" -I"$UPSTREAM/soh/assets" \
        -I"$UPSTREAM/libultraship/include" -I"$UPSTREAM/libultraship/src" \
        -c "$source" -o "$object" >"$log" 2>&1; then
    echo 'BLOCKED compiling: ps3_platform/source/shipwright_os_ps3.c'
    grep -m1 -E 'fatal error:|error:' "$log" || true
    exit 3
fi
echo 'Native PS3 OS object compiled: OK'

if ! "$LD_BIN" -r -o "$OUT/shipwright-core-ps3-os.o" "$BASE" "$object" \
        >"$OUT/link.log" 2>&1; then
    echo 'BLOCKED: PS3 OS relocatable link failed'
    grep -m3 -E 'multiple definition|undefined reference|error:' "$OUT/link.log" || true
    exit 4
fi
"$NM_BIN" -u "$OUT/shipwright-core-ps3-os.o" | awk 'NF { print $NF }' | sort -u > "$OUT/undefined.txt"
comm -23 "$BASE_UNDEFINED" "$OUT/undefined.txt" > "$OUT/resolved.txt"
echo 'PS3 OS relocatable link: OK'
echo "OS symbols resolved: $(wc -l < "$OUT/resolved.txt")"
echo "Current unresolved frontier: $(wc -l < "$OUT/undefined.txt")"
echo 'Resolved OS services:'
cat "$OUT/resolved.txt"
echo 'Share only this summary. Full diagnostics stay in ps3-os-check/.'
}

main 2>&1 | tee "$REPORT"
