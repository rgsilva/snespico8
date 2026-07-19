#!/bin/sh
# Dialect statement diff: tests/dialect.lua executed by our core (as a
# cart) and by the z8lua oracle; stdout must match line for line.
set -e
cd "$(dirname "$0")/.."
Z8LUA=../tools/z8lua/z8lua

{
  printf 'pico-8 cartridge\nversion 42\n__lua__\n'
  printf 'print = printh -- M2: print draws to the screen; route to stderr\n'
  cat tests/dialect.lua
} > build/dialect.p8
./build/runp8 build/dialect.p8 -n 0 2>&1 >/dev/null | grep -v '^DONE' > build/dialect_ours.txt

"$Z8LUA" tests/dialect.lua > build/dialect_z8.txt

if diff -u build/dialect_z8.txt build/dialect_ours.txt; then
  n=$(wc -l < build/dialect_ours.txt | tr -d ' ')
  echo "DIALECT PASS ($n output lines match the z8lua oracle)"
else
  echo "DIALECT FAIL"
  exit 1
fi
