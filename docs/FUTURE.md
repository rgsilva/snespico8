# Where snespico8 goes next

M9 is complete (T0–T6 plus the T7 reevaluation addendum;
`docs/M9-perf-plan.md` has the full story. T6's finding stands — the VM
floor is memory-bound — but T7 proved a third of the op budget sat in
removable overhead *above* that floor: cross-call factoring, looped
shift lowering, far-call round-trips, iteration ceremony. Gameplay-
shaped VM throughput +72%, all() iteration +73% on top). What remains
is polish, hardware, and the SA-1 tier.

## Attack the memory-bound floor anyway (stock-VM ideas that survive T6)

The T6 finding says register allocation can't help the VM — but these
four don't rely on register allocation; they remove memory traffic or
call chains outright. Ranked by expected payoff x confidence:

- **GETTABLE/SETTABLE inline fast path.** The single biggest remaining VM
  target: `GETTABLE` alone is 22% of Celeste gameplay, and today every one
  pays the far-call chain `vmop_gettable → luaV_gettable → luaH_get →
  (hashnum | array index)`. An in-body fast path — table with no
  metatable, integer key in the array part → direct element read — would
  collapse the chain to a few bounds checks for the common case. The T6
  split made this tractable (the op body is a small function now).
  Semantics are the risk: metamethod and boundary cases must stay
  oracle-exact; parity + a directed table-edge vector set gate it.
- **MVN-based TValue copies.** TValue moves (6+ bytes via lda/sta pairs
  through far pointers) are the literal memory-bound floor. The 65816's
  block-move instruction (`MVN`) transfers at 7 cycles/byte with zero
  per-byte instruction fetch — a hand-asm `setobj` for the hottest op
  bodies could shave the "irreducible" traffic by ~30-40%. Needs the
  full A/B-ROM discipline (`test-abmath` pattern) and bank-crossing care.
- **5.3-style local-pc cache.** The one idea from the original T6 list
  never tried: keep `savedpc` in a direct-page local inside the dispatch
  loop, written back at Protect points, instead of two far indirections
  per fetch. Small, measurable, roulette-gated.
- **Per-op asm.** The split's structural dividend: any op body is now a
  small function with a one-pointer ABI — hand-tuning `vmop_gettable` or
  `vmop_test` in asm is a contained, A/B-provable project instead of
  surgery on a 21 KB switch.

## Squeeze the rest of the stock machine (optional polish)

- **Flip ceremony diet.** Of the ~11-frame game frame, ~6–7 are
  `lua_getglobal`/`pcall` ceremony per `_update`/`_draw`. Caching the
  function references once per cart (invalidate on global assignment, or
  simply re-look-up every N frames) could halve the floor for menu-class
  carts — the cheapest big fps win left. Pure C, parity-gated.
- **Sprite row-blit.** `spr`/`map` still call `p8i_sheet_get` per pixel.
  Walking sheet bytes directly (two pixels per read, flip-aware) is the
  designed-but-unclaimed T3 leftover; profile says it's the next gfx win
  for map-heavy carts (and exactly what Celeste's title draw does).
- **`--force-switch` on the -O0 lexer/parser.** A third of source-mode
  parse time is `_ValueSwitch16` dispatch in llex/lparser (pinned -O0
  by miscompiles #1/#2); Calypsi can lower switches as jump tables
  without raising the opt level. Never measured — but T7 inc5's
  bytecode precompile (`P8BYTECODE=1`) already makes on-console parse
  optional, so this only matters for source-mode ROMs now.
- **Presentation polish:** vblank frame pacing once anything sustains
  30+ fps; splitting the 16 KB VRAM DMA across natural vblanks to kill
  the force-blank flicker (pure cosmetics, zero risk).

## Make it provable (debt from the M9 bug hunt)

- **`bugs/` reductions** for the three new Calypsi miscompiles: the ltable
  -O1/-O2 allocation-size bug, the -O2 volatile deletion through
  cast-constant pointers, and the -O0 dropped truncation in
  `(uint8_t)(u32 >> K)`. Each wants a minimal .c that upstream could
  actually look at; the workarounds are already fenced in-tree.
- **CI the fences.** `lstcheck` and `make fuzz` run locally; the host half
  belongs in the GitHub workflow (Calypsi can't go to CI, the fuzzer can).

## Make it real (M10 — hardware)

Everything is built for a stock console + flashcart, but Mesen has been the
only judge so far. The bring-up kit is already in-tree:

- `make test-abmath` re-proves the CPU math units on the real 5A22 — the
  divider probes (`p8hw_div16_probe`/`p8hw_div_pair_probe`) exist exactly
  because real-hardware unit latency may differ from Mesen's model.
- The SRAM probe handles flashcarts that map fewer windows than claimed.
- Open questions only hardware can answer: WRAM refresh interaction with
  the long force-blank DMA, real auto-joypad timing vs the `$4212` waits,
  SPC upload timing on a cold APU.

## Reach further (M11 — SA-1, and beyond)

- **SA-1 tier — where the memory wall actually falls.** The T6 verdict
  makes SA-1 the honest VM path: the floor is bus cycles, and SA-1 runs
  the same 65816 ISA at 10.74 MHz against fast BW-RAM — the multiplier
  applies directly to the memory-bound cost that -O2 and the VM split
  could not touch. Everything carries over: the portable-C core, the T6
  per-op structure, the whole validation rig (parity, roulette, A/B);
  `hwmath.c` swaps the CPU math units for SA-1's own multiplier/divider
  registers. The heap moves to linear 256 KB BW-RAM, retiring the 8 KB
  window allocator and its single-allocation cap. Realistic outcome:
  VM-mid carts playable, Celeste from slideshow into the
  watchable-motion range; "full 30 fps Celeste" stays a stretch goal,
  not a promise.
- **Cart gallery**: a curated menu ROM of freely-licensed BBS carts (each
  author's permission, CC-BY carts) once the fps floor makes them fun.
- **The white whale**: `#include`-free tline + P8SCII, for the handful of
  carts that lean on them.

## The milestone map

| # | milestone | state |
|---|-----------|-------|
| M9 | perf passes | **done** (T0–T6); polish above is optional |
| M10 | real hardware, stock SNES + flashcart | kit ready, not run |
| M11 | SA-1 tier | design notes only |
| M12 | documentation pass | this docs/ set |
