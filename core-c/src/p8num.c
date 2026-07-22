/* fix32 implementation. See p8num.h for the ground rules (no int64,
** unsigned wrap). Semantics pinned against the z8lua oracle:
**   5.5%2=1.5  -5.5%2=0.5  5.5%-2=1.5      (mod lands in [0,|b|))
**   1/0=0x7fff.ffff  -1/0=0x8000.0000  0/0=0x7fff.ffff
**   0x7fff.ffff+0x0.0001=-32768           (wrap)
**   print rounds to 4 decimals, trims zeros, keeps "-0"
*/
#include <stdint.h>
#include <stddef.h>
#include "p8num.h"

/* M9 T1: macros, not functions -- at -O0 Calypsi compiled these identity
** casts as far calls, three per p8fix_add */
#define U(v) ((uint32_t)(v))
#define S(v) ((long)(int32_t)(v))

long p8fix_add(long a, long b) { return S(U(a) + U(b)); }
long p8fix_sub(long a, long b) { return S(U(a) - U(b)); }
long p8fix_unm(long a) { return S(0u - U(a)); }
long p8fix_floor(long a) { return S(U(a) & 0xFFFF0000u); }
long p8fix_fromint(long i) { return S((uint32_t)i << 16); }

long p8fix_toint(long a)
{
  /* floor()>>16: the high word read as SIGNED 16-bit is the floor for
     all 2^32 inputs (fixfuzz proves it exhaustively). int16_t, not int:
     host int is 32-bit and would not sign-extend. M9 T1 -- the old
     (a - frac) / 65536L form cost a _Div32 call per integer argument,
     and -O2 does not strength-reduce it. */
  return (long)(int16_t)(U(a) >> 16);
}

/* M9 T5: the four 16x16 partials go through MUL16. SNES builds define
** P8_HWMATH (snes/Makefile) and route to the CPU hardware multiplier
** (snes/hwmath.c); everywhere else MUL16 is the native multiply, i.e.
** exactly the pre-T5 expression. Bit-identity of the hardware path is
** proven on target (make test-abmath) and by snesparity (SNES hardware
** vs host C, same vectors). */
#ifdef P8_HWMATH
extern unsigned long p8hw_mul16(unsigned int a, unsigned int b);
extern unsigned long p8hw_udiv32_8(unsigned long ua, unsigned char k);
#define MUL16(x, y) ((uint32_t)p8hw_mul16((unsigned int)(x), (unsigned int)(y)))
#else
#define MUL16(x, y) ((uint32_t)(x) * (uint32_t)(y))
#endif

long p8fix_mul(long a, long b)
{
  uint32_t ua = U(a), ub = U(b);
  uint32_t ah = ua >> 16, al = ua & 0xFFFFu;
  uint32_t bh = ub >> 16, bl = ub & 0xFFFFu;
  uint32_t r = MUL16(ah, bh) << 16;
  r += MUL16(ah, bl) + MUL16(al, bh);
  r += MUL16(al, bl) >> 16;
  if (a < 0)
    r -= ub << 16;
  if (b < 0)
    r -= ua << 16;
  return S(r);
}

#ifdef P8_HWMATH
/* pure-C references for the on-target A/B ROM (-DP8_ABMATH): the exact
** pre-T5 compositions (native multiplies -> Calypsi _Mul32 helpers, and
** the bit-serial udiv48 with no pow2/hardware fast paths). Unreferenced
** in normal ROMs; the linker strips them. */
long p8fix_mul_ref(long a, long b)
{
  uint32_t ua = U(a), ub = U(b);
  uint32_t ah = ua >> 16, al = ua & 0xFFFFu;
  uint32_t bh = ub >> 16, bl = ub & 0xFFFFu;
  uint32_t r = (ah * bh) << 16;
  r += ah * bl + al * bh;
  r += (al * bl) >> 16;
  if (a < 0)
    r -= ub << 16;
  if (b < 0)
    r -= ua << 16;
  return S(r);
}
#endif

/* low 32 bits of the 48-bit quotient (hi:lo)/d; d != 0.
** Callers rely on the wrap for PICO-8 overflow semantics. */
static uint32_t udiv48(uint32_t hi, uint32_t lo, uint32_t d)
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

long p8fix_div(long a, long b)
{
  uint32_t ua, ub, q;
  int neg;
  if (b == 0)
    return (a < 0) ? S(0x80000000u) : S(0x7FFFFFFFu);
  neg = (a < 0) != (b < 0);
  ua = (a < 0) ? 0u - U(a) : U(a);
  ub = (b < 0) ? 0u - U(b) : U(b);
  if ((ub & (ub - 1u)) == 0) {
    /* M9 T1: pow2 divisor. The low 32 bits of the 48-bit quotient
       (ua<<16)/2^k are a pure shift; uint32 shifts drop the same high
       bits udiv48's wrap drops, so this is exact (fixfuzz: all 32 k,
       both signs, vs the bit-serial path). x/1, x/2, x/0x8000 etc. */
    int k = 0;
    uint32_t t = ub;
    while (t >>= 1)
      k++;
    q = (k >= 16) ? (ua >> (k - 16)) : (ua << (16 - k));
#ifdef P8_HWMATH
  } else if ((ub & 0xFFFFu) == 0u && (ub >> 16) < 256u) {
    /* M9 T5: small integral divisor k (3,5,10,...,255): the 48-bit
       quotient (ua<<16)/(k<<16) is exactly ua/k, a 32/8 divide on the
       CPU hardware divider. Exact, no wrap involved (q <= ua). */
    q = (uint32_t)p8hw_udiv32_8((unsigned long)ua, (unsigned char)(ub >> 16));
#endif
  } else {
    q = udiv48(ua >> 16, ua << 16, ub);
  }
  return S(neg ? 0u - q : q);
}

#ifdef P8_HWMATH
/* A/B reference: bit-serial division only -- no pow2 shift path, no
** hardware divider -- so the A/B ROM re-proves BOTH fast paths on
** target against the original algorithm. */
long p8fix_div_ref(long a, long b)
{
  uint32_t ua, ub, q;
  int neg;
  if (b == 0)
    return (a < 0) ? S(0x80000000u) : S(0x7FFFFFFFu);
  neg = (a < 0) != (b < 0);
  ua = (a < 0) ? 0u - U(a) : U(a);
  ub = (b < 0) ? 0u - U(b) : U(b);
  q = udiv48(ua >> 16, ua << 16, ub);
  return S(neg ? 0u - q : q);
}
#endif

long p8fix_mod(long a, long b)
{
  uint32_t ub, m;
  if (b == 0)
    return 0; /* oracle: x%0 == 0 for all x */
  ub = (b < 0) ? 0u - U(b) : U(b);
  if (a >= 0) {
    m = U(a) % ub;
  } else {
    m = (0u - U(a)) % ub;
    if (m)
      m = ub - m;
  }
  return S(m);
}

long p8fix_sqrt(long v)
{
  /* bit-pair integer sqrt of the 48-bit radicand v<<16 */
  uint32_t hi, lo, rem = 0, root = 0;
  int i;
  if (v <= 0)
    return 0;
  hi = U(v) >> 16;
  lo = U(v) << 16;
  /* 32 bit-pairs sweep the whole hi:lo; the radicand's 48 bits sit in
     the low half, so the root comes out with its full 24 significant
     bits (24 iterations would truncate it by 2^8) */
  for (i = 0; i < 32; i++) {
    uint32_t two;
    rem = (rem << 2) | (hi >> 30);
    hi = (hi << 2) | (lo >> 30);
    lo <<= 2;
    root <<= 1;
    two = (root << 1) | 1u;
    if (rem >= two) {
      rem -= two;
      root |= 1u;
    }
  }
  return S(root);
}

long p8fix_pow(long a, long b)
{
  uint32_t fb = U(b) & 0xFFFFu;
  long e = p8fix_toint(b); /* floor: b = e + frac, frac in [0,1) */
  long r = p8fix_fromint(1);
  long base = a;
  int negexp = 0;
  if (e < 0) {
    negexp = 1;
    e = -e;
  }
  while (e) {
    if (e & 1)
      r = p8fix_mul(r, base);
    base = p8fix_mul(base, base);
    e >>= 1;
  }
  /* b = floor(b) + frac: the reciprocal belongs to the integer part
     only (2^-0.5 = 2^-1 * 2^0.5) */
  if (negexp) {
    r = p8fix_div(p8fix_fromint(1), r);
    negexp = 0;
  }
  if (fb) {
    if (a < 0)
      return 0; /* oracle: (-2)^0.5 == 0 */
    {
      long fr = p8fix_fromint(1);
      long s = a;
      int i;
      for (i = 15; i >= 0; i--) {
        s = p8fix_sqrt(s); /* a^(2^-(16-i)) chain: first is a^0.5 */
        if ((fb >> i) & 1)
          fr = p8fix_mul(fr, s);
      }
      r = p8fix_mul(r, fr);
    }
  }
  (void)negexp;
  return r;
}

/* ---- string conversions ---- */

int p8_fix2str(char *buf, long v)
{
  char *p = buf;
  uint32_t av = U(v);
  uint32_t ip, fr, frac4;
  char tmp[8];
  int n;
  if (v < 0) {
    *p++ = '-';
    av = 0u - av;
  }
  ip = av >> 16;
  fr = av & 0xFFFFu;
  frac4 = (fr * 10000u + 32768u) >> 16; /* round half up to 4 decimals */
  if (frac4 >= 10000u) {
    frac4 = 0;
    ip++;
  }
  n = 0;
  do {
    tmp[n++] = (char)('0' + ip % 10u);
    ip /= 10u;
  } while (ip);
  while (n)
    *p++ = tmp[--n];
  if (frac4) {
    int digits[4], k;
    digits[0] = (int)(frac4 / 1000u % 10u);
    digits[1] = (int)(frac4 / 100u % 10u);
    digits[2] = (int)(frac4 / 10u % 10u);
    digits[3] = (int)(frac4 % 10u);
    k = 4;
    while (k > 0 && digits[k - 1] == 0)
      k--;
    *p++ = '.';
    for (n = 0; n < k; n++)
      *p++ = (char)('0' + digits[n]);
  }
  *p = 0;
  return (int)(p - buf);
}

static int hexv(int c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

long p8_str2fix(const char *s, char **endptr)
{
  const char *p = s;
  uint32_t ip = 0, frac = 0;
  int any = 0, neg = 0;
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
    p++;
  if (*p == '-') {
    neg = 1;
    p++;
  } else if (*p == '+') {
    p++;
  }
  if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
    int k = 0;
    p += 2;
    while (hexv(*p) >= 0) {
      ip = (ip << 4) | (uint32_t)hexv(*p);
      p++;
      any = 1;
    }
    if (*p == '.') {
      p++;
      while (hexv(*p) >= 0) {
        if (k < 4)
          frac |= (uint32_t)hexv(*p) << (12 - 4 * k);
        k++;
        p++;
        any = 1;
      }
    }
  } else if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B')) {
    int k = 0;
    p += 2;
    while (*p == '0' || *p == '1') {
      ip = (ip << 1) | (uint32_t)(*p - '0');
      p++;
      any = 1;
    }
    if (*p == '.') {
      p++;
      while (*p == '0' || *p == '1') {
        if (k < 16)
          frac |= (uint32_t)(*p - '0') << (15 - k);
        k++;
        p++;
        any = 1;
      }
    }
  } else {
    uint32_t f = 0, scale = 1;
    while (*p >= '0' && *p <= '9') {
      ip = ip * 10u + (uint32_t)(*p - '0');
      p++;
      any = 1;
    }
    if (*p == '.') {
      p++;
      while (*p >= '0' && *p <= '9') {
        if (scale < 1000000u) { /* real PICO-8 reads at most 6 digits */
          f = f * 10u + (uint32_t)(*p - '0');
          scale *= 10u;
        }
        p++;
        any = 1;
      }
    }
    if (scale > 1) {
      /* frac = trunc(f * 65536 / scale): real PICO-8 TRUNCATES decimal
         fractions (0.0001 -> raw 6, 0.1 -> 0x1999, 0.33 -> 0x547a,
         6-digit cap pinned by tostr(x,1) raw dumps on v0.2.6b; z8lua
         rounds and diverges from the real console here) */
      frac = udiv48(f >> 16, f << 16, scale);
    }
  }
  if (!any) {
    if (endptr)
      *endptr = (char *)s;
    return 0;
  }
  if (endptr)
    *endptr = (char *)p;
  {
    uint32_t v = (ip << 16) + frac;
    if (neg)
      v = 0u - v;
    return S(v);
  }
}

/* ---- PICO-8 dialect operators (M1c) ----
** Bit ops work on the raw 16.16 bits; shift counts are floored ints,
** negative counts reverse direction (z8lua fix32.h semantics). */

long p8fix_idiv(long a, long b) { return p8fix_floor(p8fix_div(a, b)); }
long p8fix_band(long a, long b) { return S(U(a) & U(b)); }
long p8fix_bor(long a, long b) { return S(U(a) | U(b)); }
long p8fix_bxor(long a, long b) { return S(U(a) ^ U(b)); }
long p8fix_bnot(long a) { return S(~U(a)); }

long p8fix_lshr(long a, long b);

/* Shift counts, z8lua-exact (M9 fixfuzz/numvec findings): the count is
** the FLOORED int of b, negated as an int for negative counts -- z8lua
** casts b to int once (arithmetic >>16 = floor) and does `y < 0 ?
** other_dir(x, -y)`. The old code here instead recomputed the count as
** toint(unm(b)), which (a) trunc-rounds fractional negatives (shl by
** -1.5 gave >>1, oracle wants >>2) and (b) never terminated on
** b = 0x8000.0000, whose fix32 negation is itself (stack overflow on
** hardware). Non-recursive form: n <= -32 shifts everything out. */
long p8fix_shl(long a, long b)
{
  long n = p8fix_toint(b);
  if (n < 0)
    return (n <= -32) ? 0 : S(U(a) >> -n);   /* z8: lshr(x, -y) */
  return (n >= 32) ? 0 : S(U(a) << n);
}

long p8fix_shr(long a, long b)
{
  long n = p8fix_toint(b);
  if (n < 0)
    return (n <= -32) ? 0 : S(U(a) << -n);   /* z8: x << -y */
  if (n > 31)
    n = 31;
  /* arithmetic shift without impl-defined behavior */
  return (a < 0) ? S(~(~U(a) >> n)) : S(U(a) >> n);
}

long p8fix_lshr(long a, long b)
{
  long n = p8fix_toint(b);
  if (n < 0)
    return (n <= -32) ? 0 : S(U(a) << -n);   /* z8: x << -y */
  return (n >= 32) ? 0 : S(U(a) >> n);
}

long p8fix_rotl(long a, long b)
{
  long n = p8fix_toint(b) & 31;
  return n ? S((U(a) << n) | (U(a) >> (32 - n))) : a;
}

long p8fix_rotr(long a, long b)
{
  long n = p8fix_toint(b) & 31;
  return n ? S((U(a) >> n) | (U(a) << (32 - n))) : a;
}

long p8fix_ceil(long a) { return p8fix_unm(p8fix_floor(p8fix_unm(a))); }
