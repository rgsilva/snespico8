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

**Pending one-line fix in p8.c (apply after the M2 agent lands, it owns
the file):** in `rnd_seed`, replace the zero special-case with
`if (seed == 0) seed = 0xdeadbeefu;` and fall through to the general
path (constants + 32 warmups). Optionally ALSO switch `rnd_step` to the
real formulation above so the RAM state at 0x5f44 is bit-identical to
real PICO-8 (needed for carts that peek/poke the rnd state; output
streams are unchanged either way).

## Captured goldens (scratchpad realvec.p8 / rndprobe*.p8, rerunnable)

    srand(42):  state lo=0xb5946865 hi=0x7c9336ed; draws 0xec81 0x0866
    srand(0):   state lo=0xd67ce1e8 hi=0x42cfadf8; draw 0x8474 (then 0x5724 predicted)
    srand(1):   state lo=0x06cd517c hi=0x88fb0dea
    srand(0x1234.5678): state lo=0x41a8bda7 hi=0x980fd138
    boot state varies per launch (entropy) — keep ours fixed for determinism.

## TODO

- realvec.sh: permanent vector suite runp8-vs-real-PICO-8 (like
  numvec.sh vs z8lua), including screen-hash carts for the gfx API.
- Re-verify rnd state layout at 0x5f44 after switching rnd_step to the
  real formulation.
