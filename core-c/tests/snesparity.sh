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
# --timeout: Mesen's DEFAULT testrunner wall limit kills slow runs (the
# harness scripts self-stop on EMULATED frame counts, so under host
# contention a healthy run can starve mid-output and fail as a bogus
# 0-line/0-flip diff). 900 s per run only catches true hangs.
MESEN="/Applications/Mesen.app/Contents/MacOS/Mesen"
MESENTO="--timeout=900"
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
    | grep -vE '^(boot|sram |state ok|chunk ok|init ok|frames |M3 PASS|RESULT |HASH )'
}

run_one() {
  name="$1"; cart="$2"; post="$3"    # post = extra normalizer (e.g. sed for table addrs)
  ref="build/parity_${name}_host.txt"
  got="build/parity_${name}_snes.txt"

  # host reference (printh -> stderr)
  ./build/runp8 "$cart" -n 0 2>&1 >/dev/null | grep -v '^DONE' | eval "$post" > "$ref"

  # build + run the SNES ROM with this cart embedded (no hashing needed;
  # printh is captured before the flip loop). SNESPARITY_MAKEFLAGS lets
  # the M9 roulette battery inject PERTURB=n link-layout perturbations.
  rm -f "$SNES/build/p8obj/p8main.o"
  make -C "$SNES" p8 CART="$cart" P8MAINDEF= $SNESPARITY_MAKEFLAGS >/dev/null 2>&1
  "$MESEN" --testrunner $MESENTO "$SNES/build/p8.sfc" "$SNES/test/parity.lua" 2>/dev/null \
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

# --- Track 2: framebuffer + machine-RAM hash parity ---
# p8_hash_region (shared C, identical both sides) over low RAM [0,0x6000)
# and screen [0x6000,0x8000), per flip. SNES runs are bounded by Mesen's
# script time limit (the extra hashing is slow at -O0), so we compare the
# common prefix and require at least MINF matching flips.
MINF=3
run_hash() {
  name="$1"; cart="$2"; frames="$3"; input="$4"
  ref="build/hash_${name}_host.txt"; got="build/hash_${name}_snes.txt"
  iarg=""; [ -n "$input" ] && iarg="-i $input"
  ./build/runp8 "$cart" -H -n "$frames" $iarg 2>/dev/null | grep '^HASH ' > "$ref"
  rm -f "$SNES/build/p8obj/p8main.o"
  make -C "$SNES" p8 CART="$cart" FRAMES="$frames" INPUT="$input" \
    P8MAINDEF=-DP8_PARITY $SNESPARITY_MAKEFLAGS >/dev/null 2>&1
  "$MESEN" --testrunner $MESENTO "$SNES/build/p8.sfc" "$SNES/test/parity.lua" 2>/dev/null \
    | grep '^OUT HASH' | sed 's/^OUT //' > "$got"
  n=$(wc -l < "$got" | tr -d ' ')
  if [ "$n" -lt "$MINF" ]; then
    echo "HASH FAIL ${name} (only ${n} flips before wall limit, need ${MINF})"; fail=1; return
  fi
  head -n "$n" "$ref" > "${ref}.trunc"
  if diff "${ref}.trunc" "$got" >/dev/null 2>&1; then
    echo "HASH PASS ${name} (${n} flips: FB+RAM bit-identical SNES==host)"
  else
    echo "HASH FAIL ${name}"; diff "${ref}.trunc" "$got" | head -12; fail=1
  fi
}

# SNESPARITY_HASHCARTS (basenames) trims the hash-cart list -- the M9
# roulette battery runs 1-2 carts per perturbed layout, rotating so the
# battery as a whole still covers all six. m4_input always runs.
HASHSET="${SNESPARITY_HASHCARTS:-gfx_lines gfx_shapes gfx_spr gfx_map gfx_pal_fillp gfx_print}"
for g in $HASHSET; do
  run_hash "$g" "$(pwd)/tests/carts/$g.p8" 4 ""
done
run_hash m4_input "$(pwd)/tests/carts/m4_input.p8" 8 "$(pwd)/tests/carts/m4_input.inp"

exit $fail
