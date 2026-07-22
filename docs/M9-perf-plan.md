# M9 — performance plan

Status: **in progress — T0 (measurement) and T1 (pure-C wins) landed.**
This document is the analysis, the tiered optimization plan, the
validation strategy, and the running results table for M9.

T1 notes: gains match the honest sizing (+8.7% VM — per-op arithmetic is
a minority at ~2,100 cycles/op; T2 attacks the dispatch ceremony that
dominates). The T1 gates also caught two latent shift bugs shipped since
M1: `shl/shr/lshr` by `0x8000.0000` never terminated (fix32 self-negation
in the unm-recursion path — a stack overflow on hardware), and fractional
negative counts trunc-rounded where the z8lua oracle floors (`16 << -1.5`
was 8, oracle wants 4). Both fixed non-recursively and pinned by six new
numvec vectors (48 → 54) plus fixfuzz's shift refs. The hookmask gating
idea from the original T1 list was dropped: measured share is inside the
<2.5% counter overhead and it touches Lua debug-hook semantics.

## Where the cycles go (the audit)

The entire runtime compiles at `-O0` (`P8CFLAGS` in `snes/Makefile`) — a
baseline pinned in M0b when two Calypsi `-O2` miscompiles surfaced. Both of
those live in the **lexer** (`bugs/calypsi-comma-seq.c`,
`bugs/calypsi-postinc-index.c`, both in `llex.c`), which runs once at
cart-load. The hot path — `lvm.c`, `ltable.c`, `lgc.c`, `p8num.c`,
`p8gfx.c` — has never been compiled above `-O0`.

Measured/derived baseline:

- ~1,260 VM ops/sec at -O0 (M3) on a ~2.68 MHz effective CPU ≈ **~2,100
  CPU cycles per VM op**.
- Celeste gameplay needs ~30K ops/frame (M1.5) → ~24 s per frame today.
- A flip (expand + 16 KB VRAM DMA) costs **~10 frames** → a ~6 fps ceiling
  for *any* cart, however cheap its Lua (M0d; the pause-menu comment in
  `p8main.c` documents the ~10-frame flip).

Listing-level findings (compiled `p8num.c`/`lvm.c`/`p8.c` with the exact
project flags and read the `.lst`):

- **`-O0` makes trivial fix32 ops far-call storms.** `p8fix_add` — a 32-bit
  add — is ~35 instructions including *three* `jsl` far calls to `U()`/`S()`,
  which are identity cast helpers (not inlined at -O0). At `-O2 --speed` it
  collapses to ~12 instructions. `p8fix_mul` calls the generic `_Mul32`
  software helper four times (one per 16x16 partial).
- **`p8fix_toint` divides by 65536 via `_Div32` — and `-O2` does NOT
  strength-reduce it.** Verified in the -O2 listing. `p8a_int` calls this for
  every integer API argument (every `pset`/`spr`/`map` coordinate), and all
  the shift ops route through it. Needs the manual rewrite (below).
- **The VM dispatch is already a jump table even at -O0** (`lda long:tab,x /
  pha / rts`). No compare-chain problem; per-op cost is operand-extraction
  ceremony (generic variable-shift loops, spill traffic), which -O2 targets.
- **Calypsi defaults to `--space` even at `-O2`.** The sweep must test
  `-O2 --speed` (and `-O1` as fallback granularity). Both compile `lvm.c`
  cleanly — no ICE (the micropython-snes lexer ICE does not reproduce here).
- **The `-O0`-era workarounds survive `-O2` in today's build** — audited
  `p8_reset`'s chunked-pointer loop: the running pointer stays a live loop at
  `$7F:0000`, no refold into the broken `$7E:8000` form. Still per-build
  roulette (see validation), so this gets a mechanical fence, not trust.

Rendering is not the VM bottleneck, but the flip ceiling (~6 fps) is the
binding limit for exactly the carts that *can* reach playability (API-heavy,
near-VM-free) — so the flip path is in scope, not deferred.

## The plan (tiers, in order)

**T0 — measure first.** Fix `snes/test/p8bench.lua` to read `MB_VMOPS` at its
fixed mailbox address (`$7E:F00C`, `calypsi/mailbox.h`) instead of the
hardcoded `.bss` address `0x615F` (the same drift trap that bit test-input in
M8). Add fps + flip-time metrics and a flip-only cart (empty `_draw`). Add an
on-target PC-sampling profile (Mesen `emu.getState()` per frame, symbolized
against `build/p8.map` — the proven micropython-snes technique) and a host
profile of `runp8` for GC share. Corpus: one compute-bound micro-cart, one
draw-bound (spr/map heavy), one API-heavy (`demo_video`), Celeste gameplay
(local-only). Commit a baseline table; every later tier re-runs it and logs
its delta.

**T1 — pure-C wins, host-provable, bit-exact by construction:**

- `p8fix_toint` → `(long)(int)(U(a) >> 16)` — the high word read as signed
  16-bit *is* floor; kills a `_Div32` call per integer argument fetch.
- `luai_numadd/numsub/numunm` + the bitwise ops in `luaconf.h` become direct
  cast-expression macros (`(LUA_NUMBER)((unsigned long)(a)+(unsigned long)(b))`
  etc.) — removes a cross-TU `jsl` + spill ceremony per arith op. Calypsi has
  no LTO, so `-O2` alone can never do this.
- `U`/`S` in `p8num.c` become macros (helps every function at any opt level).
- `p8fix_div` power-of-two-divisor fast path (exact: for pow2 divisors the
  fix32 quotient is a pure shift; magnitude-truncation matches).
- Gate `p8_vmops++` + the hookmask check behind `P8_PROFILE` (kept on for
  test/bench ROMs, off for release). Honest sizing: at ~2,100 cycles/op this
  is <2.5% — a cleanup, not a lever.

**T2 — selective `-O` per file. LANDED, with a humbling outcome.** The
surviving set is `lvm lgc lstring ldo p8num p8 p8gfx` at `-O2 --speed`
(`P8HOT` in `snes/Makefile`); `lua_ltable` is **pinned -O0** — at -O1 AND
-O2 it miscompiles an allocation size during `lua_newstate` ("memory
allocation error: block too big" panic at boot; bisected TU-by-TU, third
documented Calypsi miscompile, `bugs/` reduction pending). Measured: VM
only **+1.8%** (1,374 → 1,399 ops/s), rendering flat, ROM **512 → 256 KB**.
Why so small: the `.lst` shows `luaV_execute` compiles to nearly the same
code at -O2 as at -O0 — Calypsi's allocator effectively gives up inside
the giant re-entrant switch body (the exact micropython-snes pathology),
so the dispatch ceremony never improved. Consequence: **T6's per-opcode
VM split is promoted from contingency to the real VM lever**; T3/T4 (the
user-visible fps levers) come first. The -O2 set stays: free +2%, half
the ROM, and p8num/p8gfx at -O2 are the foundation T3 builds on.

**T3 — gfx inner loops in C.** `p8i_shape_plot`/`p8i_sprite_plot` re-read
camera (two calls), four clip bytes, the fillp pattern and the draw palette
from far RAM *per pixel* — `rectfill(0,0,127,127)` pays it 16K times. Hoist
per-shape invariants, add a span/byte fill path (two pixels per byte store,
drawpal precomputed for both nibbles), and a sprite row-blit. Bit-exact
against the gfxvec goldens; re-profile after T2 first (T2 shifts what's hot).

**T4 — the flip path. LANDED — M9's headline win.** `expand_screen` via a
256-entry const `uint16_t` LUT in ROM (one indexed 16-bit store per
source byte, row addressing hoisted to `sr += 64`). Measured: the empty-
draw game frame fell **~44 → ~11 frames** — the fps ceiling for any cart
went **1.37 → ~5.5 fps (4x)**. Verified pixel-perfect: `test-p8` pixel
asserts + byte-identical Mesen screenshots (demo ROM, old vs new expand).
The remaining ~11 frames ≈ DMA (~1) + LUT walk (~2-3) + per-frame Lua/C
ceremony (`lua_getglobal`/pcall x3, input) — that ceremony is the next
flip-path lever if needed. Optional polish (unclaimed): split the 16 KB
VRAM DMA across vblanks to drop the force-blank flicker.

**T5 — asm leaf functions.** `p8fix_mul` via the CPU `$4202/$4203` 8x8
hardware multiplier (four partials; the CPU multiplier is separate silicon
from the Mode 7 matrix registers `$211B/$211C` that forced `--no-ppu-mul`,
but verify no NMI-path contention). Asm `udiv48`; small-integer-divisor path
via the `$4204` hardware divider. Each A/B-proven bit-identical (below).

**T6 — VM structural (contingency only).** Per-opcode split of
`luaV_execute` (no allocator faces one 21 KB re-entrant body — the
micropython-snes trick) if T2 is layout-fragile; a 5.3-style local-`pc`
cache if post-T2 profiling shows instruction fetch dominating.

When anything sustains 30+ fps: add vblank frame pacing (a nice problem).

## Validation

The existing bit-exact oracle does the heavy lifting; every tier lands only
behind it:

1. **Every step:** `make -C core-c check` (numvec/dialectvec/libvec + gfx/
   audio goldens), then `snesparity.sh` (149 printh vectors + per-flip
   framebuffer *and* RAM hashes, host↔SNES) plus test-p8/input/audio/menu/
   pause.
2. **Roulette battery (T2/T5/T6):** one green run proves nothing — Calypsi
   miscompiles are per-layout. Build the candidate under N (≥5) perturbed
   link layouts (odd-sized pad objects / object reorder) and re-run parity on
   each; plus an extended Celeste soak with per-flip hash compare (short
   parity runs can miss rarely-hit opcodes).
3. **`.lst` fences, wired into every build:** add `--list-file` to
   `P8CFLAGS` (today no listings are generated at all), then fail the link
   on (a) negative-Y far indexing (`ldy ##-n` + `[dp],y` — port
   `check_neg_index.py` from micropython-snes) and (b) any core-c object
   referencing bank `$7E` at offset >= `0x8000` (the const-ptr sign-extension
   misfold signature; constant far stores appear as literal `long:0x7eXXXX`
   operands, so the scan is mechanical).
4. **Host differential fuzzer for every p8num rewrite:** old-vs-new over
   exhaustive (toint: all 2^32 inputs is feasible on host) or large random +
   edge corpora. Decisive, and far stronger than numvec's 48 expressions.
5. **On-target A/B for asm (T5):** a test ROM runs the C oracle and the asm
   candidate over a shared random corpus and mailboxes any mismatch — the
   asm is trusted only after bit-identity on target (the pxa discipline).
6. **Throughput regression:** re-run T0 after each landed step; keep the
   ops/sec + fps table in this file. Anything that doesn't pay gets backed
   out.

## T0 baseline (measured 2026-07-21, all -O0)

Infrastructure: `p8_vmops` now lives AT the `MB_VMOPS` mailbox slot
(`P8_VMOPS_ADDR`, passed in `P8CFLAGS`) so `test/p8bench.lua` samples it
live with no layout drift (the old bench read a `.bss` address that had
already drifted). `make bench-{vm,draw,flip,api,celeste}` + `make prof`
(PC-sampling profiler, `tools/profsym.py`); `tools/lstcheck.py` codegen
fences run before every link. Mesen runs ~0.6x real time headless, so a
bench costs ~2-8 min wall.

| cart | metric | baseline | T1 | T2 | T4 |
|------|--------|----------|----|----|----|
| bench_vm | add-loop VM rate | **1,264 ops/s** (M3 continuity confirmed) | **1,374** (+8.7%) | 1,399 (+1.8%) | — |
| bench_vm | mul-loop VM rate | ~620-670 ops/s | ~690 | — | — |
| bench_flip | game-frame, empty `_draw` | **~44 frames (~730 ms) → 1.37 fps ceiling** | — (T4 target) | unchanged | **~11 frames → ~5.5 fps (4x)** |
| bench_draw | fills/lines/circles+print | ~0.23 fps (16 flips/4.5K frames) | 17 flips (+6%) | 16 (noise) | 18 (+12%) |
| demo_video (api) | | ~0.27 fps (20 flips/4.5K frames) | 22 flips (+10%) | 21 (noise) | **25 (+19%)** |
| ROM size | | 512 KB | 512 KB | **256 KB** | 256 KB |
| celeste | parse 27 KB | ~3,540 frames ≈ 59 s |
| celeste | `_init` VM rate | ~330-400 ops/s (1,848 ops total) |
| celeste | first title `_draw` | multi-minute C grind (full-screen `map()`), 0 flips in budget |
| host (clang -O2) | GC share, gameplay | ~1% — negligible, GC tuning deprioritized |

PC profiles (5,000 frames from reset, `make prof`):

- **bench_flip: `expand_screen` = 91.6%** of the whole empty-draw loop
  (~40 of the 44 frames; DMA 0.8%, per-frame Lua/C ceremony ~1 frame
  total). The M0d "~10-frame flip" was the -O2 spike; the -O0 runtime
  version costs ~4x that. T4 is the single biggest user-visible lever.
- bench_vm: `luaV_execute` 43.3%, `expand_screen` 26.1% (post-compute
  flip tail), `_Mul16`+`_Mul32` 18.0% (mul phase), `p8fix_add` 4.1%.
- bench_draw: `p8i_shape_plot` 37.2%, `expand_screen` 16.2%,
  `p8i_screen_set` 10.3%, `memset` (cls) 7.1%, `p8i_cam_x/y` 7.7%,
  p8gfx static walkers ~7%, `_UDivMod32` 1.7% — T3's hoist list, verbatim.
- celeste parse: `_UDivMod32` 12.6% (ltable `hashnum` mod during
  constant dedup) + `_ValueSwitch16` 11.8% (llex/lparser switch dispatch)
  + `_Mul32` 6.0% + `_Mul16` 2.3% ≈ **33% in Calypsi runtime helpers**;
  rest is luaK/luaX/luaH/luaS machinery. llex/lparser stay -O0 (the known
  miscompiles), but `--force-switch` on them at -O0 is a candidate
  parse-time win; measure in T2. (A pow2-mask table hash would kill the
  `_UDivMod32` but changes `pairs()` order → breaks z8lua parity. No.)
- host celeste gameplay: table machinery dominates even on x86/ARM —
  notably `all_iter` calls `lua_rawlen`→`luaH_getn` (binary search) per
  iteration of every `all()`/`foreach` loop. Candidate pure-C win, but
  only with oracle-exact iteration semantics; parked for T3-era.

## Honest ceiling

T1+T2 plausibly lands in the 400–700 cycles/op range → **4–8K ops/sec**
(3–6x); T5/T6 could reach 10–20K. Real-time Celeste needs ~900K ops/sec
(30K ops x 30 fps) — out of reach on stock hardware; that stays M11 (SA-1).
The honest M9 outcome: simple carts playable, API-heavy carts at real frame
rates via the flip fix, Celeste demoted from slideshow to slow motion.
