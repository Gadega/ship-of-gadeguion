#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Byte-order parity check: compiles every game header (global.h, z64audio.h and all
# overlay headers) for the PS3 (ppu-gcc, big-endian) and for the PC (host gcc, x86-64,
# little-endian, how Ship of Harkinian normally runs), then compares every struct/union
# layout with scripts/layout_parity.py. Report: layout-parity/report.txt.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
U="$ROOT/upstream/shipwright"
OUT="$ROOT/layout-parity"
mkdir -p "$OUT"

common=(-std=gnu11 -fno-strict-aliasing -DF3DEX_GBI_2 -DGBI_UCODE=F3DEX_GBI_2 -DCONTROLLERBUTTONS_T=uint32_t
    -DLOG_LEVEL_GAME_PRINTS=6 "-I$U/soh" "-I$U/soh/include" "-I$U/soh/src" "-I$U/soh/soh" "-I$U/soh/assets"
    "-I$U/libultraship/include" "-I$U/libultraship/src" -g -fno-eliminate-unused-debug-types -w)
ps3=(ppu-gcc "${common[@]}" -D__PS3__ -mcpu=cell "-I$ROOT/ps3_platform/include")
pc=(gcc "${common[@]}" -gdwarf-4)

mapfile -t headers < <(find "$U/soh/src/overlays" -name 'z_*.h' | sort)

# Put headers into probe files; a header that clashes with an earlier one (two actors
# defining the same enum) goes to the next probe file.
probes=()
remaining=("${headers[@]}")
n=0
while [ "${#remaining[@]}" -gt 0 ] && [ "$n" -lt 8 ]; do
    src="$OUT/probe$n.c"
    { echo '#include "global.h"'; echo '#include "z64audio.h"'
      for h in "${remaining[@]}"; do echo "#include \"$h\""; done
      echo "int layout_parity_probe$n;"; } > "$src"
    bad=()
    for attempt in 1 2 3 4 5 6; do
        if "${ps3[@]}" -c "$src" -o "$OUT/ps3_$n.o" 2> "$OUT/ps3_$n.err"; then break; fi
        mapfile -t clash < <(grep -o "$U/soh/src/overlays/[^:]*\.h" "$OUT/ps3_$n.err" | sort -u)
        [ "${#clash[@]}" -gt 0 ] || { echo "STOP: PS3 probe $n failed:"; head -5 "$OUT/ps3_$n.err"; exit 1; }
        bad+=("${clash[@]}")
        { echo '#include "global.h"'; echo '#include "z64audio.h"'
          for h in "${remaining[@]}"; do
              skip=0; for b in "${bad[@]}"; do [ "$h" = "$b" ] && skip=1; done
              [ "$skip" -eq 1 ] || echo "#include \"$h\""
          done
          echo "int layout_parity_probe$n;"; } > "$src"
    done
    "${pc[@]}" -c "$src" -o "$OUT/pc_$n.o" 2> "$OUT/pc_$n.err" || { echo "STOP: PC probe $n failed:"; head -5 "$OUT/pc_$n.err"; exit 1; }
    probes+=("$n")
    remaining=("${bad[@]+"${bad[@]}"}")
    n=$((n + 1))
done
echo "probe files: ${#probes[@]} (headers: ${#headers[@]})"

: > "$OUT/report.txt"
for n in "${probes[@]}"; do
    python3 "$ROOT/scripts/layout_parity.py" "$OUT/ps3_$n.o" "$OUT/pc_$n.o" "$OUT/report_$n.txt" > /dev/null
    cat "$OUT/report_$n.txt" >> "$OUT/report.txt"
done
echo "LAYOUT PARITY: report at $OUT/report.txt"
grep -E '^== ' "$OUT/report.txt" | sort | uniq -c | sort -rn | head -20
