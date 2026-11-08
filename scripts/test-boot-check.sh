#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TEMP="$(mktemp -d)"
trap 'rm -rf "$TEMP"' EXIT
mkdir -p "$TEMP/scripts"
cp "$ROOT/scripts/check-real-boot.sh" "$TEMP/scripts/"
# The mock validates reporting/control flow only, never source compilation.
cp "$ROOT/scripts/test-boot-compiler.sh" "$TEMP/compiler"
chmod +x "$TEMP/compiler"
run() {
    local expected="$1"
    shift
    local status=0
    env "$@" bash "$TEMP/scripts/check-real-boot.sh" > "$TEMP/output" || status=$?
    test "$status" -eq "$expected"
    cmp "$TEMP/output" "$TEMP/boot-check/summary.txt"
}
run 1 OOT_PS3_CC="$TEMP/missing"
grep -q 'No host fallback' "$TEMP/output"
run 1 OOT_PS3_CC=gcc
grep -q 'not PS3 PPU' "$TEMP/output"
run 0 OOT_PS3_CC="$TEMP/compiler" MOCK_FAIL=0
grep -q '4 objects, 0 blocked' "$TEMP/output"
run 1 OOT_PS3_CC="$TEMP/compiler" MOCK_FAIL=1
grep -q '0 objects, 4 blocked' "$TEMP/output"
test "$(find "$TEMP/boot-check" -name '*.o' | wc -l)" -eq 0
test "$(grep -c 'error: simulated' "$TEMP/output")" -eq 4
echo 'PASS: missing compiler, host rejection, success/failure summaries, stale object removal.'
