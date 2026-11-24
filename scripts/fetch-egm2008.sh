#!/usr/bin/env bash
#
# Download the EGM2008 2.5' GeoTIFF grid used by libbaltic.
#
# Usage:
#   scripts/fetch-egm2008.sh [DESTINATION]
#
# Environment:
#   BALTIC_EGM2008_URL     alternative download URL
#   BALTIC_EGM2008_SHA256  expected SHA-256 (default: checksum of the
#                          official 80,585,622-byte PROJ-data grid)
#   BALTIC_SKIP_SHA256=1   skip checksum verification (not recommended)
#
# Copyright (c) 2026 Baltic contributors
# SPDX-License-Identifier: MIT

set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
DEST=${1:-"$ROOT/data/egm2008/us_nga_egm08_25.tif"}
URL=${BALTIC_EGM2008_URL:-"https://raw.githubusercontent.com/OSGeo/PROJ-data/master/us_nga/us_nga_egm08_25.tif"}
EXPECTED=${BALTIC_EGM2008_SHA256:-"4191d471eefebf24091b56dbc604353cb3b8cf8cc70e448bb9ae56a272bef17a"}

sha256_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    elif command -v openssl >/dev/null 2>&1; then
        openssl dgst -sha256 "$1" | awk '{print $NF}'
    else
        echo "error: no SHA-256 tool found (sha256sum, shasum or openssl)" >&2
        return 1
    fi
}

verify() {
    local file=$1
    local actual

    if [[ "${BALTIC_SKIP_SHA256:-0}" == "1" ]]; then
        echo "warning: checksum verification skipped" >&2
        return 0
    fi
    actual=$(sha256_file "$file")
    if [[ "$actual" != "$EXPECTED" ]]; then
        echo "error: SHA-256 mismatch for $file" >&2
        echo "  expected: $EXPECTED" >&2
        echo "  actual:   $actual" >&2
        return 1
    fi
    return 0
}

if ! command -v curl >/dev/null 2>&1; then
    echo "error: curl is required" >&2
    exit 1
fi

mkdir -p "$(dirname "$DEST")"

if [[ -f "$DEST" ]]; then
    if verify "$DEST"; then
        echo "already present: $DEST"
        exit 0
    fi
    echo "existing file is not valid, downloading again" >&2
fi

echo "downloading EGM2008 grid"
echo "  from: $URL"
echo "  to:   $DEST"

tmp="${DEST}.part.$$"
trap 'rm -f "$tmp"' EXIT

curl --fail --location --retry 3 --retry-delay 2 --connect-timeout 30 \
     --progress-bar --output "$tmp" "$URL"

verify "$tmp"
mv "$tmp" "$DEST"
trap - EXIT

echo "saved: $DEST"
echo "size:  $(wc -c < "$DEST" | tr -d ' ') bytes"
echo "sha256: $(sha256_file "$DEST")"
