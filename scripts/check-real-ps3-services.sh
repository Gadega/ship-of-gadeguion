#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM="$ROOT/upstream/shipwright"
BASE="$ROOT/essential-cpp-check/shipwright-core-essential.o"
BASE_UNDEFINED="$ROOT/essential-cpp-check/undefined.txt"
OUT="$ROOT/ps3-services-check"
CXX_BIN="${OOT_PS3_CXX:-ppu-g++}"
LD_BIN="${OOT_PS3_LD:-ppu-ld}"
NM_BIN="${OOT_PS3_NM:-ppu-nm}"
mkdir -p "$OUT"
REPORT="$OUT/summary.txt"

main() {
echo 'V46 functional PS3 service bridge check (NOT a game executable)'
for tool in "$CXX_BIN" "$LD_BIN" "$NM_BIN"; do
    command -v "$tool" >/dev/null 2>&1 || { echo "STOP: missing $tool"; exit 1; }
done
if [ ! -f "$BASE" ] || [ ! -f "$BASE_UNDEFINED" ]; then
    echo 'STOP: run bash scripts/check-real-essential-cpp.sh first.'
    exit 1
fi
machine="$("$CXX_BIN" -dumpmachine)"
case "$machine" in
    powerpc64*ps3*) ;;
    *) echo "STOP: compiler target is $machine, not PS3 PPU."; exit 1 ;;
esac
echo "Compiler: $CXX_BIN ($machine)"

source="$ROOT/ps3_platform/source/shipwright_core_services.cpp"
object="$OUT/shipwright_core_services.o"
log="$OUT/compile.log"
if ! "$CXX_BIN" -std=gnu++17 -O2 -mcpu=cell -Wall -Wextra -Werror \
        -D__PS3__ -DIS_BIGENDIAN \
        -I"$UPSTREAM/libultraship/include" \
        -c "$source" -o "$object" >"$log" 2>&1; then
    echo 'BLOCKED compiling: ps3_platform/source/shipwright_core_services.cpp'
    grep -m1 -E 'fatal error:|error:' "$log" || true
    exit 2
fi
echo 'PS3 service object compiled: OK'

if ! "$LD_BIN" -r -o "$OUT/shipwright-core-services.o" "$BASE" "$object" \
        >"$OUT/link.log" 2>&1; then
    echo 'BLOCKED: service relocatable link failed'
    grep -m3 -E 'multiple definition|undefined reference|error:' "$OUT/link.log" || true
    exit 3
fi
"$NM_BIN" -u "$OUT/shipwright-core-services.o" | awk 'NF { print $NF }' | sort -u > "$OUT/undefined.txt"
comm -23 "$BASE_UNDEFINED" "$OUT/undefined.txt" > "$OUT/resolved.txt"
echo 'Service relocatable link: OK'
echo "Service symbols resolved: $(wc -l < "$OUT/resolved.txt")"
echo "Current unresolved frontier: $(wc -l < "$OUT/undefined.txt")"
echo 'Resolved services:'
cat "$OUT/resolved.txt"
echo 'Share only this summary. Full list is ps3-services-check/undefined.txt.'
}

main 2>&1 | tee "$REPORT"
