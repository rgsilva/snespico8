# Where snespico8 goes next

M9 is complete (T0–T6; `docs/M9-perf-plan.md` has the full story,
including the closing finding: the VM on stock hardware is memory-bound —
the per-opcode split landed, proved it, and stays as the safer shape).
What remains is polish, hardware, and the SA-1 tier.

## Squeeze the stock machine further (optional polish)

- **Flip ceremony diet.** Of the ~11-frame game frame, ~6–7 are
  `lua_getglobal`/`pcall` ceremony per `_update`/`_draw`. Caching the
  function references once per cart (invalidate on global assignment, or
  simply re-look-up every N frames) could halve the floor for menu-class
  carts. Pure C, parity-gated, unglamorous.
- **Sprite row-blit.** `spr`/`map` still call `p8i_sheet_get` per pixel.
  Walking sheet bytes directly (two pixels per read, flip-aware) is the
  designed-but-unclaimed T3 leftover; profile says it's the next gfx win
  for map-heavy carts.
- **`--force-switch` on the -O0 lexer/parser.** A third of Celeste's ~59 s
  parse is `_ValueSwitch16` dispatch in llex/lparser (pinned -O0 by
  miscompiles #1/#2). Calypsi can be told to lower switches as jump tables
  without raising the opt level. Never measured — cheap experiment.
- **Polish:** vblank frame pacing once anything sustains 30+ fps; splitting
  the 16 KB VRAM DMA across natural vblanks to kill the force-blank
  flicker; `all()`/`foreach` calling `lua_rawlen` (a binary search) per
  iteration — cacheable if the iteration semantics survive the oracle.

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

- **SA-1 tier**: 4x clock, and the Lua heap moves to linear BW-RAM instead
  of 8 KB windows (killing the region allocator's fragmentation tax). This
  is where real-time Celeste lives — ~900K ops/s needed, stock tops out
  around 10–20K even after T6. The core is portable C; the port is a
  linker script and a memory map, not a rewrite.
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
