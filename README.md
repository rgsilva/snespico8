# snespico8 — PICO-8 carts on a stock SNES

Sister project to [snes86](../emu8086) (8086 PC emulator for the SNES).
Runs real PICO-8 cartridges on unmodified SNES hardware: the z8lua
runtime + PICO-8 API in portable C, compiled for the 65816 with Calypsi,
Mode 7 as a chunky 128x128 framebuffer, SPC700 playing the music in real
time while the game runs in slow motion.

See `docs/PROJECT.md` for the full brief, constraints and milestones.

## Layout

```
core-c/   PC reference core (z8lua + PICO-8 API) — same C source the
          SNES build compiles; correctness oracle + local runner (runp8)
snes/     65816/WLA-DX glue, Mode 7 video, SPC driver, Mesen2 test harness
carts/    .p8 test carts (micro-cart vector suite; BBS carts gitignored)
tools/    third-party toolchains/checkouts (gitignored): Calypsi, z8lua ref
docs/     project brief, design notes
```

## Status

- **M0 COMPLETE — all four kill-or-commit spikes green.**
- M0b: **vanilla Lua 5.2.4 boots and runs on the SNES** (Mesen2, Calypsi
  -O0, ~170 KB ROM) — parses and executes a table/loop test script with
  the correct result. Three Calypsi codegen bugs found, characterized
  and worked around (see `bugs/`).
- M0c: SRAM ground truth: 128 KB usable (header $08, banks $70-$73).
- M0d: Mode 7 chunky-framebuffer path proven and measured — full-screen
  expansion+DMA ≈ 6 fps ceiling unoptimized (`snes/test/m7demo.png`).
  Rendering will not be the bottleneck.
- M1 done: PC reference core — fix32 numbers, full P8 dialect and
  library, all oracle-exact against z8lua (149 vector outputs) and
  against **real PICO-8** (`tests/realvec.sh`).
- M2 done: full graphics API — Celeste Classic (evercore) runs
  deterministically on the reference core, title screen to gameplay.
- M1.5 done: measured ~30K VM ops/frame for Celeste gameplay, ~1.5K
  title; API-heavy carts near-VM-free.
- M3 done: the same core, compiled with Calypsi, boots a cart on the
  SNES (`make -C snes test-p8`) — Mode 7 output, framebuffer pixel-exact
  vs the host oracle. Measured ~1,260 VM ops/sec at -O0; 256 KB SRAM heap.
- M4 done: the SNES build is proven bit-identical to the host core
  (`core-c/tests/snesparity.sh`) — 149 printh vector lines and per-flip
  framebuffer+RAM hashes all match, across 6 gfx carts and a d-pad input
  cart. Two Calypsi codegen bugs caught and fixed along the way.
- **M5 done: Celeste Classic boots and runs on the SNES**
  (`make -C snes test-celeste`) — parses 27 KB of Lua, runs _init + the
  game loop, draws its title through Mode 7. Heap peak ~100 KB, zero
  allocation failures (HiROM 8 KB windows suffice — measured on host
  first via `runp8 -M`, confirmed on hardware). stat(0) + graceful OOM.
- **M6 done: the SPC plays SFX + music in real time while the game loop
  crawls** — the signature effect. The SPC700 runs an autonomous sequencer
  (timer-ticked at ~121 Hz) from the cart's SFX/music streamed into its own
  RAM; the core's `sfx()`/`music()` just send "play" commands. 8 PICO-8
  waveforms as looped BRR, notes in tune to the DSP pitch register. A cart's
  `music(0)` drives the SPC end to end (`make -C snes test-audio`); the SPC
  keeps playing after the game loop stops. Per-tick effects
  (slide/vibrato/drop/fade/arp) are decoded but not yet applied — the
  remaining audio polish.
- Next: M7 (carting/UX — .p8.png+pxa decoder, multi-cart menu, pause menu).
