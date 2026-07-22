#!/bin/sh
# M9 T7 step 5 gate: every test cart, run from source (runp8, the
# host-shaped compiler) and from a p8luac cross-dump (target-shaped
# 16-bit MAXARG encode + 2-byte int/size_t widths, reloaded via -b).
# stdout AND stderr must match byte for byte in -H hash mode, so this
# compares framebuffer/RAM state per flip, not just prints. Proves the
# whole dump pipeline preserves semantics before Mesen ever sees it.
set -e
cd "$(dirname "$0")/.."
R=./build/runp8
X=./build/xdump/p8luac
D=build/dumpvec
mkdir -p $D
n=0
for cart in tests/carts/*.p8 tests/carts-external/celeste.p8; do
  [ -f "$cart" ] || continue
  b=$D/$(basename "$cart" .p8).bin
  "$X" "$cart" -c "$b" >/dev/null
  "$R" "$cart" -H -n 4 >$D/src_out.txt 2>$D/src_err.txt || true
  "$X" "$cart" -b "$b" -H -n 4 >$D/bin_out.txt 2>$D/bin_err.txt || true
  if ! diff -q $D/src_out.txt $D/bin_out.txt >/dev/null 2>&1 || \
     ! diff -q $D/src_err.txt $D/bin_err.txt >/dev/null 2>&1; then
    echo "DUMPVEC FAIL $cart"
    diff $D/src_err.txt $D/bin_err.txt | head -10
    diff $D/src_out.txt $D/bin_out.txt | head -10
    exit 1
  fi
  n=$((n + 1))
done
echo "DUMPVEC PASS ($n carts: source-run == cross-dump-run, hash mode)"
