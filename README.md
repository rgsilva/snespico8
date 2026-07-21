# snespico8 — PICO-8 carts on a stock SNES

Sister project to [snes86](../emu8086) (8086 PC emulator for the SNES).
Runs real PICO-8 cartridges on unmodified SNES hardware: the z8lua
runtime + PICO-8 API in portable C, compiled for the 65816 with Calypsi,
Mode 7 as a chunky 128x128 framebuffer, SPC700 playing the music in real
time while the game runs in slow motion.

## Layout

```
core-c/   PC reference core (z8lua + PICO-8 API) — same C source the
          SNES build compiles; correctness oracle + local runner (runp8)
snes/     65816/WLA-DX glue, Mode 7 video, SPC driver, Mesen2 test harness
carts/    .p8 test carts (micro-cart vector suite; BBS carts gitignored)
tools/    third-party toolchains/checkouts (gitignored): Calypsi, z8lua ref
docs/     project brief, build guide, format + design notes
```

## Documentation

- [`docs/PROJECT.md`](docs/PROJECT.md) — full brief, constraints, milestone log.
- [`docs/BUILDING.md`](docs/BUILDING.md) — prerequisites and every make target.
- [`docs/cart-formats.md`](docs/cart-formats.md) — `.p8` and `.p8.png` (stego + pxa).
- [`docs/audio-format.md`](docs/audio-format.md) — SFX/music memory format + timing.
- [`docs/M9-perf-plan.md`](docs/M9-perf-plan.md) — the M9 performance plan (audit, tiers, validation).

## Milestones

| # | milestone | status |
|---|-----------|--------|
| M0 | kill-or-commit spikes | ✅ |
| M1–M2 | PC reference core (fix32, full API, Celeste on host) | ✅ |
| M3–M4 | core on the SNES, **bit-identical** to host | ✅ |
| M5 | heap tier — Celeste boots on the SNES (slow) | ✅ |
| M6 | real-time SPC audio + all note effects | ✅ |
| M7 | live controller input | ✅ |
| M8 | carting/UX — menu, `.p8.png`, pause menu + `menuitem()` | ✅ |
| M9 | perf passes | ⬜ |
| M10 | real hardware | ⬜ |
| M11 | SA-1 tier | ⬜ |
| M12 | documentation pass | 🔶 |
| M13 | demo ROM | ✅ |
| M14 | CI (host suites + builds the ROM) + release | ✅ |

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
  keeps playing after the game loop stops. All 8 per-tick note effects
  (slide/vibrato/drop/fade-in/out/arp) run SPC-side too — fades via the DSP
  envelope, pitch effects via per-tick reprogramming (`make -C snes test-fx`).
  A loadable demo (a melody that plucks + a bass line, over a slow-updating
  screen) builds with `make -C snes demo` — open `snes/build/p8.sfc` in Mesen
  with sound on.
- **M7 done: the ROM reads the real SNES controller** (`make -C snes
  test-input`). Auto-joypad on; `read_pad` decodes `$4218/$421A` into
  `btn()`/`btnp()` each frame (d-pad → dirs, O = B/Y, X = A/X) — the first
  interactive build. Baked input traces still drive the parity tests, so M4
  stays pixel-exact. No virtual keyboard: PICO-8 is a joypad machine.
- **M8 done — carting/UX**: a **multi-cart menu ROM** (`make -C snes menu`)
  bundling carts, each run in a fresh `lua_State` (Select returns); a
  **`.p8.png` loader** (`make -C snes test-png`) that decodes real PICO-8 PNG
  carts at build time (stego + hand-rolled pxa), so `.p8.png` works anywhere
  `.p8` does; and a **Start pause menu** with `menuitem()` callbacks
  (`make -C snes test-pause`). btnp repeat rides M7's live input.
- **Demo ROM** (`make -C snes demo-rom`): the menu curated with our own test
  carts — a graphics test, a live joypad readout, and the music demo (all
  ours, freely shippable; `make -C snes test-demo` verifies it).
- **Release + CI**: GitHub CI (`.github/workflows/build.yml`) builds it all —
  the host suites (`make -C core-c check-ci`) *and* the flashable ROM (it
  installs Calypsi from its official release + builds WLA-DX). A `v*` tag
  publishes a release with `snespico8.sfc` attached; the CI ROM is
  byte-identical to a local `make -C snes release`.
- Next: M9 (perf) — the swing toward playable. Planned in
  [`docs/M9-perf-plan.md`](docs/M9-perf-plan.md): the runtime still builds at
  -O0; the plan is measure → pure-C wins → selective -O2 (parity-gated) →
  gfx/flip paths → asm leaves. Honest ceiling: simple carts playable,
  API-heavy carts at real frame rates; real-time Celeste stays M11 (SA-1).
