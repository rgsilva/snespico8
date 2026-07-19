// M0a: prove Calypsi C on the SNES, exercising exactly what z8lua needs:
// .data/.bss init, setjmp/longjmp (Lua error handling), far-pointer WRAM
// access, and 16.16 fixed-point mul/div (the fix32 number type).
//
// KNOWN CALYPSI 5.18 BUG (see bugs/calypsi-mul64-hi.c): 64-bit multiply
// returns the high 32 bits right-shifted by one (low 32 bits correct),
// -O0 and -O2, snes and generic clib alike. The battery below documents
// it; fixmul uses 16x16 partial products instead (two's-complement exact,
// including PICO-8 wrap-on-overflow, so the shared PC core matches).
#include <stdint.h>
#include <setjmp.h>
#include "calypsi/mailbox.h"

static jmp_buf jb;
static char inited[] = "data ok\n";
static int32_t bss_probe[4];

// (a*b)>>16 without int64: exact two's-complement bits 16..47 via partials
static int32_t fixmul(int32_t a, int32_t b)
{
  uint32_t ua = (uint32_t)a, ub = (uint32_t)b;
  uint32_t ah = ua >> 16, al = ua & 0xFFFFu;
  uint32_t bh = ub >> 16, bl = ub & 0xFFFFu;
  uint32_t r = (ah * bh) << 16;
  r += ah * bl + al * bh;
  r += (al * bl) >> 16;
  if (a < 0)
    r -= ub << 16;
  if (b < 0)
    r -= ua << 16;
  return (int32_t)r;
}

static void thrower(void)
{
  longjmp(jb, 42);
}

int main(void)
{
  mb_init();
  mb_puts(inited);
  if (bss_probe[0] | bss_probe[1] | bss_probe[2] | bss_probe[3]) {
    mb_puts("bss FAIL\n");
    mb_finish(2);
  }

  int v = setjmp(jb);
  if (v == 0) {
    thrower();
    mb_puts("longjmp FAIL\n");
    mb_finish(5);
  }
  if (v != 42) {
    mb_puts("setjmp FAIL\n");
    mb_finish(6);
  }
  mb_puts("setjmp ok\n");

  // far sweep over $7E WRAM above the linker's far sections
  volatile __far uint8_t *wram = (volatile __far uint8_t *)0x7ea000;
  for (unsigned i = 0; i < 4096; i++)
    wram[i] = (uint8_t)(i * 7 + 3);
  for (unsigned i = 0; i < 4096; i++)
    if (wram[i] != (uint8_t)(i * 7 + 3)) {
      mb_puts("far FAIL\n");
      mb_finish(7);
    }
  mb_puts("far ok\n");

  // --- int64 helper battery (informational: documents the Calypsi bug) ---
  static volatile int32_t ca[4] = {205887, 0x10000L, 0x7FFFFFFFL, 0x12345678L};
  static volatile int32_t cb[4] = {0x20000L, 0x10000L, 2, 0x1000};
  static const uint32_t exp_hi[4] = {6, 1, 0, 0x123};
  static const uint32_t exp_lo[4] = {0x487e0000, 0, 0xfffffffe, 0x45678000};
  int mulbug = 0;
  for (int i = 0; i < 4; i++) {
    int64_t p = (int64_t)ca[i] * cb[i];
    if ((uint32_t)(p >> 32) != exp_hi[i] || (uint32_t)p != exp_lo[i])
      mulbug++;
  }
  mb_puts(mulbug ? "mul64 BUG confirmed\n" : "mul64 ok (fixed?)\n");

  int64_t sh = (int64_t)205887 << 32;
  mb_puts((uint32_t)(sh >> 32) == 205887 && (uint32_t)sh == 0 ? "shl64 ok\n"
                                                              : "shl64 BUG\n");

  int64_t dq = ((int64_t)205887 << 16) / 205887;
  mb_puts("div64 ");
  mb_puthex32((uint32_t)dq);
  mb_putc('\n');

  // --- the workaround fixmul must be exact ---
  static volatile int32_t ma[5] = {205887, 205887, -205887, 0x7FFFFFFFL, -65536};
  static volatile int32_t mbv[5] = {0x20000L, -0x20000L, -0x20000L, 0x7FFFFFFFL, -65536};
  // wrap case: (2^31-1)^2 >> 16 mod 2^32 = 0xFFFF0000 = -65536
  static const int32_t mexp[5] = {411774, -411774, 411774, -65536, 65536};
  for (int i = 0; i < 5; i++) {
    int32_t r = fixmul(ma[i], mbv[i]);
    mb_puts("fixmul ");
    mb_puthex32((uint32_t)r);
    if (r != mexp[i]) {
      mb_puts(" FAIL\n");
      mb_finish(3);
    }
    mb_putc('\n');
  }

  mb_puts("M0A PASS\n");
  mb_finish(MB_STATUS_PASS);
  return 0;
}
