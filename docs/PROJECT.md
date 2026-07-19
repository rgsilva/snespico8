# snespico8 — a PICO-8 runtime for the SNES

## Project goal

Run real PICO-8 cartridges on a **stock, unmodified SNES**, delivered as a
plain flashable ROM (cart baked in). Sister project to snes86 (`../emu8086`),
same philosophy: novelty/art project, honesty about speed, everything
verified in Mesen2 with an automated parity harness before hardware.

**This is not a CPU emulator.** PICO-8 has no public bytecode ISA — carts
contain P8-Lua *source*. "Running PICO-8" means implementing the Lua 5.2
language runtime (VM, GC, tables, strings, closures, coroutines) plus the
~60-function game API. That is a *runtime reimplementation*, the same
category as zepto8 and fake-08 — precedent Lexaloffle has tolerated for
years (runtimes yes, editor clones no; we ship no Lexaloffle assets or
carts; the PICO-8 font and palette are CC-0).

## The enabling decision: C on the 65816 via Calypsi

Hand-writing a Lua runtime in 65816 asm would be 5-10x the whole snes86
core. Instead the core is **portable C compiled with Calypsi**
(`--target=SNES`, C99, 32-bit ints, 24-bit far pointers, setjmp; closed
source, free for hobby use). Existence proof: **micropython-snes** runs
MicroPython on a stock SNES with this exact stack. Known landmines from
that project: ~23 codegen bugs root-caused during development; giant
switch statements inside setjmp frames miscompile (exactly the shape of
Lua's `luaV_execute` — split dispatch into per-opcode functions if hit).

The Lua dialect comes free: **z8lua** (samhocevar, MIT, Lua 5.2.4 fork,
~96% C) already implements PICO-8's language — 16.16 fixed-point (fix32)
numbers, `+=` family, `!=`, short-if, `?`, binary literals. No floating
point anywhere except the `^` operator, which must be replaced with a
fixed-point pow. The 8086-project pattern upgrades: here the PC reference
core and the SNES build are the *same C source*, so parity divergence
means compiler/glue bugs, not reimplementation drift.

tcc-816 (pvsneslib) was evaluated and rejected for the core: 16-bit-only
pointer arithmetic (no >64KB objects, no bank crossing), pointer<->int
loses the bank byte, documented miscompiles. Asm glue stays WLA-DX like
snes86.

## Why the specs fit the SNES weirdly well

- **PICO-8 addressable RAM is exactly 64 KB** (0x0000-0xFFFF incl. the
  0x8000+ extended region, 0.2.4+) → WRAM bank $7F verbatim:
  pico address `0xNNNN` = `$7F:NNNN`. peek/poke/memcpy are single
  long-indexed ops. Screen sits at $7F:6000-7FFF in PICO-8's own format
  (2 px/byte, 4-bit linear).
- **All numbers are 16.16 fixed point.** Zero floats in the whole system.
- **Mode 7 is the SNES's only chunky (byte-per-pixel) mode**, and a
  128x128 screen is exactly 256 unique Mode 7 tiles (16 KB). Render path:
  expand the 8 KB PICO-8 framebuffer through a 256-entry LUT (1 byte =
  2 px → 2 bytes) and DMA to VRAM high bytes. No planar bit-twiddling.
  Out-of-map area = transparent → backdrop color letterbox.
  CAVEAT: Mode 7 matrix regs $211B/$211C double as the PPU multiplier
  that Calypsi's SNES target uses → compile with `--no-ppu-mul` (or
  restrict to vblank).
- Palette: 16 CGRAM entries from the 32-color master palette. Screen-level
  `pal(...,1)` = CGRAM rewrite; draw-palette, `palt`, `fillp` in software.
- Input: d-pad + O/X on face buttons, Start = pause menu. 2 controllers.
- PRNG is documented (two 32-bit words, state peekable at 0x5f44) →
  seeded lockstep testing works.

## Hardware budget (stock SNES, phase 1)

- **WRAM $7F (64 KB)**: the PICO-8 machine RAM, 1:1.
- **WRAM $7E (64 KB)**: C stack (Calypsi software stack), runtime
  .data/.bss, expansion strip buffer, NMI/DMA queues, DP hot state.
  Leftover joins the Lua heap pool.
- **Cart SRAM**: the Lua heap. Mesen2 source caps at 256 KB (header $08);
  snes86 empirically measured only 64 KB usable from a $07 request —
  discrepancy unresolved, M0 spike re-probes. LoROM SRAM comes in 32 KB
  bank windows → allocator caps single allocations at 32 KB (flat Lua
  arrays beyond ~4-8K entries fail; rare, documented limitation).
- **ROM**: Calypsi-compiled runtime + WLA glue + font + cart assets +
  cart code. LoROM FastROM, up to 4 MB. Cart's 32 KB asset block is
  copied to $7F at reset.
- PICO-8 spec allows **2 MB** Lua heap; we offer 64-256 KB (+WRAM
  leftovers). Small/mid carts fit; heap-hungry carts OOM. Per-cart
  limitation, not a design failure. clip: single alloc ≤ 32 KB.

## Cart pipeline (PC side)

`.p8` (text) first, `.p8.png` later: PNG stego (2 bits/channel ARGB) +
pxa decompression are fully specified on the PICO-8 wiki
(P8PNGFileFormat) with two open reference decoders (zepto8 WTFPL,
fake-08 MIT). The build tool extracts the 32 KB asset block + Lua
source and embeds both in the ROM.

Cart code strategy is **gated by the SRAM spike**:
- ≥256 KB heap → compile source *on-device at boot* (z8lua's parser comes
  along anyway; gives `load()` support free).
- Only ~64 KB → cross-compile on PC with `luac` and freeze
  protos/constants into ROM (eLua/MicroPython-frozen-modules style; more
  work, saves ~40-80 KB heap for a token-limit cart).

## Audio

SPC700 driver derived from snes86's `spc_midi.asm` + `apu_upload` IPL
loader (lift ~80%): PICO-8's 8 waveforms become small looped BRR samples
(noise via DSP noise generator; phaser and SFX-instruments are the hard
cases), 4 voices, sequencer ticking SFX rows at 120 Hz with effects
(slide/vibrato/drop/fade/arp) computed SPC-side.

**Design decision: audio runs in REAL time while the game runs in slow
motion.** Music sounds correct while gameplay crawls — the demo effect.
Consequence: `time()` is virtual (presented-frame-counted) for game
logic; audio/game-time divergence accepted and documented.

## Performance expectations (be honest in all docs/commits)

- PICO-8 spec: 4M-8M Lua VM ops/sec. Calypsi-compiled Lua on a 3.58 MHz
  65816: est. 2-10K ops/sec → **~400-2000x slower than real PICO-8**
  before optimization. Seconds per frame on typical carts; asm hot paths
  (VM dispatch, table access, blitters) might reach ~1 fps on simple
  carts. MEASURE, don't guess: M1.5 counts ops/frame on a cart corpus
  with the PC core, M3 measures per-op cost on the SNES, projection from
  numbers.
- Escape hatch tier 2: **SA-1** — same 65816 ISA at 10.74 MHz + 256 KB
  linear BW-RAM, emulated by Mesen2. Compiled C runs unchanged (linker
  script + memory map only; swap PPU-mul for SA-1 math regs). ~4-6x,
  plus the whole heap problem relaxes.

## Test pyramid (inherited from snes86, upgraded)

1. **PC reference core** (`core-c/`): z8lua + API in portable C, built
   for the host. `runp8` local runner: headless, per-flip framebuffer
   hash + PNG dump, scripted input traces, optional SDL window later.
   Oracle for goldens: real PICO-8 (Ricardo owns it) and/or zepto8.
2. **Same C source compiled for SNES**, glue in WLA-DX. Mesen2
   `--testrunner` + Lua harness reused wholesale from snes86: WRAM
   snapshot → Lua print → python diff; hex-over-stdout screenshots;
   `RESULT PASS/FAIL` convention; input injection via `emu.setInput`.
3. **Parity**: per-flip hash of framebuffer + 64 KB machine RAM on both
   sides, seeded `srand`, scripted inputs. Micro-cart vector suite (one
   cart per API function / language feature) instead of CPU vectors.
4. Real hardware last (flash cart, 128 KB SRAM keeps it real-cart
   plausible).

## Unsupported / deferred (phase 1)

splore/BBS, devkit keyboard, mouse (SNES mouse = fun stretch goal), GPIO/
serial, `cstore` (SRAM-backed later), 8-player, `tline`/newest 0.2.5+ API
tail (phased), byte-exact `^` vs real PICO-8 (matches our oracle instead).

## Milestones

- [ ] M0: kill-or-commit spikes
  - [x] M0a: **Calypsi C runs on the SNES** (`snes/hello.c`, `make test-hello`).
        Proven: .data/.bss init, setjmp/longjmp, far pointers, function
        pointers (incl. in far structs/heap). Toolchain vendored in
        `tools/calypsi-65816-5.17` (5.18 also present; both share the bugs
        below). Setup cribbed from micropython-snes (MIT): header.s,
        linker.scm (HiROM), mailbox protocol, raw2sfc.py.
  - [x] M0b: **vanilla Lua 5.2.4 BOOTS AND RUNS ON THE SNES** — all 32
        files compile; ~150-170 KB ROM; `lua_newstate` + parse + execute
        of a table/loop script returns the right answer in Mesen2
        (`tools/lua-v52/` worktree, b17 build). Cost: a Calypsi bug hunt
        exactly like micropython-snes predicted — see `bugs/` ledger:
        (1) int64 multiply/shift runtime helpers broken (hi-word) → no
        int64 in fix32 paths, use 16x16 partials (proven exact in hello.c);
        (2) unsigned 16-bit constant compares/divisions fold as SIGNED →
        MAX_SIZET capped to 0x7FFF (single Lua alloc ≤ 32 KB — matches the
        SRAM window design anyway);
        (3) -O2 sequencing miscompiles (comma-expr in save_and_next,
        post-inc index, one more unreduced) → **-O0 is the correctness
        baseline**, optimize selectively later under parity coverage.
        Target facts: size_t/unsigned int are 16-BIT (lu_mem forced to
        unsigned long or GC counters wrap); time() hangs (no RTC) →
        deterministic luai_makeseed (good for parity anyway).
        z8lua itself is C++ (fix32 struct, builds with g++) → plan is
        vanilla-Lua-C base + re-apply the z8lua dialect diff in C
        (`git diff v5-2..pico8` = only ~1700 lines, 411 of them tables).
  - [x] M0c: SRAM ground truth (`snes/sramprobe.asm` + `test/sramprobe.lua`):
        Mesen2 backs **half the header request** through the LoROM window —
        $05→32 KB, $07→64 KB (matches snes86's finding), **$08→128 KB in
        banks $70-$73** (the max; WLA-DX rejects $08 so the header byte is
        patched post-link). Hardware-compatible: a real 128 KB chip maps
        those same banks. Lua heap budget ≈ 128 KB SRAM + WRAM leftovers.
  - [x] M0d: **Mode 7 framebuffer path proven** (`snes/m7demo.asm`,
        `make test-m7demo`): PICO-8-format 8 KB FB → word-LUT expansion
        into a tile-major 16 KB buffer → DMA to Mode 7 tile data (VRAM
        high bytes), 16x16 identity tilemap, PICO-8 palette in CGRAM,
        letterbox via M7SEL over=transparent + window 1 (right) + HDMA
        TM-off (bottom). VRAM pixel spot-checks green; screenshot
        test/m7demo.png. MEASURED: full expansion ~9.4 frames (~156 ms)
        naive/unoptimized; full 16 KB DMA ~0.37 frames (~6 ms). Display
        ceiling ≈ 6 fps full-screen — far above VM speed, so rendering
        won't be the bottleneck. Known nit: one-line artifact at the
        bottom HDMA boundary (off-by-one, cosmetic).
- [ ] M1: PC reference core (`core-c/`)
  - [x] M1a: **skeleton runs**: vendored vanilla Lua 5.2.4 (from z8lua's
        v5-2 branch) with the 3 Calypsi workarounds baked in (MAX_SIZET
        0x7FFF also on host → identical 32KB alloc limit both sides);
        p8.c machine (64KB RAM, draw state AT the documented 0x5f00
        addresses), .p8 text loader (__lua__/__gfx__/__map__/__gff__),
        headless `runp8` (per-flip FNV hash, PPM dump, input scripts).
        `make test` renders the m7demo stripe pattern from a cart.
  - [x] M1b: **fix32 numbers, oracle-exact**: lua_Number = int32 16.16
        raw bits; all ops via src/p8num.c (no int64 — partial-product
        mul, 48-bit shift-subtract div/sqrt, sqrt-chain fractional pow);
        luaconf/llimits/lobject/lauxlib/lparser rewired (str2fix parses
        dec/hex/0b with fractions; fix2str matches PICO-8 rounding incl.
        "-0"; cast_num scales; default for-step was the classic raw-1
        trap). z8lua built as the host oracle (`tools/z8lua/z8lua`):
        `tests/numvec.sh` — **48/48 expressions byte-identical** incl.
        x/0, wrap, mod signs and x%0==0, (-2)^0.5==0, 2^-0.5.
        Framebuffer hash unchanged across the double→fix32 swap.
  - [x] M1c: **P8 dialect + pico8 lib, oracle-exact.** z8lua's dialect
        diff (v5-2..pico8) applied to the vendored tree and de-C++'d;
        C ports: lpico8lib.c (trigtable sin/cos/atan2 — atan2's int64
        math redone with 48-bit dividers —, tostr/tonum/chr/ord/split,
        plus `sub` which z8lua lacks), luaV_peek widened to 64 KB.
        Our own (z8lua has none): add/del/deli/all/foreach/count,
        rnd/srand with the documented PRNG state at 0x5f44 (in RAM,
        peekable, deterministic — VERIFY sequence vs real PICO-8),
        peek/poke/2/4, memcpy/memset, cocreate/coresume/costatus/yield
        aliases. Vector suites vs the z8lua oracle: `tests/numvec.sh`
        48/48, `tests/dialectvec.sh` 27/27 (+= family, \, ^^, <<>, >><,
        >>>, short-if, !=, ?), `tests/libvec.sh` 72/72. Cart hash still
        c5a1d7e5 through it all.
  - [ ] M1.5: ops/frame measured over a cart corpus → SNES projection
- [x] M2: **full gfx API on the PC core — Celeste Classic runs.**
      `src/p8gfx.c`: line (with 0x5f3c endpoint state), rect(fill),
      circ(fill), oval(fill), spr (flips, fractional w/h), sspr, map +
      mget/mset (incl. the 0x1000 shared region), sget/sset, fget/fset,
      pal (draw+screen+table forms, transparency bit preserved), palt
      (incl. bitfield form), fillp (two-color + transparent-secondary,
      via the shared shape-pixel pipeline), cursor, print (P8SCII font
      from fake-08's grid via tools/mkfont.py, wide glyphs 128+, \n,
      cursor-mode scrolling, returns next x), btnp (held counters,
      0x5f5c/0x5f5d delay/repeat), unpack; stubs: music/sfx/menuitem/
      cstore/reload/extcmd (no-op), cartdata/dget/dset (0x5e00 RAM, no
      persistence), stat (always 0). Tests: `tests/gfxvec.sh` — six
      micro-carts, per-flip hash goldens (self-regression; VERIFY tags
      mark semantics to pin against real PICO-8: circle/oval shapes,
      line-state edge cases, pal() reset scope, print/fillp interaction,
      btnp 0-means-default, sspr negative sizes, P8SCII control codes
      unimplemented). Smoke test: **evercore.p8 (vanilla Celeste Classic
      modbase) runs deterministically** — title screen and level 1
      render correctly (`tests/celeste.sh`, cart fetched separately,
      not committed). Sound remains M6.
- [ ] M2: full gfx API on PC (spr/sspr/map/pal/palt/fillp/clip/camera/
      P8SCII print subset, peek/poke/memcpy semantics); real BBS carts run
      headless; Celeste classic flagship
- [ ] M3: SNES bring-up — same core via Calypsi, $7F machine RAM, Mode 7
      out, WLA glue; hello.p8 boots in Mesen2
- [ ] M4: parity harness — scripted inputs, per-flip FB+RAM hashes both
      sides, suite.sh convention, vector suite green on SNES
- [ ] M5: heap tier — SRAM allocator (32 KB windows), GC tuning, stat(0),
      OOM story; Celeste playable-in-slow-motion (milestone photo)
- [ ] M6: audio — SPC driver + sequencer, real-time; event-trace parity
- [ ] M7: carting/UX — .p8.png+pxa decoder, multi-cart menu ROM,
      pause/menuitem, btnp repeat
- [ ] M8: perf passes — asm hot paths, measured fps corpus
- [ ] M9: real hardware, stock SNES, flash cart
- [ ] M10: SA-1 tier — 4x clock + 256 KB linear heap

## Style / working preferences

- Same as snes86: direct, pragmatic, honest tradeoffs; iterative hands-on
  validation over theory; measure before projecting. Comments/docs and
  commit messages in English.
- Owner: Ricardo (Brazil).
