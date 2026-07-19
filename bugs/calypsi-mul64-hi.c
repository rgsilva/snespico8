// Calypsi 5.18 cc65816: 64-bit multiply returns the HIGH 32 bits of the
// product right-shifted by one; low 32 bits are correct. Reproduces at
// -O0 and -O2, --code-model=large --data-model=large, both with
// clib-lc-ld-snes-noppu.a and generic clib-lc-ld.a. 64-bit shift-left by
// 32 is also wrong; 64-bit divide of a 48-bit dividend appeared correct.
//
//   205887 * 0x20000      -> got hi 0x00000003, want 0x00000006 (lo ok)
//   0x10000 * 0x10000     -> got hi 0x00000000, want 0x00000001 (lo ok)
//   0x12345678 * 0x1000   -> got hi 0x00000091, want 0x00000123 (lo ok)
//
// Observed on macOS x86_64 build of 5.18, running on SNES (Mesen2) —
// values read back via a WRAM mailbox. Not yet reported upstream.
#include <stdint.h>

volatile int32_t a = 205887, b = 0x20000L;

int64_t repro(void)
{
  return (int64_t)a * b; // hi word comes back halved
}
