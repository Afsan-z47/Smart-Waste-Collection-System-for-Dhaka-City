#!/usr/bin/env bash
# Compile waste.cpp and run every input file under inputs/.
# Usage:  bash run_tests.sh

set -e

SRC=waste.cpp
BIN=waste
IN_DIR=inputs
OUT_DIR=outputs

# ---------------------------------------------------------------- build
if [ ! -f "$SRC" ]; then
    echo "Error: $SRC not found in $(pwd)" >&2
    exit 1
fi

echo "==> Compiling $SRC"
g++ -O2 -std=c++17 -Wall -o "$BIN" "$SRC"
echo "    build OK -> ./$BIN"

# ---------------------------------------------------------------- run
mkdir -p "$OUT_DIR"
pass=0
fail=0

for inp in "$IN_DIR"/*.txt; do
    name=$(basename "$inp" .txt)
    out="$OUT_DIR/$name.out"

    echo
    echo "==> Running $name"
    if ./"$BIN" < "$inp" > "$out" 2>&1; then
        echo "    exit 0 -> $out  ($(wc -l < "$out") lines)"
        pass=$((pass+1))
    else
        echo "    FAILED (non-zero exit) -> see $out"
        fail=$((fail+1))
    fi
done

echo
echo "================================================================"
echo "  Tests passed: $pass    failed: $fail"
echo "  Outputs in:  $OUT_DIR/"
echo "================================================================"

exit $(( fail > 0 ? 1 : 0 ))
