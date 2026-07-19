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
- Next (M1): PC reference core — vanilla Lua C + z8lua dialect diff
  re-applied in C (fix32), .p8 loader, headless `runp8`, micro-cart
  vector suite with goldens from real PICO-8.
