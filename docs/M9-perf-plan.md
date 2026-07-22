# M9 — performance plan

Status: **T0–T6 landed — M9 complete — plus the T7 reevaluation
addendum (see the T7 section at the end).** (Commits `7f609e9..HEAD`;
deferred polish lives in `FUTURE.md`: vblank pacing, DMA split, sprite
row-blit, `--force-switch` parse experiment, flip-ceremony diet, `bugs/`
reductions.) This document is the analysis, the plan, the validation
strategy, and the results table.

Cumulative: fps ceiling 1.37 → ~5.5 (4x), draw-bound carts ~3x,
demo_video 20 → 43 flips (2.2x), VM add +11% and mul +38%, ROM halved
to 256 KB, three latent core bugs fixed (two never-terminating shift
paths, one z8lua divergence), **three new Calypsi miscompiles found and
pinned by the M9 gates** (ltable -O1/-O2 allocation-size; -O2 volatile
deletion through cast-constant pointers; -O0 dropped truncation in
casted shifts inside 16-bit expressions).

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

**T3 — gfx inner loops in C. LANDED.** Every shape/sprite walker now
snapshots the draw state once per API call into a `P8Ctx` (camera, clip,
fillp, palette — `p8int.h`) and plots through it; the pen's two fillp
nibbles resolve through the palette up front (walkers draw one color per
call), so a shape pixel is down to camera-subtract + clip-compare +
fillp-bit on direct-page struct fields plus one screen-byte RMW.
`rectfill` additionally takes a byte-wise span path when no fill pattern
is active (2 px/byte, odd-edge nibble RMW, `memset` middle). Semantically
identical by construction — plotting can only write screen bytes, never
draw state. Measured: **bench_draw 18 → 49 flips (2.7x, ~0.7 fps)**,
demo_video 25 → 43 (1.7x). Gates: all six gfxvec goldens bit-identical
on host, SNES parity hash carts bit-identical. Sprite ROW-blit (byte-wise
sheet walk) left unclaimed — per-pixel `ctx_sprite` was enough for now.

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

**T5 — hardware math units. LANDED** (as C helpers over the units, not
raw asm — the win is the silicon, not the mnemonics). `p8fix_mul`'s four
16x16 partials on the CPU multiplier (`$4202/$4203`, four 8x8 triggers
each); `p8fix_div` small-integral-divisor path (`b = k` in 1..255) as
four chained 16/8 hardware divides — the identity `(ua<<16)/(k<<16) =
ua/k` makes it exact. Host keeps native expressions (`MUL16` macro), so
core-c stays portable. Measured: **mul phase +38%** (~690 → ~950 ops/s).
The A/B ROM (`make test-abmath`) caught **two fresh Calypsi miscompiles**
en route: (5th) -O2 deletes volatile accesses through cast-constant
pointers → `hwmath.c` pinned -O0; (6th) -O0 drops the truncation in
`(uint8_t)(u32 >> K)` inside 16-bit expressions, doing an unmasked wide
load → byte operands pre-masked into 16-bit locals. Wait margins are 3x
documented latency, re-proven per build by the corpus. Diagnostic
divider probes stay compiled-but-stripped for M10 hardware bring-up.

**T6 — the per-opcode VM split. LANDED — and it settled the question.**
`luaV_execute`'s frame state moved into a `VMCtx` struct; 28 opcode
bodies became per-op functions (verbatim ports, `ProtectV` keeping
`v->base` authoritative); `JMP` and `FORLOOP` were extracted, measured,
and put back inline (bodies too small to amortize a call — the bench
regressed 5% and recovered on revert; the bench decides, not theory).
Result: add-loop 1,399 → **1,485 ops/s (+6%)**, and a clean lvm.c-only
A/B on a gameplay-shaped cart (table reads + tests + adds in celeste's
histogram proportions) measured **+1.7%** (1,012 vs 995 ops/s).

**The finding that matters:** the VM floor on a stock 65816 is
**memory-bound, not codegen-bound**. Every op moves 6-byte TValues
through 24-bit far pointers; no allocator quality can delete those bus
cycles, which is why neither -O2 (T2) nor the split (T6) moved the
needle much. The split stays anyway: it is strictly ≥ baseline, it is
the shape that avoids the known giant-switch miscompile roulette, and
per-op bodies are individually asm-able later. But the next real VM
multiplier is a faster memory system — that is the SA-1 tier (M11),
exactly where the original plan's honest ceiling pointed.

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

| cart | metric | baseline | T1 | T2 | T4 | T3 (cumulative) | T5/T6 (final) |
|------|--------|----------|----|----|----|----|----|
| bench_vm | add-loop VM rate | **1,264 ops/s** (M3 continuity confirmed) | **1,374** (+8.7%) | 1,399 (+1.8%) | — | 1,399 | **1,485 (T6, +6%)** |
| bench_vm | mul-loop VM rate | ~620-670 ops/s | ~690 | — | — | — | **~950 (T5, +38%)** |
| bench_flip | game-frame, empty `_draw` | **~44 frames (~730 ms) → 1.37 fps ceiling** | — (T4 target) | unchanged | **~11 frames → ~5.5 fps (4x)** | ~11 frames | ~11 frames |
| bench_draw | fills/lines/circles+print | ~0.23 fps (16 flips/4.5K frames) | 17 flips (+6%) | 16 (noise) | 18 (+12%) | **49 (3.1x, ~0.7 fps)** | 49 |
| demo_video (api) | | ~0.27 fps (20 flips/4.5K frames) | 22 flips (+10%) | 21 (noise) | **25 (+19%)** | **43 (2.2x, ~0.6 fps)** | **45 (2.25x)** |
| ROM size | | 512 KB | 512 KB | **256 KB** | 256 KB | 256 KB | 256 KB |

(Columns are chronological: T4 landed before T3; each column includes
everything to its left.)
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

## T7 — the reevaluation pass (2026-07-22, post-M9 addendum)

M9 closed on the memory-bound verdict: -O2 on the giant switch (T2,
+1.8%) and the per-op split (T6, +1.7%) both null. A fresh read of the
T6-final *listings* — not the C — showed why both A/Bs were blind:
every configuration ever measured carried the same removable overhead
classes, so no experiment isolated them. The verdict survives (the
TValue traffic is real); what it hid was ~1/3 of the per-op budget in
costs that were never on either side of an A/B ledger.

| inc | change | bench_tab | cumulative |
|-----|--------|-----------|------------|
| — | T6 final baseline | 1,012 ops/s | — |
| 1 | `--no-cross-call --no-interprocedural-cross-jump` (all runtime TUs) | 1,158 | +14.4% |
| 2 | byte-wise GETARG decode + shell `ra` sink + `P8_NO_HOOKS` (+ release ROMs drop the vmops counter) | 1,453 | +43.6% |
| 3 | table/compare far-call diet + `ltable_get.c` split (`luaH_get` finally at -O2) | 1,737 | +71.6% |
| 4 | direct-table `all()`/`foreach` step | bench_all: 380 → 659 (+73%) | tab: 1,736 (flat) |
| 5 | host bytecode precompile (`P8BYTECODE=1`, opt-in) | celeste boot ~65 s → ~21 s (3.1×), boot heap 102→61 KB | — |

The four overhead classes, with the evidence that found them:

1. **Calypsi cross-call factoring** (inc1): 1,187 `jsl`→helper call
   sites in `lua_lvm.lst` alone at `-O2 --speed`, ~5 on the dispatch
   path of every op, 14 cycles of pure call overhead each.
   `--no-cross-call` existed all along; nothing ever tried it. Cost:
   ROM back to 512 KB (code expands past the 256 KB pad boundary).
2. **Looped constant shifts in operand decode** (inc2): Calypsi lowers
   32-bit `>>14` as a counted `lsr/ror` loop (~150 cycles); the shell
   also computed `ra = RA(i)` (looped `>>6` + a ×6) for every op even
   though the T6 split ops recompute `vRA` themselves, burned a far
   RMW on the vmops mailbox counter in *release* ROMs, and tested the
   hook mask per op. `P8_BYTEDECODE` decodes fields from bytes — the
   host suite runs with it forced on, pinning the byte decoder against
   portable-compiled carts before Mesen is ever involved.
3. **Far-call round-trips on the table path** (inc3): `luaH_get` paid
   `p8fix_toint` + `p8fix_fromint` per number key just to test
   integer-ness (`luai_numisint` is a 16-bit compare); EQ/LT/LE far-
   called into `luaV_*` for the dominant number-number case; and the
   whole lookup half of ltable sat at -O0 because miscompile #4 pinned
   the TU — the split moves `luaH_get/getint/getstr/getn` into
   `ltable_get.c`, which joins P8HOT.
4. **API-layer iteration ceremony** (inc4): every `all()` step paid a
   `rawlen` binary search + `rawgeti`/`rawequal` API ceremony +
   toint/fromint far calls. The T0 profile called this out; T7
   collected it.

The same pass found two real semantics bugs, both now pinned against
the real console by new realvec vectors: `str[pos]` indexed with the
raw fix32 bits (every normal index returned nil; and the oracle ruled
the edges: out-of-range is NIL — the console's `str[pos]` is *not*
`sub(str,pos,pos)` at the edges), and `foreach` skipped the successor
of an element deleted mid-callback (the console's foreach is
`all()`-based and revisits: `fe1 123`, not `13`).

Where this leaves the ceiling: the gameplay-shaped probe runs +72%
over T6 (1,012 → 1,737 ops/s) and all()-heavy stretches +73% on top of
their old rate; release ROMs shed the vmops counter for a further ~2%
nobody benches. The memory-bound floor is still the floor — SA-1 (M11)
remains the next VM multiplier — and every T7 gain carries over to it
unchanged. The lesson T7 adds to the M9 rules: a null A/B only rules
out what actually varied between its two sides. Read the listings.
