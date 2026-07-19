# Real PICO-8 as an oracle

Ricardo has PICO-8 installed: `/Applications/PICO-8.app/Contents/MacOS/pico8`.

**Headless execution works**: `pico8 -x cart.p8` runs the cart without a
window, `printh` goes to stdout, and the process exits at the end of the
program. This is the second oracle (z8lua being the first) — it can
adjudicate everything z8lua can't: rnd/srand, draw-state quirks, and any
VERIFY-tagged behavior. Pattern for framebuffer goldens: have the cart
draw, then hash 0x6000-0x7fff in Lua via @peek and printh the hash —
runs identically in real PICO-8 and runp8, no screenshot compare needed.

## Findings (verified 2026-07-19, PICO-8 v0.2.6b)

All previously VERIFY-tagged semantics confirmed correct in our core:
`x%0 == 0`, `x\0 == 0x7fff.ffff` (sign-following), `(-2)^0.5 == 0`,
`2^-0.5 == 0.7071`, div-by-zero saturation, overflow wrap, `-0`
formatting, `tonum("ff",1) == 255`, `sgn(0) == 1`, `atan2(0,0) == 0.25`,
`atan2(1,1) == 0.875`.

## PRNG — exact algorithm (empirically pinned)

State: two u32 little-endian words in RAM: `lo` at 0x5f44, `hi` at 0x5f48.

**Step** (verified via poke4-then-rnd): the BBS tid=51113 description has
the roles swapped. Real transition, from state (lo=0x11112222,
hi=0x33334444) -> rnd() -> (lo=0x55555555, hi=0x88889999), result 0x5555:

    lo += rotl32(hi, 16);
    hi += lo;
    result = lo % range_raw   (range_raw = 0x10000 for rnd())

Our implementation uses the BBS formulation (hi = rotl(hi,16)+lo;
lo += hi; result from hi) which is **output-equivalent for
srand-seeded streams** (verified: srand(42) -> 0xec81, 0x0866 matches
real byte-for-byte) but holds the state words swapped/differently, so
**@0x5f44 reads and poke4-seeded states diverge from real PICO-8**.

**srand(seed)** (seed = raw 16.16 bits of the argument):

    if (seed == 0) seed = 0xdeadbeef;
    lo = seed; hi = seed ^ 0xbead29ba;
    32 warmup steps;

(0xdeadbeef ^ 0xbead29ba = 0x60009755, which is how the BBS constants
arise.) Verified against real states/draws for seeds 42, 0, and raw
1/2/3 draws pending.

**Resolution (applied post-M2 in p8.c):** srand's zero-case fix is in
(seed 0 -> 0xdeadbeef, general path, 32 warmups) and rnd_step keeps the
BBS formulation. Seeded DRAW STREAMS are verified byte-exact against
real PICO-8 (srand(42) -> 0xec81,0x0866; srand(0) -> 0x8474). The pure
"real step" formulation was tried and reverted: it reproduces poked
single-step transitions exactly but NOT the seeded chains — real
PICO-8's seeded state sequence has an additional subtlety (possibly
range-dependent extra stepping) that is not yet pinned. Consequence:
our RAM words at 0x5f44/0x5f48 differ from real PICO-8's even though
draw streams match; only carts that peek/poke the rnd state directly
would notice (VERIFY if one ever does — revisit with more captures).

## Captured goldens (scratchpad realvec.p8 / rndprobe*.p8, rerunnable)

    srand(42):  state lo=0xb5946865 hi=0x7c9336ed; draws 0xec81 0x0866
    srand(0):   state lo=0xd67ce1e8 hi=0x42cfadf8; draw 0x8474 (then 0x5724 predicted)
    srand(1):   state lo=0x06cd517c hi=0x88fb0dea
    srand(0x1234.5678): state lo=0x41a8bda7 hi=0x980fd138
    boot state varies per launch (entropy) — keep ours fixed for determinism.

## realvec.sh (DONE)

`tests/realvec.sh` runs `tests/realvec.lua` under both engines and
diffs; gfx stages hash their own framebuffer via @ peeks (the hash IS
the comparison — no screenshots). First run found and fixed: three
wrong font glyphs (puny-e 69, puny-l 76, ampersand 38 — OVERRIDES in
tools/mkfont.py), a print() stack-index bug (luaL_tolstring's pushed
copy read as the color arg -> cursor-mode text drawn in color 0), and
**decimal literal parsing: real PICO-8 TRUNCATES with a 6-digit cap**
(0.1 -> 0x1999, 0.33 -> 0x547a, 0.12345678 -> 0x1f9a; z8lua rounds and
diverges from the real console — real wins, p8num.c fixed; all z8lua
suites still pass since displays coincide).

## Open: display formatting boundary rule

tostr's 4-decimal display has contradictory boundary evidence:
raw 0xffff -> "1" (demands rounding: 9999.847 -> 10000 -> carry) but
raw 6 -> "0" (demands truncation: 0.9155 -> 0). Neither pure rule fits
both; our formatter rounds half-up (matches z8lua; raw 6 shows
"0.0001"). Needs a boundary sweep (raws 2..8, 0xfff0..0xffff, halfway
cases) to pin. Display-only, 1-ulp class; realvec avoids the ambiguous
case by comparing raw bits.
