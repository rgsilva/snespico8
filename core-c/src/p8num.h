/* PICO-8 16.16 fixed point ("fix32") in pure C.
**
** NO int64 anywhere: Calypsi's 64-bit helpers are broken
** (bugs/calypsi-mul64-hi.c). Wrap semantics via unsigned 32-bit
** arithmetic — this both avoids signed-overflow UB and matches PICO-8's
** observed overflow behavior (0x7fff.ffff + 0x0.0001 == -32768).
**
** Values travel as `long` (LUAI_UACNUMBER); the invariant is that they
** always hold a value in int32 range (the raw 16.16 bits, sign-extended
** on LP64 hosts). Declared with plain C types so luaconf.h can
** reference these without include cycles. */
#ifndef P8NUM_H
#define P8NUM_H

long p8fix_add(long a, long b);
long p8fix_sub(long a, long b);
long p8fix_mul(long a, long b);
long p8fix_div(long a, long b);   /* /0 -> +-0x7fff.ffff by dividend sign */
long p8fix_mod(long a, long b);   /* result in [0,|b|) — oracle-verified */
long p8fix_pow(long a, long b);
long p8fix_unm(long a);
long p8fix_sqrt(long v);
long p8fix_floor(long a);         /* as fix32 */
long p8fix_fromint(long i);       /* wraps beyond +-32767, like PICO-8 */
long p8fix_toint(long a);         /* floor, as plain integer */

int p8_fix2str(char *buf, long v);               /* PICO-8 tostr, ret len */
long p8_str2fix(const char *s, char **endptr);   /* dec, 0x.., 0b.., .5 */

#endif
