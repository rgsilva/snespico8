#!/bin/sh
# Audio sequencer goldens: run the m6_audio cart and capture the two
# DETERMINISTIC audio signals — the sfx()/music() command trace (the
# host<->SNES parity signal) and the per-sample stat() dump (channel +
# pattern state as the sequencer advances). The per-tick NOTE stream is
# intentionally not goldened (verbose, and its exact timing is a host-only
# preview detail — the SPC plays it in real time). Real PICO-8's audio
# mixer does not run headless, so this is a self-regression golden pinned
# to the format/logic that WAS verified against PICO-8 (docs/audio-format.md).
# Rerecord: sh tests/audiovec.sh record
set -e
cd "$(dirname "$0")/.."
mode="$1"
cart=tests/carts/m6_audio.p8
out=build/m6_audio.out
{
  ./build/runp8 "$cart" -n 90 -A 2>/dev/null | grep -E '^(SFX|MUS)'
  ./build/runp8 "$cart" -n 90 2>&1 >/dev/null | grep -E '^f[0-9]'
} > "$out"
if [ "$mode" = record ]; then
  cp "$out" tests/goldens/m6_audio.txt
  echo "recorded m6_audio"
elif diff -q tests/goldens/m6_audio.txt "$out" >/dev/null 2>&1; then
  n=$(wc -l < "$out" | tr -d ' ')
  echo "AUDIO PASS (m6_audio, $n command+stat lines)"
else
  echo "AUDIO FAIL"
  diff -u tests/goldens/m6_audio.txt "$out" | head -24
  exit 1
fi
