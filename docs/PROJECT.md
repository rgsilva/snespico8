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

splore/BBS, devkit keyboard (PICO-8 is a joypad machine — `stat(30/31)` is
off by default and few carts use it; 8 buttons can't type without an
on-screen letter-picker, so if a specific cart ever forces the issue it'd
be a d-pad overlay, not a real keyboard), mouse (SNES mouse = fun stretch
goal), GPIO/serial, `cstore` (SRAM-backed later), 8-player, `tline`/newest
0.2.5+ API tail (phased), byte-exact `^` vs real PICO-8 (matches oracle).

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
  - [x] M1.5: **ops/frame MEASURED** (VM instruction counter in lvm.c,
        per-flip in runp8): Celeste title ~1.5K ops/frame, Celeste
        GAMEPLAY ~30K ops/frame (peaks ~60K on transitions); API-heavy
        carts are nearly VM-free (gfx_shapes: 9 ops/frame — drawing
        happens in C). SNES projection at assumed 65816 VM speeds
        (measured for real in M3): Celeste gameplay = ~30s/frame @1K
        ops/sec (-O0 pessimistic), ~10s @3K, ~3s @10K (optimized),
        ~1s @30K (asm dispatch, dream tier). Celeste title and simple
        API-driven carts land at ~0.5-2s/frame — and draw-bound carts
        approach the 6fps display ceiling since their VM load is tiny.
        Conclusion: "slideshow but alive" confirmed; script-light carts
        will feel far better than script-heavy ones.
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
- [x] M3: **the core runs on the SNES.** The exact core-c source
      compiled with Calypsi (5.17, -O0) boots an embedded .p8 cart in
      Mesen2 and renders through Mode 7. `snes/p8main.c`: PICO-8 RAM =
      WRAM $7F (1:1); Lua heap = a runtime-probed set of cart SRAM
      windows + WRAM $7E slice (`snes/p8heap.c`, first-fit region
      allocator); cart assets embedded via `snes/tools/cart2c.py`;
      `flip()` expands $7F:6000 through the LUT and DMAs 16 KB to Mode 7
      tile data; letterbox ported from m7demo (M7SEL transparent +
      window + HDMA). `make -C snes test-p8` boots `m3_boot.p8` and
      asserts 8 framebuffer pixels **byte-exact against the host oracle**
      (`snes/test/p8boot.lua`, screenshot p8boot.png).
      - **SRAM ground truth (HiROM):** the probe finds the **full 256 KB**
        (header $08, 32 distinct 8 KB windows at banks $20-$3F) —
        heap ≈ 268 KB. Caveat vs LoROM: HiROM's 8 KB windows cap a
        *single* allocation at ~8 KB (LoROM's would allow ~32 KB);
        revisit for array-heavy carts in M5.
      - **MEASURED VM speed:** ~**1,260 VM ops/sec** at -O0 (dead-linear
        21 ops/frame on the 3.58 MHz 65816), plus a one-time ~4-5 s
        boot overhead (newstate + parse, before the VM loop starts).
        Replaces the M1.5 assumption at its pessimistic end. Projection:
        Celeste gameplay (~30K ops/frame) ≈ **24 s/frame** at -O0;
        title (~1.5K) ≈ 1.2 s/frame. Headroom untouched: -O2 per-file,
        asm hot paths, SA-1. "Slideshow but alive" confirmed on hardware.
      - **New Calypsi bug** (`bugs/calypsi-const-ptr-offset.c`): folding
        `const_ptr + 0x8000u` sign-extends the offset into the bank byte
        ($7F0000+0x8000 → $7E8000); it silently cleared the mailbox in
        p8_reset. Runtime/indexed access is unaffected (hardware `[dp],y`
        is unsigned), so peek/poke to high memory is fine. Also hit the
        16-bit size_t truncation (memset of 0x10000 → 0). Both fixed by
        chunked pointer-walk clears; -O0 kept as the baseline.
- [x] M4: **SNES build proven bit-identical to the host core.**
      `core-c/tests/snesparity.sh` runs the same carts on both the
      Calypsi-compiled SNES ROM (Mesen + snes/test/parity.lua draining
      the mailbox) and the host reference (runp8), and diffs. Same shared
      source, so a divergence is a compiler bug — and two were caught:
      - **Track 1 (printh):** num 48 + dialect 27 + lib 74 = 149 output
        lines byte-identical SNES==host. Proves fix32 arithmetic, the P8
        dialect, and the whole pico8 library run correctly on the 65816.
      - **Track 2 (framebuffer + machine-RAM hashes):** a shared
        `p8_hash_region` (FNV-1a, runtime-indexed so Calypsi-safe) over
        low RAM [0,0x6000) and screen [0x6000,0x8000) per flip, emitted
        by runp8 `-H` and by snes_flip (gated on `-DP8_PARITY`). 6 gfx
        carts + a d-pad-driven input cart (embedded input via cart2c):
        every compared flip bit-identical. SNES runs are bounded by
        Mesen's ~100 s script limit (per-flip 32 KB hashing is slow at
        -O0), so the suite compares the common prefix (≥3 flips, here 4)
        — enough since a codegen divergence shows on frame 1.
      - Calypsi bugs found (both invisible to host-only testing, fixed in
        shared source, host stays green): **ternary-arg clobber**
        (bugs/calypsi-ternary-arg-clobber.c — a `?:` passed directly as a
        call arg is clobbered by a spill reload at -O0; broke
        tonum(true)); and a latent **sintable OOB** read at index 4096
        (z8lua's own bug, tolerated on the host, exposed on SNES).
- [ ] M5: heap tier — SRAM allocator, GC tuning, stat(0), OOM story;
      real cart on hardware (Celeste in slow motion)
  - [x] M5a: **heap sizing measured → HiROM 8 KB windows suffice for
        Celeste** (no LoROM rework). `runp8 -M <winKB> <totalKB>` runs a
        cart through the p8heap region allocator with a chosen window
        layout (windows gap-separated so coalescing can't cross them),
        reporting peak/largest-alloc/largest-FAILED-alloc. Celeste (host,
        60 frames): peak **~201 KB**, and exactly one allocation >8 KB —
        a 256-node table hash at **10,240 B on the host**. But that's
        pointer-inflated: Calypsi struct sizes are TValue 8 B (host 16),
        **Node 20 B (host 40)**, ptr 4 B (host 8), so on the SNES that
        hash is **256×20 = 5,120 B — it fits an 8 KB window**, and every
        other alloc was already ≤5 KB. SNES peak projects to ~110-130 KB
        (< the 256 KB HiROM heap), and host peak is stable 30→60 frames
        (GC bounds it, no leak). Also fixed a real p8heap bug the host
        measurement exposed: the block-size header was `unsigned long`
        (8 B on a 64-bit host, silently corrupting; accidentally correct
        on the SNES where long is 4 B) → now `uint32_t`.
  - [x] M5b: **stat(0) + graceful OOM.** stat(0) returns real Lua KB
        used (lua_gc GCCOUNT) — truthful for memory-pressure checks, but
        intentionally NOT host<->SNES parity-identical (smaller SNES
        structs), like the raw PRNG state; documented. OOM is already
        graceful via Lua's ERRMEM: a request that no window can satisfy
        -> emergency GC -> LUA_ERRMEM -> caught by the pcall wrappers
        (runp8 prints it / p8main mb_finish(PANIC)); the MAX_SIZET cap
        gives "block too big" the same way. Verified both paths exit
        cleanly (no hang/crash) on host.
  - [x] M5c: **CELESTE CLASSIC BOOTS AND RUNS ON THE SNES.** The full
        cart (27 KB of Lua, 6 KB gfx, 4 KB map) embedded via cart2c into
        a 512 KB ROM; `make -C snes test-celeste` (cart fetched
        separately). Measured on real hardware (Mesen): parses to
        **chunk ok** (~60 s emulated), runs **_init** and the game loop,
        and draws its title screen through Mode 7 (logo, snow, credits).
        **Heap prediction confirmed exactly: peak_used ~100 KB, largest
        single alloc 4 KB, max_fail = 0** — nothing failed, everything
        fit the 8 KB HiROM windows, well within the 256 KB heap. GC keeps
        the heap bounded (peak flat across the game-loop frames) at the
        default settings — no -O0 tuning needed for Celeste. Slow
        (~24 s/frame gameplay, per M3's ~1.26 K ops/s), so "playable" is
        aspirational, but it genuinely boots and runs. Screenshot + the
        Celeste cart are BBS assets (CC-BY-NC-SA), kept local, not
        committed; the harness (`snes/test/celeste.lua`) is.
- [x] M6: **audio — the SPC plays SFX + music in real time while the game
      loop crawls.** The signature design decision, working end to end on the
      SNES. Split into a host model + four SNES stages, each verified in Mesen.
  - [x] M6-host: **PICO-8 audio sequencer model** (`core-c/src/p8audio.c`) —
        the reference sequencer logic (channel allocation, note advance, sfx
        loops, music-pattern chaining) that the SPC mirrors, plus stat(16..24)
        and the sfx()/music() command seam. SFX (0x3200) / music (0x3100)
        memory format VERIFIED against real PICO-8 by peeking memory back
        (`docs/audio-format.md`); note timing uses the zepto8 reference
        constant (speed ticks at 120.5 Hz) since headless PICO-8 doesn't run
        the mixer. `tests/audiovec.sh` goldens the command trace + stat dump.
  - [x] M6a: **APU bring-up** (`snes/p8spc.c/.asm`, `make -C snes test-spc`).
        apu_upload (the IPL handshake) ported verbatim to C from snes86; the
        resident driver (snes86's proven synth) uploaded and driven over the
        $2140-$2143 mailbox. RESULT PASS: voice mask at SPC $00 goes 00->07,
        proving upload + transport + DSP key-ons work.
  - [x] M6b: **8 PICO-8 waveforms as BRR + pitch table** (`tools/mkp8snd.py`).
        triangle/tilted-saw/saw/square/pulse/organ/noise/phaser as looped BRR;
        pitch 0-63 -> DSP P. test-spc keys 4 waveforms at 4 pitches and the
        programmed DSP pitch matches the reference formula EXACTLY (in tune).
  - [x] M6c: **autonomous SPC sequencer** (`make -C snes test-spcseq`). A
        hardware timer (T0 ~121 Hz) ticks the sequencer independently of the
        65816; the cart's SFX/music are streamed into SPC RAM once, then the
        65816 only says "play sfx/pattern". Ported from the host model:
        per-channel row advance, sfx loops, and the music player (pattern
        chaining, loop-start/-end/stop). RESULT PASS: streamed data matches,
        the timer heartbeat advances on its own, the player chains patterns
        0->1->loop, and notes key across channels — 65816 idle throughout.
  - [x] M6-fx: **per-tick effects engine** (`make -C snes test-fx`). All 8
        PICO-8 note effects (word bits 12-14): fades (4/5) via the DSP
        envelope (fade-out = ADSR fast-attack decay-to-zero, fade-in = GAIN
        linear rise, set in note_on); slide (1)/vibrato (2)/drop (3)/arp
        (6/7) via a new seq_fx that reprograms the voice pitch every tick
        (pos = tick/speed; 16x8 mul/div for the ramps; arp reads the 4-note
        group). Verified: drop sweeps voice-0 pitch 173..4269 per note,
        fade-out sweeps its envelope 0..99 — both read back from SPC state.
        Depths/rates ear-tuned vs real PICO-8; the demo melody plucks now.
  - [x] M6d: **real carts drive the audio** (`make -C snes test-audio`). The
        core's sfx()/music() route to the SPC (`snes/p8main.c`
        snes_audio_cmd); boot + stream + register happen in the normal path.
        A normal ROM runs a cart whose _init calls music(0) and the SPC plays
        the song, chaining patterns, in real time — 65816 does nothing after.
        Event-trace parity is inherited from M4: the sfx()/music() command
        stream is deterministic and the SNES runs identical Lua (proven
        bit-identical), so both sides issue the same commands. stat() audio
        position on the SNES reflects the frozen host model (real-time
        position lives on the SPC) — the documented divergence, like time().
- [x] M7: **live controller input — the ROM reads the real SNES pad.** Enable
      auto-joypad ($4200 bit 0); read_pad() decodes $4218/$421A into a PICO-8
      button mask each frame (d-pad -> P8 dirs, O = B or Y, X = A or X) and
      feeds btn()/btnp() -- the first genuinely interactive build. A baked
      cart_input trace still overrides for the deterministic parity/input
      tests; a normal cart (no trace) reads the live pad, and an idle pad
      reads 0 exactly like the old path, so M4 parity is unaffected (test-p8
      still boots pixel-exact vs the oracle). Verified end to end: `make -C
      snes test-input` drives the pad with setInput and m7_input.p8's dot
      moves through all four directions + both face-button columns -- the
      decoded mask ($7E:61B2) and the cart's on-screen response both checked.
      No virtual keyboard: PICO-8 is a joypad machine (devkit keyboard stays
      deferred; a d-pad overlay is the only way it could ever be done, and few
      carts need it).
- [x] M8: carting/UX — .p8.png+pxa decoder, multi-cart menu ROM,
      pause/menuitem, btnp repeat
  - [x] M8a: **multi-cart menu ROM.** `tools/multicart2c.py` bundles N .p8
        carts into one ROM (a `cart_multi.h` descriptor table); `p8main.c`
        built `-DP8_MULTICART` boots a d-pad picker instead of a single cart.
        Each pick runs in a fresh `lua_State`; Select tears it down
        (`lua_close` frees the whole Lua heap back, coalesced) and returns to
        the list. Menu text draws via a new C-side `p8_text()` straight into
        the framebuffer. Verified end to end (`make -C snes test-menu`): draw
        list -> launch m7_input -> live input reaches its `btn()` -> Select
        returns; single-cart path stays pixel-exact (no regression).
  - [x] M8b: **.p8.png loader.** `tools/p8png.py` decodes a real PICO-8 PNG
        export at build time: manual PNG decode (zlib, no Pillow), the
        2-bit-per-channel steganography, and the 'pxa' code decompression
        (move-to-front literals + back-references) -- the pxa bit-encoding was
        hand-derived and verified byte-exact against two real PICO-8 exports.
        `cart2c`/`multicart2c` accept `.p8.png` anywhere a `.p8` goes (one
        shared `parse_cart`); the SNES never sees a PNG -- the ROM gets the
        already-decoded cart. `make -C snes test-png` boots a ROM built
        straight from a `.p8.png` and checks the render. One catch fixed:
        PICO-8 drops the trailing newline on export and the Calypsi lexer
        needs it, so p8png adds it back.
  - [x] M8c: **pause menu + menuitem().** Start opens an in-game overlay
        ("continue" + up to 5 `menuitem()` entries) drawn over the frame;
        d-pad navigates, O/X selects -- a custom item runs its Lua callback
        (staying open if it returns true), "continue"/Start resumes.
        `menuitem()` is a real registry in the core (labels + callback refs,
        reset per lua_State via `p8_menu_*`); the SNES overlay draws with
        `p8_text`/`p8_fillrect` and drives it with the raw pad. Verified end
        to end (`make -C snes test-pause`): Start -> navigate -> select runs
        the callback. Fixed a real input bug found here: a tight pad poll
        could read `$4218` mid auto-joypad-read (garbage), so `pad_raw` now
        waits on `$4212` bit 0 first. **btnp repeat** is the core's
        held-counter repeat (M2, delay/interval at 0x5f5c/0x5f5d), live now
        that M7 reads the real pad. **M8 complete.**
- [ ] M9: perf passes — asm hot paths, measured fps corpus
- [ ] M10: real hardware, stock SNES, flash cart
- [ ] M11: SA-1 tier — 4x clock + 256 KB linear heap
- [x] M12: **documentation pass** — `docs/BUILDING.md` (prerequisites + every
      make target, host suites, the release flow), `docs/cart-formats.md` (the
      `.p8` / `.p8.png` stego + pxa reference hand-derived in M8b), and a
      refreshed `README.md` (a per-milestone status map + doc links). Current
      through M14.
- [x] M13: **demo ROM** (`make -C snes demo-rom`, verify with `test-demo`) —
      a curated multi-cart build on the M8a menu with our own carts (all ours,
      freely shippable): `demo_video` (16-colour palette bars + an animated
      ring of circles), `demo_input` (live joypad readout -- buttons light up
      as held), and the `m6_demo` music demo. Verified in Mesen: the menu
      lists all three and `demo_video` launches + renders the color bars.
      NOT Celeste -- CC-BY-NC-SA (BBS default), kept a local test cart only.
      Adding genuinely free-licensed (CC-0/MIT) community carts is optional
      later, each license checked first.
- [x] M14: **CI + release** (`.github/workflows/build.yml`, `make -C snes
      release`). CI can't build the SNES ROM: Calypsi is licensed for personal
      use only and may not be redistributed into a public runner (unlike
      snes86, which is open-source WLA-DX). So CI runs the HOST side -- it
      builds the z8lua oracle + the portable core and runs the numeric /
      dialect / lib / gfx / audio suites (`make -C core-c check`); green is
      strong coverage since M4 proved the SNES build is bit-identical to host.
      On a `v*` tag it opens a GitHub release with auto notes. The flashable
      ROM is built locally -- `make -C snes release` -> `snespico8.sfc` (our
      runtime + our own demo carts, zero BBS assets, freely redistributable)
      -- and attached to the release by hand.

## Style / working preferences

- Same as snes86: direct, pragmatic, honest tradeoffs; iterative hands-on
  validation over theory; measure before projecting. Comments/docs and
  commit messages in English.
- Owner: Ricardo (Brazil).
