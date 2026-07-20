#!/bin/sh
# Graphics goldens: run each gfx cart 2 frames, compare per-flip hashes
# (and any printh output) against tests/goldens/<name>.txt.
# Rerecord with: sh tests/gfxvec.sh record
set -e
cd "$(dirname "$0")/.."
mode="$1"
fail=0
for cart in tests/carts/gfx_*.p8; do
  name=$(basename "$cart" .p8)
  out="build/${name}.out"
  ./build/runp8 "$cart" -n 2 -p "build/${name}.ppm" 2>"build/${name}.err" \
    | grep FLIP | sed 's/ ops=[0-9]*//' > "$out"
  cat "build/${name}.err" >> "$out"
  if [ "$mode" = record ]; then
    cp "$out" "tests/goldens/${name}.txt"
    echo "recorded ${name}"
  elif diff -q "tests/goldens/${name}.txt" "$out" >/dev/null 2>&1; then
    echo "GFX PASS ${name}"
  else
    echo "GFX FAIL ${name}"
    diff -u "tests/goldens/${name}.txt" "$out" | head -8 || true
    fail=1
  fi
done
exit $fail
