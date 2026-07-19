#!/bin/sh
# Numeric vector diff: every expression in numvec.txt evaluated by our
# fix32 core (runp8 + printh) and by the z8lua oracle; outputs must match
# line for line.
set -e
cd "$(dirname "$0")/.."
Z8LUA=../tools/z8lua/z8lua
V=tests/numvec.txt

# our core: build a cart that printh's each expression
{
  printf 'pico-8 cartridge\nversion 42\n__lua__\n'
  while IFS= read -r e; do
    [ -n "$e" ] && printf 'printh(%s)\n' "$e"
  done < "$V"
} > build/numvec.p8
./build/runp8 build/numvec.p8 -n 0 2>build/numvec_ours.txt >/dev/null

# oracle
{
  while IFS= read -r e; do
    [ -n "$e" ] && printf 'print(%s)\n' "$e"
  done < "$V"
} > build/numvec_oracle.lua
"$Z8LUA" build/numvec_oracle.lua > build/numvec_z8.txt

if diff -u build/numvec_z8.txt build/numvec_ours.txt; then
  n=$(grep -c . "$V")
  echo "NUMVEC PASS ($n expressions match the z8lua oracle)"
else
  echo "NUMVEC FAIL"
  exit 1
fi
