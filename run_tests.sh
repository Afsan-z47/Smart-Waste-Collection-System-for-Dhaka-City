#!/usr/bin/env bash
# Compile waste.cpp from the project directory and run all rigorous tests.
# Usage: bash run_tests.sh

set -u

SRC=./waste.cpp
BIN=./waste
IN_DIR=inputs
OUT_DIR=outputs

if [ ! -f "$SRC" ]; then
    echo "Error: $SRC not found" >&2
    exit 1
fi

echo "==> Compiling $SRC"
g++ -O2 -std=c++17 -Wall -Wextra -pedantic -o "$BIN" "$SRC" || exit 1
echo "    build OK -> $BIN"

mkdir -p "$OUT_DIR"
pass=0
fail=0

run_case() {
    local inp="$1"
    local name
    name=$(basename "$inp" .txt)
    local out="$OUT_DIR/$name.out"

    echo
    echo "==> Running $name"

    if [[ "$name" == test_invalid_* ]]; then
        if "$BIN" < "$inp" > "$out" 2>&1; then
            echo "    FAIL: expected non-zero exit -> $out"
            fail=$((fail+1))
        else
            echo "    PASS: rejected invalid input -> $out"
            pass=$((pass+1))
        fi
        return
    fi

    if ! "$BIN" < "$inp" > "$out" 2>&1; then
        echo "    FAIL: program exited non-zero -> $out"
        fail=$((fail+1))
        return
    fi

    case "$name" in
        test_knapsack_exact)
            grep -q 'urgency value 155.0' "$out" || {
                echo "    FAIL: expected DP value 155.0 (A+B)"; fail=$((fail+1)); return; }
            ;;
        test_flow_classic)
            grep -q 'Maximum flow : 23.00 t/day' "$out" || {
                echo "    FAIL: expected max-flow 23.00"; fail=$((fail+1)); return; }
            grep -q 'Minimum cut  : 23.00 t/day' "$out" || {
                echo "    FAIL: expected min-cut 23.00"; fail=$((fail+1)); return; }
            grep -q 'Verified: max-flow = min-cut' "$out" || {
                echo "    FAIL: max-flow/min-cut verification missing"; fail=$((fail+1)); return; }
            ;;
        test_flow_bottleneck)
            grep -q 'System max-flow      : 7.00 t/day' "$out" || {
                echo "    FAIL: expected system max-flow 7.00"; fail=$((fail+1)); return; }
            ;;
        test_unreachable)
            if grep -q -- '- IsolatedBin' "$out"; then
                echo "    FAIL: unreachable bin was selected"
                fail=$((fail+1))
                return
            fi
            ;;
    esac

    # Common sanity checks for valid runs.
    if grep -Eqi '\\bnan\\b|\\binf\\b|segmentation fault|abort' "$out"; then
        echo "    FAIL: suspicious numeric/runtime error found in output"
        fail=$((fail+1))
        return
    fi

    echo "    PASS -> $out  ($(wc -l < "$out") lines)"
    pass=$((pass+1))
}

for inp in "$IN_DIR"/*.txt; do
    run_case "$inp"
done

echo
echo "================================================================"
echo "  Tests passed: $pass    failed: $fail"
echo "  Outputs in:  $OUT_DIR/"
echo "================================================================"

exit $(( fail > 0 ? 1 : 0 ))
