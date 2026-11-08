#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/stage-diagnostic-check"
mkdir -p "$OUT"

run_step() {
    local name="$1"
    shift
    echo "STEP: $name"
    if ! "$@" >"$OUT/$name.log" 2>&1; then
        echo "BLOCKED: $name"
        tail -n 12 "$OUT/$name.log"
        exit 1
    fi
}

cd "$ROOT"
run_step env ./scripts/check-env.sh
run_step ps3-os bash ./scripts/check-real-ps3-os.sh
run_step ps3-resources bash ./scripts/check-real-ps3-resources.sh
run_step native-link bash ./scripts/check-real-native-link.sh
run_step package bash ./scripts/package-real-native.sh

echo "STAGE DIAGNOSTIC PKG: OK"
echo "Install/run it, then report the highest visible boot stage:"
echo "0 bars/red-ish = entered main before GameConsole_Init"
echo "2 bars/cyan-ish = survived InitOTR"
echo "4+ bars/light-ish = reached heap setup / about to enter real Main"
echo "No screen, straight XMB = crash/exit before main or before RSX init"
