#include <stddef.h>
extern void take(int);
void t1(void) { // Lua 5.2.4 MAX_SIZET form: ((size_t)(~(size_t)0)-2)
  if ((size_t)46 <= ((size_t)(~(size_t)0)-2)/(size_t)16) take(1); else take(2);
}
void t2(void) { // literal form
  if ((size_t)46 <= (size_t)0xFFFFFFFDu/(size_t)16) take(1); else take(2);
}
void t3(void) { // smaller literal sanity check
  if ((size_t)46 <= (size_t)4096) take(1); else take(2);
}
// FULL CHARACTERIZATION (see also fold4 tests): on Calypsi 65816,
// unsigned int and size_t are 16-BIT. Constant folding of unsigned
// comparisons AND divisions at 16-bit width treats operands as SIGNED:
//   (size_t)46 <= (size_t)0xFFFDu/(size_t)10  -> folds to else-branch
//   46u <= 0xFFFDu/10u                        -> folds to else-branch
//   (size_t)46 <= (size_t)0xFFFFu             -> folds to else-branch
// (0xFFFD/10 folded as -3/10 = 0). Runtime comparisons are CORRECT;
// only compile-time folds are affected. 32-bit (unsigned long) folds
// are correct. Workaround: keep guard constants <= 0x7FFF (sign clear),
// e.g. Lua's MAX_SIZET = (size_t)0x7FFF.
// Original (narrower) note: unsigned 32-bit constant division folds
// as SIGNED when the dividend has the sign bit set: 0xFFFFFFFDu/16 folds
// to 0 (as -3/16), so t1/t2 call take(2) — should be take(1). t3 (no
// division) folds correctly. Breaks every `x <= MAX_SIZET/n` guard in
// Lua (luaM_reallocv, string/table growth) into the error branch.
// Workaround: define MAX_SIZET as 0x7FFFFFFF (sign bit clear).
