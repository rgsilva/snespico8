#!/bin/sh
# Third oracle: run tests/realvec.lua under BOTH real PICO-8 (pico8 -x,
# printh->stdout) and runp8 (printh->stderr); outputs must match line
# for line. Gfx stages hash their own framebuffer via @ peeks.
# Skips politely when PICO-8 isn't installed (CI-friendliness).
set -e
cd "$(dirname "$0")/.."
P8BIN=/Applications/PICO-8.app/Contents/MacOS/pico8
if [ ! -x "$P8BIN" ]; then
  echo "REALVEC SKIP (PICO-8 not installed)"
  exit 0
fi

{
  printf 'pico-8 cartridge // http://www.pico-8.com\nversion 42\n__lua__\n'
  cat tests/realvec.lua
} > build/realvec.p8

perl -e 'alarm 60; exec @ARGV' "$P8BIN" -x build/realvec.p8 2>/dev/null \
  | grep -v '^RUNNING' > build/realvec_real.txt
./build/runp8 build/realvec.p8 -n 0 2>&1 >/dev/null \
  | grep -v '^DONE' > build/realvec_ours.txt

if diff -u build/realvec_real.txt build/realvec_ours.txt; then
  n=$(wc -l < build/realvec_ours.txt | tr -d ' ')
  echo "REALVEC PASS ($n lines match real PICO-8)"
else
  echo "REALVEC FAIL"
  exit 1
fi
