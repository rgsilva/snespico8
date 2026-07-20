#!/bin/sh
# M4 Track 1 — printh parity: run each vector cart on BOTH the host core
# (runp8) and the Calypsi-compiled SNES ROM (Mesen), and diff the printh
# output. Same shared source both sides, so any divergence is a Calypsi
# codegen/arithmetic bug. Compares SNES-vs-our-host-core (the host core
# already matches z8lua/real PICO-8 via the other suites).
#
# Skips politely if the SNES toolchain or Mesen is missing.
set -e
cd "$(dirname "$0")/.."          # -> core-c/
ROOT=$(cd .. && pwd)
SNES="$ROOT/snes"
MESEN=/Applications/Mesen.app/Contents/MacOS/Mesen
CAL="$ROOT/tools/calypsi-65816-5.17/bin/cc65816"

if [ ! -x "$MESEN" ] || [ ! -x "$CAL" ]; then
  echo "SNESPARITY SKIP (Mesen or Calypsi missing)"
  exit 0
fi

mkdir -p build
fail=0

# strip p8main/parity.lua status lines, leaving only the cart's printh output
strip_status() {
  grep '^OUT ' | sed 's/^OUT //' \
    | grep -vE '^(boot|sram |state ok|chunk ok|init ok|frames |M3 PASS|RESULT )'
}

run_one() {
  name="$1"; cart="$2"; post="$3"    # post = extra normalizer (e.g. sed for table addrs)
  ref="build/parity_${name}_host.txt"
  got="build/parity_${name}_snes.txt"

  # host reference (printh -> stderr)
  ./build/runp8 "$cart" -n 0 2>&1 >/dev/null | grep -v '^DONE' | eval "$post" > "$ref"

  # build + run the SNES ROM with this cart embedded
  make -C "$SNES" p8 CART="$cart" >/dev/null 2>&1
  "$MESEN" --testrunner "$SNES/build/p8.sfc" "$SNES/test/parity.lua" 2>/dev/null \
    | strip_status | eval "$post" > "$got"

  if diff -u "$ref" "$got" >/dev/null 2>&1; then
    n=$(wc -l < "$got" | tr -d ' ')
    echo "PARITY PASS ${name} (${n} lines: SNES == host core)"
  else
    echo "PARITY FAIL ${name}"
    diff -u "$ref" "$got" | head -20 || true
    fail=1
  fi
}

# --- numvec: printh(expr) per line ---
{ printf 'pico-8 cartridge\nversion 42\n__lua__\n'
  while IFS= read -r e; do [ -n "$e" ] && printf 'printh(%s)\n' "$e"; done < tests/numvec.txt
} > build/pnum.p8
run_one num "$(pwd)/build/pnum.p8" 'cat'

# --- dialect: print=printh prologue + statements ---
{ printf 'pico-8 cartridge\nversion 42\n__lua__\n'
  printf 'print = printh\n'; cat tests/dialect.lua
} > build/pdialect.p8
run_one dialect "$(pwd)/build/pdialect.p8" 'cat'

# --- libvec: table addresses differ per allocator -> normalize both sides ---
{ printf 'pico-8 cartridge\nversion 42\n__lua__\n'
  printf 'print = printh\n'; cat tests/libvec.lua
} > build/plib.p8
run_one lib "$(pwd)/build/plib.p8" "sed 's/table: 0x[0-9a-f]*/table/'"

exit $fail
