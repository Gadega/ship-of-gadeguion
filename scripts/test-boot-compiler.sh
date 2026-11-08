#!/usr/bin/env bash
set -euo pipefail
if [ "${1:-}" = '-dumpmachine' ]; then
    echo powerpc64-ps3-elf
    exit 0
fi
if [ "${MOCK_FAIL:-1}" = 1 ]; then
    echo 'fixture.c:1: error: simulated compiler failure' >&2
    exit 1
fi
while [ "$#" -gt 0 ]; do
    if [ "$1" = '-o' ]; then
        touch "$2"
        exit 0
    fi
    shift
done
exit 1
