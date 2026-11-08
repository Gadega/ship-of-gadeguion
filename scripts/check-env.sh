#!/usr/bin/env sh
set -eu

failed=0

check_command() {
    if command -v "$1" >/dev/null 2>&1; then
        printf 'ok: %s\n' "$1"
    else
        printf 'missing: %s\n' "$1" >&2
        failed=1
    fi
}

if [ -z "${PS3DEV:-}" ]; then
    printf 'missing: PS3DEV environment variable\n' >&2
    failed=1
fi
if [ -z "${PSL1GHT:-}" ]; then
    printf 'missing: PSL1GHT environment variable\n' >&2
    failed=1
elif [ ! -f "$PSL1GHT/ppu_rules" ]; then
    printf 'missing: %s/ppu_rules\n' "$PSL1GHT" >&2
    failed=1
fi

check_command ppu-gcc
check_command ppu-g++
check_command make_self_npdrm
check_command pkg
check_command sfo
check_command package_finalize

if [ "$failed" -ne 0 ]; then
    printf '\nInstall the current ps3dev release and export PS3DEV, PSL1GHT, and PATH.\n' >&2
    exit 1
fi

printf '\nPS3 build environment is ready.\n'
