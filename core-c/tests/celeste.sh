#!/bin/sh
# Celeste Classic (evercore modbase) smoke regression. The cart is
# CC-BY-NC-SA and not committed; fetch it first:
#   curl -sL -o tests/carts-external/celeste.p8 \
#     https://raw.githubusercontent.com/CelesteClassic/evercore/main/evercore.p8
set -e
cd "$(dirname "$0")/.."
CART=tests/carts-external/celeste.p8
if [ ! -f "$CART" ]; then
  echo "CELESTE SKIP (cart not fetched)"
  exit 0
fi
./build/runp8 "$CART" -n 60 2>/dev/null | tail -20 > build/celeste.out
if [ "$1" = record ]; then
  cp build/celeste.out tests/goldens/celeste.txt
  echo "recorded celeste"
elif diff -u tests/goldens/celeste.txt build/celeste.out; then
  echo "CELESTE PASS (title-screen hash sequence stable)"
else
  echo "CELESTE FAIL"
  exit 1
fi
