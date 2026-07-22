# Performance: where the time goes, and where it went

The stock-SNES numbers after M9 and its T7 addendum (all measured in
Mesen2 with the `make bench-*` rig; `docs/M9-perf-plan.md` is the
milestone log with the full per-tier history):

| metric | pre-M9 | now | how |
|--------|--------|-----|-----|
| fps ceiling (empty `_draw`) | 1.37 fps (~44 frames/flip) | **~5.5 fps (~11)** | LUT flip expand |
| draw-bound cart (`bench_draw`) | ~0.23 fps | **~0.7 fps** | draw-context snapshot + span fill |
| API-heavy cart (`demo_video`) | ~0.27 fps | **~0.6 fps** | both of the above |
| VM, gameplay-shaped (`bench_tab`) | (T6: 1,012 ops/s) | **1,737** | T7 overhead removal |
| VM, `all()` iteration (`bench_all`) | (T6-era: 380 ops/s) | **659** | T7 direct-table iterator |
| VM, add-loop | 1,264 ops/s | **2,260** | T1/T2 + T7 |
| VM, mul-loop | ~640 ops/s | **~1,286** | CPU multiplier + T7 |
| Celeste parse (27 KB) | ~59 s | ~2x faster whole-boot (soak) | T7 side effect; parse itself unclaimed |
| ROM size | 512 KB | **512 KB** | was 256 after T2; T7's `--no-cross-call` trades it back for cycles |

Numbers come from the live telemetry rig: the VM instruction counter counts
directly into its fixed mailbox slot ($7E:F00C), so `test/p8bench.lua`
samples slopes mid-`_init` with no flips and no address drift; `make prof`
is a PC-sampling profiler folded through the link map.

## Where the time goes today

- **Game-frame floor (~11 SNES frames)**: ~2–3 frames of LUT expand, ~1 of
  VRAM DMA, and ~6–7 of per-frame Lua/C ceremony — `lua_getglobal` +
  `pcall` for `_update`/`_draw`, the implicit-flip lookup, input sampling.
  The ceremony is the next flip-path lever if anyone needs >6 fps menus.
- **VM ops: memory-bound at bottom — but T7 found a third of the op
  budget hiding above the floor.** T2 showed -O2 barely changes the
  giant switch body; T6 split it into per-op functions and a clean
  lvm.c-only A/B measured just +1.7%. Both nulls were real but blind:
  every configuration ever measured carried the same overhead — Calypsi
  cross-call factoring (14-cycle jsl/rtl per outlined helper, ~5 per
  dispatched op), counted-loop lowering of the constant shifts in
  operand decode, a dead `ra` computation per op, far-call round-trips
  on the table/compare paths, and API-layer iteration ceremony. The T7
  pass (docs/M9-perf-plan.md, T7 section) removed them: gameplay-shaped
  throughput +72%, all()-iteration +73% on top of its old rate. What
  remains under that is the true floor — TValue traffic through 24-bit
  far pointers — and that still belongs to SA-1 (M11). The split stays,
  now also the shape that made per-op fast paths easy to land.
- **Draw-bound carts**: per-pixel cost is now clip-compare + fillp-bit +
  one screen-byte RMW on dp-resident state. The remaining fat is sprite
  pixels re-reading the sheet per pixel (`p8i_sheet_get`) — a row-blit
  that walks sheet bytes directly is designed but unclaimed.
- **Celeste parse (~59 s)**: one-third of parse time is Calypsi runtime
  helpers — `_ValueSwitch16` (the lexer/parser's switch dispatch; llex and
  lparser are pinned -O0 by miscompiles #1/#2) and `_UDivMod32` (ltable's
  `hashnum` mod during constant dedup; ltable is pinned -O0 by miscompile
  #4, and a pow2-mask hash would change `pairs()` order and break z8lua
  parity, so no). Candidate: `--force-switch` on the -O0 lexer — measured
  never, promising, unclaimed.
- **GC: a non-problem.** ~1% of host gameplay time. Deprioritized forever.

## What the ceiling means

Real-time Celeste needs ~900K VM ops/s; stock hardware, fully optimized
(T0–T6 + T7), delivers ~1.7K on gameplay-shaped code — the remaining
gap is the memory system, not the code.
Stock runs simple carts at real (single-digit) frame rates and API-heavy
carts watchably; VM-heavy games are the SA-1 tier (M11): 4x clock, faster
RAM, and a linear 256 KB heap, with the whole T0–T6 core carrying over
unchanged.

## The measurement rig (use it, don't guess)

- `make -C snes bench-vm|draw|flip|api|celeste` — live ops/flip slopes,
  window summaries, plateau/panic detection, hard frame caps. For VM
  work, `bench_tab.p8` is the gameplay-shaped probe (table reads +
  tests + adds in Celeste's opcode-histogram proportions) and
  `bench_all.p8` the all()-iteration probe (T7 inc4's A/B cart);
  `runp8 -S` prints the per-opcode histogram that justifies them.
- `make -C snes prof` — one PC sample per frame via `emu.getState()`,
  symbolized by `tools/profsym.py` against a per-run snapshot of the map
  (Calypsi renumbers `?L` statics every build; never symbolize against a
  different build's map).
- Mesen testrunner needs `--timeout=SECS` for anything over ~2 minutes of
  wall time — the default limit kills slow runs mid-output, which under
  host contention manifests as bogus 0-line/0-flip "failures" that pass
  verbatim on rerun.

## Rules of the road (learned the expensive way)

1. **Measure on target.** The M0-era "~10-frame flip" was an -O2 spike; the
   shipped -O0 loop cost 4x that and nobody knew until the T0 profile.
2. **Every perf change lands behind the bit-exactness net** (host suites,
   fixfuzz, snesparity), and codegen-sensitive changes also pass the
   roulette battery — Calypsi failures are link-layout roulette, so one
   green build proves nothing.
3. **Asm-tier work is A/B-proven on target** (`make test-abmath`), with the
   pure-C references kept in-tree. The A/B corpus caught two compiler bugs
   (volatile deletion at -O2, dropped byte-truncation at -O0) that neither
   the host suites nor parity could have seen first.
4. **Perf claims get a benchmark row** in `docs/M9-perf-plan.md`, before
   and after, or they didn't happen.
