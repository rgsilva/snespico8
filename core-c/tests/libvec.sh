#!/bin/sh
# Dialect statement diff: tests/libvec.lua executed by our core (as a
# cart) and by the z8lua oracle; stdout must match line for line.
set -e
cd "$(dirname "$0")/.."
Z8LUA=../tools/z8lua/z8lua

{
  printf 'pico-8 cartridge\nversion 42\n__lua__\n'
  printf 'print = printh -- M2: print draws to the screen; route to stderr\n'
  cat tests/libvec.lua
} > build/libvec.p8
./build/runp8 build/libvec.p8 -n 0 2>&1 >/dev/null | grep -v '^DONE' | sed 's/table: 0x[0-9a-f]*/table/' > build/libvec_ours.txt

"$Z8LUA" tests/libvec.lua | sed 's/table: 0x[0-9a-f]*/table/' > build/libvec_z8.txt

if diff -u build/libvec_z8.txt build/libvec_ours.txt; then
  n=$(wc -l < build/libvec_ours.txt | tr -d ' ')
  echo "LIBVEC PASS ($n output lines match the z8lua oracle)"
else
  echo "LIBVEC FAIL"
  exit 1
fi
