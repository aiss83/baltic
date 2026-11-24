#!/usr/bin/env bash
#
# CLI smoke test.  Uses the real EGM2008 grid and is skipped when the grid
# is not available.
#
# Copyright (c) 2026 Baltic contributors
# SPDX-License-Identifier: MIT

set -u

CLI=${1:?usage: test_cli.sh /path/to/baltic /path/to/source}
SRC=${2:?usage: test_cli.sh /path/to/baltic /path/to/source}
GRID=${BALTIC_EGM2008_GRID:-"$SRC/data/egm2008/us_nga_egm08_25.tif"}

if [[ ! -r "$GRID" ]]; then
    echo "SKIP: EGM2008 grid not found at $GRID"
    exit 77
fi

TMP=$(mktemp -d "${TMPDIR:-/tmp}/baltic-cli-XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT

failures=0
check() {
    if ! "$@"; then
        echo "FAIL: $*" >&2
        failures=$((failures + 1))
    fi
}

# --- convert forward -------------------------------------------------------
cat > "$TMP/in.csv" <<'EOF'
lon,lat,h
37.6173,55.7558,100.0
30.3167,59.9500,100.0
EOF

if ! "$CLI" convert --grid "$GRID" --offset 0.5 --input "$TMP/in.csv" \
        --output "$TMP/out.csv" --quiet; then
    echo "FAIL: convert forward" >&2
    failures=$((failures + 1))
fi

check test "$(wc -l < "$TMP/out.csv" | tr -d ' ')" = "3"
check grep -q '^lon,lat,ellipsoidal_h,baltic_h,egm2008_n,correction,offset,status$' "$TMP/out.csv"
check grep -q ',ok$' "$TMP/out.csv"

# Save the resulting Baltic height of the first point.
H=$(awk -F, 'NR==2 {print $4}' "$TMP/out.csv")
LON=$(awk -F, 'NR==2 {print $1}' "$TMP/out.csv")
LAT=$(awk -F, 'NR==2 {print $2}' "$TMP/out.csv")

# --- reverse ---------------------------------------------------------------
printf '%s,%s,%s\n' "$LON" "$LAT" "$H" > "$TMP/in_rev.csv"
if ! "$CLI" convert --grid "$GRID" --offset 0.5 --reverse \
        --input "$TMP/in_rev.csv" --output "$TMP/out_rev.csv" --quiet; then
    echo "FAIL: convert reverse" >&2
    failures=$((failures + 1))
fi

H_BACK=$(awk -F, 'NR==2 {print $4}' "$TMP/out_rev.csv")
check awk -v got="$H_BACK" 'BEGIN { exit !(got > 99.999999 && got < 100.000001) }'

# --- JSONL -----------------------------------------------------------------
if ! "$CLI" convert --grid "$GRID" --offset 0.5 --format jsonl \
        --input "$TMP/in.csv" --output "$TMP/out.jsonl" --quiet; then
    echo "FAIL: convert jsonl" >&2
    failures=$((failures + 1))
fi
check grep -q '"status":"ok"' "$TMP/out.jsonl"

# --- info ------------------------------------------------------------------
if ! "$CLI" info --grid "$GRID" > "$TMP/info.txt"; then
    echo "FAIL: info" >&2
    failures=$((failures + 1))
fi
check grep -q '^format=' "$TMP/info.txt"
check grep -q '^width=' "$TMP/info.txt"
check grep -q '^north_deg=' "$TMP/info.txt"

# --- calibrate -------------------------------------------------------------
# H_known = H_model + 0.5, so the recovered offset must be 0.5.
awk -F, 'NR>1 {printf "%s,%s,100,%s\n", $1, $2, $4}' "$TMP/out.csv" \
    > "$TMP/points.csv"
if ! "$CLI" calibrate --grid "$GRID" --points "$TMP/points.csv" \
        > "$TMP/cal.txt"; then
    echo "FAIL: calibrate" >&2
    failures=$((failures + 1))
fi
check grep -q '^points_used=2$' "$TMP/cal.txt"
OFFSET=$(awk -F= '$1=="offset_m" {print $2}' "$TMP/cal.txt")
check awk -v got="$OFFSET" 'BEGIN { exit !(got > 0.499999 && got < 0.500001) }'

# --- invalid latitude must fail with exit code 1 ---------------------------
printf '0,91,100\n' > "$TMP/bad.csv"
"$CLI" convert --grid "$GRID" --input "$TMP/bad.csv" --output "$TMP/bad_out.csv" --quiet
rc=$?
check test "$rc" = "1"
check grep -q 'invalid argument' "$TMP/bad_out.csv"

if [[ "$failures" -ne 0 ]]; then
    echo "$failures CLI check(s) failed" >&2
    exit 1
fi
echo "CLI checks passed"
exit 0
