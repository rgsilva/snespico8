/* Calypsi 5.17/5.18 cc65816 -O0 (large code/data): constant folding of
** a constant pointer plus a 16-bit unsigned constant offset whose bit 15
** is set SIGN-EXTENDS the offset into the bank byte.
**
**   (uint8_t *)0x7f0000ul + 0x8000u   folds to $7E:8000  (want $7F:8000)
**   (uint8_t *)0x7f0000ul + 0x4000u   folds to $7F:4000  (correct; bit15 clear)
**
** i.e. 0x8000u is treated as (int)-32768 and sign-extended to 0xFFFF8000
** before the add: 0x007F0000 + 0xFFFF8000 = 0x007E8000. Casting the
** addend to unsigned long does NOT help the fold. This is the same
** signed-treatment-of-unsigned-constants family as the division-fold bug
** (bugs/calypsi-unsigned-div-fold.c).
**
** NOT affected (verified in the .s): runtime offsets and array indexing,
** because `sta [dp],y` adds Y as an unsigned 16-bit value in hardware, so
** base[runtime_0x8000] and base[0x8000u] as an lvalue index both reach
** $7F:8000 correctly. Only the folded constant-pointer VALUE is wrong.
**
** Bit this project in p8_reset's `memset(p8_ram + 0x8000u, 0, ...)`, which
** cleared $7E:8000-$7E:FFFF (the Mode 7 buffer + mailbox) instead of the
** PICO-8 extended RAM. Workaround: advance a running pointer in chunks
** with each addend's bit 15 clear (<= 0x4000).
*/
#include <stdint.h>

void *repro(void)
{
  return (uint8_t *)0x7f0000ul + 0x8000u; /* returns $7E:8000, want $7F:8000 */
}
