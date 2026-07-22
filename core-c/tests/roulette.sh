#!/bin/sh
# M9 T2 roulette battery. Calypsi's register-allocator miscompiles are
# link-layout-sensitive (per-build roulette -- see micropython-snes
# DECISIONS.md), so ONE green parity run proves nothing for a codegen-
# sensitive change like per-TU -O2. This runs fast parity (all three
# printh vector suites + m4_input + rotating gfx hash carts) across five
# perturbed link layouts; PERTURB pads shift every downstream address.
# The cart rotation means the battery as a whole covers all six gfx
# carts. Aborts on the first divergence. ~25-30 min of bounded,
# sequential Mesen runs.
set -e
cd "$(dirname "$0")/.."

i=0
for n in 7 33 191 1021 3571; do
  i=$((i + 1))
  case $i in
    1) carts="gfx_pal_fillp" ;;
    2) carts="gfx_spr" ;;
    3) carts="gfx_map" ;;
    4) carts="gfx_lines" ;;
    5) carts="gfx_shapes gfx_print" ;;
  esac
  echo "=== roulette layout $i/5: PERTURB=$n hashcarts=$carts ==="
  SNESPARITY_MAKEFLAGS="PERTURB=$n" SNESPARITY_HASHCARTS="$carts" \
    sh tests/snesparity.sh
done
echo "ROULETTE PASS (5 perturbed layouts, all carts covered)"
