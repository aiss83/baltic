#!/usr/bin/env bash
#
# Measure the bare-metal code size of baltic_embedded with clang.
# Requires a clang with the ARM target and llvm-size (Xcode or LLVM).
#
# Usage: embedded/tools/size_report.sh
#
# Copyright (c) 2026 Baltic contributors
# SPDX-License-Identifier: MIT

set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
CC=${CC:-clang}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/baltic-size-XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT

if command -v llvm-size >/dev/null 2>&1; then
    SIZE=llvm-size
elif command -v xcrun >/dev/null 2>&1 && xcrun --find llvm-size >/dev/null 2>&1; then
    SIZE="xcrun llvm-size"
else
    echo "error: llvm-size not found" >&2
    exit 2
fi

for cpu in cortex-m7 cortex-m4 cortex-m0plus; do
    obj="$TMP/baltic_embedded_${cpu}.o"
    opt=-Os
    fpu=
    if [ "$cpu" = "cortex-m7" ]; then
        opt=-O2
        fpu="-mfpu=fpv5-d16 -mfloat-abi=hard"
    fi
    echo "=== $cpu ($opt)"
    # shellcheck disable=SC2086
    $CC --target=arm-none-eabi -mcpu="$cpu" -mthumb $fpu $opt \
        -ffunction-sections -fdata-sections -ffreestanding \
        -I"$ROOT/include" -c "$ROOT/src/baltic_embedded.c" -o "$obj"
    # shellcheck disable=SC2086
    $SIZE "$obj"
    if command -v llvm-nm >/dev/null 2>&1; then
        echo "undefined symbols:"
        llvm-nm -u "$obj" || true
    fi
done
