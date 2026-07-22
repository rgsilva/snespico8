/* M9 T1 differential fuzzer: every p8num rewrite old-vs-new, host-side.
**
** The "ref_" functions are frozen copies of the pre-M9 implementations
** (the M0-M8 code that passed 149 oracle vectors and M4 bit-parity) --
** the candidate is whatever p8num.c now provides, plus replicas of the
** luaconf.h direct-macro forms (keep REPLICA_* textually in sync with
** luaconf.h). Coverage:
**   toint            exhaustive, all 2^32 inputs
**   add/sub/unm      edge cross + random, function vs macro replica
**   band/bor/bxor/bnot  same
**   div              all pow2 divisors (+/-, +/-1 raw neighbors) x
**                    (edges + random dividends), plus general random pairs
**   shl/shr/lshr     edge cross + random (they ride on the toint rewrite)
** Deterministic (fixed xorshift seed). Exits 1 on first mismatch.
*/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "../src/p8num.h"

/* Hard wall-clock budget baked into the binary: no invocation path
** (backgrounded, QoS-throttled, forgotten) can run this forever. The
** full sweep needs ~10-20 s at normal speed, ~2 min at Background QoS;
** blowing the budget is treated as a FAILURE (exit 2), never a pass. */
#ifndef FIXFUZZ_MAX_SECS
#define FIXFUZZ_MAX_SECS 300
#endif
static time_t fz_deadline;
static void deadline_check(void)
{
  if (time(NULL) > fz_deadline) {
    fprintf(stderr, "fixfuzz: TIMEOUT -- exceeded %d s wall budget "
                    "(QoS-throttled?); treat as FAILURE\n",
            (int)FIXFUZZ_MAX_SECS);
    exit(2);
  }
}

#define RU(v) ((uint32_t)(v))
#define RS(v) ((long)(int32_t)(v))

/* ---- frozen pre-M9 references ---- */
static long ref_toint(long a)
{
  return (a - (long)(RU(a) & 0xFFFFu)) / 65536L;
}

static uint32_t ref_udiv48(uint32_t hi, uint32_t lo, uint32_t d)
{
  uint32_t rem = 0, q = 0;
  int i;
  for (i = 47; i >= 0; i--) {
    uint32_t bit = (i >= 32) ? ((hi >> (i - 32)) & 1u) : ((lo >> i) & 1u);
    uint32_t carry = rem & 0x80000000u;
    rem = (rem << 1) | bit;
    q <<= 1;
    if (carry || rem >= d) {
      rem -= d;
      q |= 1u;
    }
  }
  return q;
}

static long ref_div(long a, long b)
{
  uint32_t ua, ub, q;
  int neg;
  if (b == 0)
    return (a < 0) ? RS(0x80000000u) : RS(0x7FFFFFFFu);
  neg = (a < 0) != (b < 0);
  ua = (a < 0) ? 0u - RU(a) : RU(a);
  ub = (b < 0) ? 0u - RU(b) : RU(b);
  q = ref_udiv48(ua >> 16, ua << 16, ub);
  return RS(neg ? 0u - q : q);
}

static long ref_add(long a, long b) { return RS(RU(a) + RU(b)); }
static long ref_sub(long a, long b) { return RS(RU(a) - RU(b)); }
static long ref_unm(long a) { return RS(0u - RU(a)); }
static long ref_band(long a, long b) { return RS(RU(a) & RU(b)); }
static long ref_bor(long a, long b) { return RS(RU(a) | RU(b)); }
static long ref_bxor(long a, long b) { return RS(RU(a) ^ RU(b)); }
static long ref_bnot(long a) { return RS(~RU(a)); }

/* Shift refs are pinned SPEC (z8lua semantics: floored int count,
** negated as an int, n <= -32 shifts out), NOT copies of the pre-M9
** code -- that code trunc-rounded fractional negative counts (diverging
** from the oracle, caught by numvec `16 << -1.5`) and hung forever on
** b = 0x8000.0000 (caught by this fuzzer). The real spec weight for
** shifts is the live z8lua diff in numvec; these refs guard regressions. */
static long ref_shl(long a, long b)
{
  long n = ref_toint(b);
  if (n < 0)
    return (n <= -32) ? 0 : RS(RU(a) >> -n);
  return (n >= 32) ? 0 : RS(RU(a) << n);
}
static long ref_shr(long a, long b)
{
  long n = ref_toint(b);
  if (n < 0)
    return (n <= -32) ? 0 : RS(RU(a) << -n);
  if (n > 31)
    n = 31;
  return (a < 0) ? RS(~(~RU(a) >> n)) : RS(RU(a) >> n);
}
static long ref_lshr(long a, long b)
{
  long n = ref_toint(b);
  if (n < 0)
    return (n <= -32) ? 0 : RS(RU(a) << -n);
  return (n >= 32) ? 0 : RS(RU(a) >> n);
}

/* ---- replicas of the luaconf.h direct-macro forms (keep in sync) ----
** MUST use fixed-32-bit types: on a 64-bit host, plain (unsigned long)
** arithmetic does not wrap at 32 bits and silently diverges from the
** SNES (this fuzzer caught exactly that in its own first draft). The
** luaconf.h macros use LUA_UNSIGNED/LUA_INT32, which are these. */
#define REPLICA_ADD(a, b) ((long)(int32_t)((uint32_t)(a) + (uint32_t)(b)))
#define REPLICA_SUB(a, b) ((long)(int32_t)((uint32_t)(a) - (uint32_t)(b)))
#define REPLICA_UNM(a) ((long)(int32_t)(0u - (uint32_t)(a)))
#define REPLICA_BAND(a, b) ((long)(int32_t)((uint32_t)(a) & (uint32_t)(b)))
#define REPLICA_BOR(a, b) ((long)(int32_t)((uint32_t)(a) | (uint32_t)(b)))
#define REPLICA_BXOR(a, b) ((long)(int32_t)((uint32_t)(a) ^ (uint32_t)(b)))
#define REPLICA_BNOT(a) ((long)(int32_t)(~(uint32_t)(a)))

/* ---- driver ---- */
static uint32_t rng_state = 0x9E3779B9u;
static uint32_t rng(void)
{
  uint32_t x = rng_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return rng_state = x;
}

static long edges[512];
static int nedges;

static void build_edges(void)
{
  int k;
  long base[] = {0, 1, -1, 2, -2, 3, -3, 0x7FFF, 0x8000, 0xFFFF, 0x10000,
                 0x10001, 0x1FFFF, RS(0x7FFF0000u), RS(0x7FFFFFFFu),
                 RS(0x80000000u), RS(0x80000001u), RS(0xFFFF0000u),
                 RS(0xFFFF8000u)};
  size_t i;
  for (i = 0; i < sizeof base / sizeof base[0]; i++)
    edges[nedges++] = base[i];
  for (k = 0; k < 32; k++) {
    edges[nedges++] = RS(1u << k);
    edges[nedges++] = RS(0u - (1u << k));
    edges[nedges++] = RS((1u << k) + 1u);
    edges[nedges++] = RS((1u << k) - 1u);
  }
}

static unsigned long fails;
static void mism(const char *op, long a, long b, long got, long want)
{
  fprintf(stderr, "MISMATCH %s a=%08lx b=%08lx got=%08lx want=%08lx\n",
          op, (unsigned long)RU(a), (unsigned long)RU(b),
          (unsigned long)RU(got), (unsigned long)RU(want));
  if (++fails > 10) {
    fprintf(stderr, "fixfuzz: too many mismatches, aborting\n");
    exit(1);
  }
}

/* capture args ONCE: an argument like RS(rng()) must not be re-drawn
   per use (candidate, ref and the error dump saw three different values
   before this -- a classic macro double-eval, caught in anger) */
#define CHK1(op, fn, ref, a) \
  do { long a_ = (a); long g = fn(a_), w = ref(a_); \
       if (g != w) mism(op, a_, 0, g, w); } while (0)
#define CHK2(op, fn, ref, a, b) \
  do { long a_ = (a), b_ = (b); long g = fn(a_, b_), w = ref(a_, b_); \
       if (g != w) mism(op, a_, b_, g, w); } while (0)

int main(void)
{
  int i, j, k;
  unsigned long n;
  fz_deadline = time(NULL) + FIXFUZZ_MAX_SECS;
  /* SIGALRM backstop: deadline_check() can't fire from inside a
     non-terminating candidate (a tail-call-optimized infinite recursion
     spins with no checkpoint -- exactly what the shl-by--32768 bug did).
     The default SIGALRM action kills the process no matter where it is. */
  alarm(FIXFUZZ_MAX_SECS + 30);
  build_edges();

  /* toint: exhaustive */
  fprintf(stderr, "fixfuzz: toint exhaustive 2^32...\n");
  {
    uint32_t x = 0;
    do {
      long a = RS(x);
      long g = p8fix_toint(a), w = ref_toint(a);
      if (g != w) {
        mism("toint", a, 0, g, w);
        if (fails)
          break;
      }
      if ((x & 0xFFFFFFu) == 0)
        deadline_check();   /* every 2^24 iterations */
      x++;
    } while (x != 0);
  }

  fprintf(stderr, "fixfuzz: 2-arg ops, edge cross + 2M random...\n");
  for (i = 0; i < nedges; i++) {
    deadline_check();
    for (j = 0; j < nedges; j++) {
      long a = edges[i], b = edges[j];
      CHK2("add", p8fix_add, ref_add, a, b);
      CHK2("add-macro", REPLICA_ADD, ref_add, a, b);
      CHK2("sub", p8fix_sub, ref_sub, a, b);
      CHK2("sub-macro", REPLICA_SUB, ref_sub, a, b);
      CHK2("band", p8fix_band, ref_band, a, b);
      CHK2("band-macro", REPLICA_BAND, ref_band, a, b);
      CHK2("bor", p8fix_bor, ref_bor, a, b);
      CHK2("bor-macro", REPLICA_BOR, ref_bor, a, b);
      CHK2("bxor", p8fix_bxor, ref_bxor, a, b);
      CHK2("bxor-macro", REPLICA_BXOR, ref_bxor, a, b);
      CHK2("div", p8fix_div, ref_div, a, b);
      CHK2("shl", p8fix_shl, ref_shl, a, b);
      CHK2("shr", p8fix_shr, ref_shr, a, b);
      CHK2("lshr", p8fix_lshr, ref_lshr, a, b);
    }
    CHK1("unm", p8fix_unm, ref_unm, edges[i]);
    CHK1("unm-macro", REPLICA_UNM, ref_unm, edges[i]);
    CHK1("bnot", p8fix_bnot, ref_bnot, edges[i]);
    CHK1("bnot-macro", REPLICA_BNOT, ref_bnot, edges[i]);
  }
  /* volumes sized so the whole run stays ~10 CPU-seconds: macOS may run
     this at Background QoS (~13% of a core) and it must still finish in
     a couple of minutes. The edge cross above is the semantic net; the
     randoms only guard against "surprising" value interactions. */
  for (n = 0; n < 200000ul; n++) {
    long a = RS(rng()), b = RS(rng());
    if ((n & 0xFFFu) == 0)
      deadline_check();
    CHK2("add", p8fix_add, ref_add, a, b);
    CHK2("add-macro", REPLICA_ADD, ref_add, a, b);
    CHK2("sub", p8fix_sub, ref_sub, a, b);
    CHK2("sub-macro", REPLICA_SUB, ref_sub, a, b);
    CHK2("band-macro", REPLICA_BAND, ref_band, a, b);
    CHK2("bor-macro", REPLICA_BOR, ref_bor, a, b);
    CHK2("bxor-macro", REPLICA_BXOR, ref_bxor, a, b);
    CHK1("unm-macro", REPLICA_UNM, ref_unm, a);
    CHK1("bnot-macro", REPLICA_BNOT, ref_bnot, a);
    CHK2("shl", p8fix_shl, ref_shl, a, b);
    CHK2("shr", p8fix_shr, ref_shr, a, b);
    CHK2("lshr", p8fix_lshr, ref_lshr, a, b);
  }

  /* div: the pow2 fast path, exhaustively across divisor structure */
  fprintf(stderr, "fixfuzz: div pow2 divisors x (edges + 200k random)...\n");
  for (k = 0; k < 32; k++) {
    long p2 = RS(1u << k);
    long divs[6];
    deadline_check();
    divs[0] = p2;
    divs[1] = ref_unm(p2);
    divs[2] = RS((1u << k) + 1u);   /* near-miss: must take the slow path */
    divs[3] = RS((1u << k) - 1u);
    divs[4] = RS(0u - ((1u << k) + 1u));
    divs[5] = RS(0u - ((1u << k) - 1u));
    for (j = 0; j < 6; j++) {
      long b = divs[j];
      for (i = 0; i < nedges; i++)
        CHK2("div-pow2", p8fix_div, ref_div, edges[i], b);
      for (n = 0; n < 20000ul; n++)
        CHK2("div-pow2", p8fix_div, ref_div, RS(rng()), b);
    }
  }
  for (n = 0; n < 100000ul; n++) {
    long a = RS(rng()), b = RS(rng());
    if ((n & 0xFFFu) == 0)
      deadline_check();
    CHK2("div", p8fix_div, ref_div, a, b);
  }

  if (fails) {
    fprintf(stderr, "fixfuzz: FAIL (%lu mismatches)\n", fails);
    return 1;
  }
  fprintf(stderr, "fixfuzz: PASS\n");
  return 0;
}
