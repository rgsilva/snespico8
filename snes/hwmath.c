/* M9 T5: fix32 partials on the SNES CPU math units.
**
** The CPU multiplier ($4202/$4203 -> $4216) and divider ($4204-$4206 ->
** $4214, remainder $4216) are separate silicon from the Mode 7 matrix
** registers ($211B/$211C) that forced --no-ppu-mul, and this ROM runs
** with NMI/IRQ disabled ($4200 = $01), so nothing interrupts a
** computation. Timing: results are valid 8 (mul) / 16 (div) CPU cycles
** after the trigger write. The interleaved volatile $4212 reads are far
** accesses (>= 6 cycles each on top of the surrounding instruction
** overhead) that guarantee the margin at any optimization level.
** HDMA (the letterbox channel) only ever PAUSES the CPU while the units
** keep counting, which moves completion earlier in the instruction
** stream, never later. All of this is proven empirically, not trusted:
** make test-abmath runs a random + directed corpus of hardware-vs-C
** comparisons on target, and snesparity diffs the SNES (hardware path)
** against the host core (pure C) end to end.
**
** SNES-side only: core-c/src/p8num.c routes here under -DP8_HWMATH,
** which only snes/Makefile defines. */
#include <stdint.h>

#define REG8(a)  (*(volatile uint8_t *)(unsigned long)(a))
#define REG16(a) (*(volatile uint16_t *)(unsigned long)(a))

/* 16x16 -> 32 unsigned multiply: four 8x8 hardware partials.
** $4202 latches across triggers; each $4203 write starts a multiply. */
unsigned long p8hw_mul16(unsigned int a, unsigned int b)
{
  uint8_t al = (uint8_t)a, ah = (uint8_t)(a >> 8);
  uint8_t bl = (uint8_t)b, bh = (uint8_t)(b >> 8);
  unsigned int lo, mid1, mid2, hi;

  /* two dummy reads per multiply, three per divide: the compiler may
     schedule varying amounts of non-volatile work between the trigger
     and the result read, so the margin must hold even when NOTHING gets
     interleaved. The A/B ROM caught exactly that: with one dummy read,
     the last divide step of 0.5/3 came back one hardware cycle early
     (quotient >> 1). Margins are now >= 2x the unit latency. */
  REG8(0x4202) = al;
  REG8(0x4203) = bl;
  (void)REG8(0x4212);
  (void)REG8(0x4212);
  lo = REG16(0x4216);            /* al*bl */
  REG8(0x4203) = bh;
  (void)REG8(0x4212);
  (void)REG8(0x4212);
  mid1 = REG16(0x4216);          /* al*bh */
  REG8(0x4202) = ah;
  REG8(0x4203) = bl;
  (void)REG8(0x4212);
  (void)REG8(0x4212);
  mid2 = REG16(0x4216);          /* ah*bl */
  REG8(0x4203) = bh;
  (void)REG8(0x4212);
  (void)REG8(0x4212);
  hi = REG16(0x4216);            /* ah*bh */

  /* mid1 + mid2 can carry past 16 bits: sum them as 32-bit */
  return (unsigned long)lo + (((unsigned long)mid1 + mid2) << 8) +
         ((unsigned long)hi << 16);
}

/* Diagnostic single hardware divide with an oversized wait; returns
** remainder<<16 | quotient. Used by the A/B ROM to isolate divider
** behavior from the chaining logic. */
unsigned long p8hw_div16_probe(unsigned int d, unsigned char k)
{
  unsigned int q, r;
  REG16(0x4204) = d;
  REG8(0x4206) = k;
  (void)REG8(0x4212); (void)REG8(0x4212); (void)REG8(0x4212);
  (void)REG8(0x4212); (void)REG8(0x4212); (void)REG8(0x4212);
  (void)REG8(0x4212); (void)REG8(0x4212);
  q = REG16(0x4214);
  r = REG16(0x4216);
  return ((unsigned long)r << 16) | q;
}

/* Diagnostic pair probe: two back-to-back divides (0x80/3 then 0x200/3,
** the failing s3/s4 shape) with no chaining arithmetic between. Packs
** q1,r1,q2,r2 as bytes. */
unsigned long p8hw_div_pair_probe(void)
{
  unsigned int q1, r1, q2, r2;
  REG16(0x4204) = 0x0080u;
  REG8(0x4206) = 3;
  (void)REG8(0x4212); (void)REG8(0x4212); (void)REG8(0x4212);
  (void)REG8(0x4212); (void)REG8(0x4212); (void)REG8(0x4212);
  (void)REG8(0x4212); (void)REG8(0x4212);
  q1 = REG16(0x4214);
  r1 = REG16(0x4216);
  REG16(0x4204) = 0x0200u;
  REG8(0x4206) = 3;
  (void)REG8(0x4212); (void)REG8(0x4212); (void)REG8(0x4212);
  (void)REG8(0x4212); (void)REG8(0x4212); (void)REG8(0x4212);
  (void)REG8(0x4212); (void)REG8(0x4212);
  q2 = REG16(0x4214);
  r2 = REG16(0x4216);
  return ((unsigned long)(uint8_t)q1 << 24) | ((unsigned long)(uint8_t)r1 << 16) |
         ((unsigned long)(uint8_t)q2 << 8) | (uint8_t)r2;
}

/* 32 / 8 -> 32 unsigned divide via four chained 16/8 hardware steps.
** Each step divides (remainder << 8 | next byte); remainder < k keeps
** every intermediate dividend under k*256 <= 65280, so quotient bytes
** stay under 256. k must be 1..255 (caller guarantees). Exact -- used
** for fix32 division by small integral divisors, where the 48-bit
** quotient (ua<<16)/(k<<16) collapses to plain ua/k. */
unsigned long p8hw_udiv32_8(unsigned long ua, unsigned char k)
{
  unsigned long q;
  unsigned int step;
  /* byte operands pre-masked into 16-bit locals: Calypsi -O0 compiles
     an inline (uint8_t)(ua >> K) inside the 16-bit `step | byte`
     expression as an UNMASKED 16-bit load at byte offset K/8 -- the
     truncation is dropped and neighboring ua bytes leak into the
     dividend (6th documented miscompile; found by the A/B corpus as
     0.5/3 = 0x2A55, reconstructed instruction-by-instruction from the
     .lst). An explicit & 0xFF on a 16-bit local forces a real AND. */
  unsigned int b3 = (unsigned int)(ua >> 24) & 0xFFu;
  unsigned int b2 = (unsigned int)(ua >> 16) & 0xFFu;
  unsigned int b1 = (unsigned int)(ua >> 8) & 0xFFu;
  unsigned int b0 = (unsigned int)ua & 0xFFu;

  /* Eight margin reads (~48+ cycles), empirically pinned: the isolated
     probe divide is exact with eight, while three (~18+ cycles, already
     past the documented 16) still read a stale/partial quotient on some
     chained steps. Whatever the true settle model is, 3x the documented
     latency covers it, and the A/B corpus re-proves it on every build. */
#define HWDIV_WAIT()                                  \
  do {                                                \
    (void)REG8(0x4212); (void)REG8(0x4212);           \
    (void)REG8(0x4212); (void)REG8(0x4212);           \
    (void)REG8(0x4212); (void)REG8(0x4212);           \
    (void)REG8(0x4212); (void)REG8(0x4212);           \
  } while (0)
#define HWDIV_STEP(byte16)                            \
  do {                                                \
    REG16(0x4204) = step | (byte16);                  \
    REG8(0x4206) = k;                                 \
    HWDIV_WAIT();                                     \
    q = (q << 8) | (uint8_t)REG16(0x4214);            \
    step = (unsigned int)(uint8_t)REG16(0x4216) << 8; \
  } while (0)

  q = 0;
  step = 0;
  HWDIV_STEP(b3);
  HWDIV_STEP(b2);
  HWDIV_STEP(b1);
  HWDIV_STEP(b0);
#undef HWDIV_STEP
#undef HWDIV_WAIT
  return q;
}
