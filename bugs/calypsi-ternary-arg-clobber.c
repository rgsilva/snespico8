/* Calypsi 5.17 cc65816 -O0 (large code/data): the result of a `?:`
** ternary passed DIRECTLY as a call argument is clobbered before use
** when the call is itself an argument to (or cast feeding) another call.
**
** ret = (long)f( cond ? 1 : 0 );
**
** compiles the ternary into A (a branch selecting ##0 or ##1), then
** immediately executes `lda 1,s` — reloading a spilled slot into A —
** BEFORE the int->long widening and the call to f(). So f() receives
** stack garbage, not 0/1. Observed values were non-deterministic across
** builds (uninitialized-looking), which is the tell.
**
** Bit this project in pico8_tonum's LUA_TBOOLEAN case:
**   ret = (lua_Number)p8fix_fromint(lua_toboolean(l,1) ? 1 : 0);
** so tonum(true)/tonum(false) returned garbage on the SNES while the
** host build was correct. Caught by the M4 SNES<->host parity suite
** (tests/snesparity.sh), invisible to host-only testing.
**
** Workaround: force the ternary into a named local first:
**   long bv = cond ? 1 : 0;
**   ret = (long)f(bv);
** The named local gets a clean store/reload and survives.
**
** Disassembly of the miscompile (the BOOLEAN case body):
**   jsl  long:lua_toboolean
**   inc a / dec a / bne L1 / lda ##0 / bra L2   ; A = ternary (0 or 1)
**   L1: lda ##1
**   L2: lda 1,s                                  ; <-- A CLOBBERED here
**   ldx ##0 / tay / bpl / dex                    ; widen the garbage
**   jsl  long:p8fix_fromint                       ; f(garbage)
*/
#include <stdint.h>

extern long widen(long);

long repro(int cond)
{
  return widen(cond ? 1 : 0); /* widen() receives garbage, not 0/1 */
}
