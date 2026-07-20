/* PICO-8 machine core: RAM, draw state, and the M1 API slice
** (cls/pset/pget/color/camera/clip/flip/btn/printh/time).
** Semantics notes are tagged VERIFY where the behavior must be pinned
** against real PICO-8 by the vector suite (M1 goldens). */
#include <stdio.h>
#include <string.h>
#include "p8.h"
#include "p8num.h"
#include "p8int.h"
#include "lua/lauxlib.h"

#ifndef __CALYPSI__
uint8_t p8_ram[P8_RAM_SIZE];
#endif

static p8_flip_fn flip_fn;
static void *flip_ud;
uint16_t p8i_buttons[2];
uint8_t p8i_held[2][8];
static uint32_t frames;

void p8_set_flip(p8_flip_fn fn, void *ud) { flip_fn = fn; flip_ud = ud; }
void p8_set_buttons(uint16_t p0, uint16_t p1) { p8i_buttons[0] = p0; p8i_buttons[1] = p1; }
uint32_t p8_frame_count(void) { return frames; }

/* number->int with PICO-8 flooring (toward -inf).
** M1: lua_Number is still double; becomes a >>16 under fix32 (M1b). */
int p8a_int(lua_State *L, int idx)
{
  return (int)p8fix_toint((long)lua_tonumber(L, idx));
}

int p8a_optint(lua_State *L, int idx, int def)
{
  if (lua_isnoneornil(L, idx))
    return def;
  return p8a_int(L, idx);
}

static void rnd_seed(uint32_t seed);

void p8_reset(void)
{
  int i;
  /* Clear all 64 KB. Two Calypsi constraints (both benign on the host):
     size_t is 16-bit, so a single memset of P8_RAM_SIZE (0x10000)
     truncates to 0; and `p8_ram + 0x8000u` mis-folds (the const 0x8000u
     addend is sign-extended into the bank byte -> $7E:8000 instead of
     $7F:8000, see bugs/calypsi-const-ptr-offset.c). So walk a running
     pointer in 0x2000 chunks (addend bit-15 clear, folds correctly). */
  {
    uint8_t *p = p8_ram;
    for (i = 0; i < 8; i++) {
      memset(p, 0, 0x2000u);
      p += 0x2000u;
    }
  }
  for (i = 0; i < 16; i++) {
    p8_ram[P8_DS_DRAWPAL + i] = (uint8_t)(i == 0 ? 0x10 : i); /* 0 transparent */
    p8_ram[P8_DS_SCRNPAL + i] = (uint8_t)i;
  }
  p8_ram[P8_DS_CLIP_X1] = 128;
  p8_ram[P8_DS_CLIP_Y1] = 128;
  p8_ram[P8_DS_PEN] = 6;
  rnd_seed(0);
  frames = 0;
}

int p8i_cam_x(void) { return (int)(int16_t)(p8_ram[P8_DS_CAM_X] | (p8_ram[P8_DS_CAM_X + 1] << 8)); }
int p8i_cam_y(void) { return (int)(int16_t)(p8_ram[P8_DS_CAM_Y] | (p8_ram[P8_DS_CAM_Y + 1] << 8)); }

/* raw screen poke, no camera/clip/palette: x,y must be in range */
void p8i_screen_set(int x, int y, uint8_t c)
{
  uint8_t *b = &p8_ram[P8_SCREEN + (uint16_t)(y * 64 + (x >> 1))];
  if (x & 1)
    *b = (uint8_t)((*b & 0x0F) | (c << 4));
  else
    *b = (uint8_t)((*b & 0xF0) | c);
}

uint8_t p8i_screen_get(int x, int y)
{
  uint8_t b = p8_ram[P8_SCREEN + (uint16_t)(y * 64 + (x >> 1))];
  return (x & 1) ? (uint8_t)(b >> 4) : (uint8_t)(b & 0x0F);
}

/* shape pixel: camera + clip + fill pattern + draw palette. The fill
** pattern selects the low (bit 0) or high (bit 1) nibble of the pen
** value; with the 0x5f33 transparency flag, pattern-1 pixels are
** skipped. VERIFY: draw palette assumed to apply to both nibbles. */
void p8i_shape_plot(int x, int y, int col)
{
  uint8_t c;
  x -= p8i_cam_x();
  y -= p8i_cam_y();
  if (x < p8_ram[P8_DS_CLIP_X0] || x >= p8_ram[P8_DS_CLIP_X1] ||
      y < p8_ram[P8_DS_CLIP_Y0] || y >= p8_ram[P8_DS_CLIP_Y1])
    return;
  {
    unsigned int pat = (unsigned int)p8_ram[0x5f31] |
                       ((unsigned int)p8_ram[0x5f32] << 8);
    if (pat) {
      int bit = (pat >> (15 - ((y & 3) * 4 + (x & 3)))) & 1;
      if (bit) {
        if (p8_ram[0x5f33] & 1)
          return; /* transparent secondary */
        c = (uint8_t)((col >> 4) & 0x0F);
      } else {
        c = (uint8_t)(col & 0x0F);
      }
    } else {
      c = (uint8_t)(col & 0x0F);
    }
  }
  p8i_screen_set(x, y, (uint8_t)(p8_ram[P8_DS_DRAWPAL + c] & 0x0F));
}

/* sprite pixel: camera + clip + draw palette with transparency bit */
void p8i_sprite_plot(int x, int y, uint8_t c)
{
  uint8_t m = p8_ram[P8_DS_DRAWPAL + (c & 0x0F)];
  if (m & 0x10)
    return; /* transparent color */
  x -= p8i_cam_x();
  y -= p8i_cam_y();
  if (x < p8_ram[P8_DS_CLIP_X0] || x >= p8_ram[P8_DS_CLIP_X1] ||
      y < p8_ram[P8_DS_CLIP_Y0] || y >= p8_ram[P8_DS_CLIP_Y1])
    return;
  p8i_screen_set(x, y, (uint8_t)(m & 0x0F));
}

uint8_t p8i_sheet_get(int x, int y)
{
  uint8_t b;
  if (x < 0 || x > 127 || y < 0 || y > 127)
    return 0;
  b = p8_ram[P8_GFX + (unsigned int)(y * 64 + (x >> 1))];
  return (x & 1) ? (uint8_t)(b >> 4) : (uint8_t)(b & 0x0F);
}

void p8i_sheet_set(int x, int y, uint8_t c)
{
  uint8_t *b;
  if (x < 0 || x > 127 || y < 0 || y > 127)
    return;
  b = &p8_ram[P8_GFX + (unsigned int)(y * 64 + (x >> 1))];
  if (x & 1)
    *b = (uint8_t)((*b & 0x0F) | (c << 4));
  else
    *b = (uint8_t)((*b & 0xF0) | c);
}

/* --- API --- */

static int api_cls(lua_State *L)
{
  uint8_t c = (uint8_t)(p8a_optint(L, 1, 0) & 0x0F);
  memset(&p8_ram[P8_SCREEN], c | (c << 4), 0x2000);
  p8_ram[P8_DS_CURS_X] = 0;
  p8_ram[P8_DS_CURS_Y] = 0;
  /* VERIFY: does cls also reset clip? (wiki says yes since 0.2.0) */
  p8_ram[P8_DS_CLIP_X0] = 0;
  p8_ram[P8_DS_CLIP_Y0] = 0;
  p8_ram[P8_DS_CLIP_X1] = 128;
  p8_ram[P8_DS_CLIP_Y1] = 128;
  return 0;
}

static int api_pset(lua_State *L)
{
  int x = p8a_int(L, 1);
  int y = p8a_int(L, 2);
  if (!lua_isnoneornil(L, 3))
    p8_ram[P8_DS_PEN] = (uint8_t)(p8a_int(L, 3) & 0xFF);
  p8i_shape_plot(x, y, p8_ram[P8_DS_PEN]);
  return 0;
}

static int api_pget(lua_State *L)
{
  /* VERIFY: camera applied to pget (0.2.x behavior) */
  int x = p8a_int(L, 1) - p8i_cam_x();
  int y = p8a_int(L, 2) - p8i_cam_y();
  uint8_t c = 0;
  if (x >= 0 && x < 128 && y >= 0 && y < 128)
    c = p8i_screen_get(x, y);
  lua_pushnumber(L, (lua_Number)p8fix_fromint(c));
  return 1;
}

static int api_color(lua_State *L)
{
  uint8_t old = p8_ram[P8_DS_PEN];
  p8_ram[P8_DS_PEN] = (uint8_t)(p8a_optint(L, 1, 6) & 0xFF);
  lua_pushnumber(L, (lua_Number)p8fix_fromint(old));
  return 1;
}

static int api_camera(lua_State *L)
{
  int x = p8a_optint(L, 1, 0);
  int y = p8a_optint(L, 2, 0);
  p8_ram[P8_DS_CAM_X] = (uint8_t)(x & 0xFF);
  p8_ram[P8_DS_CAM_X + 1] = (uint8_t)((x >> 8) & 0xFF);
  p8_ram[P8_DS_CAM_Y] = (uint8_t)(y & 0xFF);
  p8_ram[P8_DS_CAM_Y + 1] = (uint8_t)((y >> 8) & 0xFF);
  return 0;
}

static int api_clip(lua_State *L)
{
  if (lua_isnoneornil(L, 1)) {
    p8_ram[P8_DS_CLIP_X0] = 0;
    p8_ram[P8_DS_CLIP_Y0] = 0;
    p8_ram[P8_DS_CLIP_X1] = 128;
    p8_ram[P8_DS_CLIP_Y1] = 128;
  } else {
    int x = p8a_int(L, 1), y = p8a_int(L, 2);
    int w = p8a_int(L, 3), h = p8a_int(L, 4);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    p8_ram[P8_DS_CLIP_X0] = (uint8_t)x;
    p8_ram[P8_DS_CLIP_Y0] = (uint8_t)y;
    p8_ram[P8_DS_CLIP_X1] = (uint8_t)((x + w) > 128 ? 128 : (x + w));
    p8_ram[P8_DS_CLIP_Y1] = (uint8_t)((y + h) > 128 ? 128 : (y + h));
  }
  return 0;
}

static int api_flip(lua_State *L)
{
  (void)L;
  frames++;
  if (flip_fn)
    flip_fn(flip_ud);
  return 0;
}

static int api_btn(lua_State *L)
{
  if (lua_isnoneornil(L, 1)) {
    /* no args: bitmask of both players (p1 in bits 8+) */
    lua_pushnumber(L, (lua_Number)p8fix_fromint((long)(p8i_buttons[0] | ((uint32_t)p8i_buttons[1] << 8))));
    return 1;
  }
  {
    int i = p8a_int(L, 1);
    int p = p8a_optint(L, 2, 0) & 1;
    lua_pushboolean(L, (p8i_buttons[p] >> (i & 7)) & 1);
    return 1;
  }
}

static int api_time(lua_State *L)
{
  /* VERIFY: 30 fps carts advance 1/30 per frame; _update60 carts 1/60.
     M1: assume 30 fps, wrap at 32768 frames. Virtual time by design
     (audio runs real-time). */
  lua_pushnumber(L, (lua_Number)p8fix_div(p8fix_fromint((long)(frames & 0x7FFFu)),
                                          p8fix_fromint(30)));
  return 1;
}

/* --- PICO-8 PRNG (documented in BBS tid=51113): two 32-bit words at
   0x5f44 (lo) / 0x5f48 (hi), little-endian in RAM. Deterministic and
   peekable, which is exactly what parity testing wants.
   VERIFY the srand warmup constant behavior against real PICO-8. */

static uint32_t rnd_get(unsigned int off)
{
  return (uint32_t)p8_ram[off] | ((uint32_t)p8_ram[off + 1] << 8) |
         ((uint32_t)p8_ram[off + 2] << 16) | ((uint32_t)p8_ram[off + 3] << 24);
}

static void rnd_put(unsigned int off, uint32_t v)
{
  p8_ram[off] = (uint8_t)(v & 0xFF);
  p8_ram[off + 1] = (uint8_t)((v >> 8) & 0xFF);
  p8_ram[off + 2] = (uint8_t)((v >> 16) & 0xFF);
  p8_ram[off + 3] = (uint8_t)((v >> 24) & 0xFF);
}

static uint32_t rnd_step(void)
{
  /* BBS tid=51113 formulation. Seeded DRAW STREAMS are verified exact
     against real PICO-8 v0.2.6b (srand(42) -> 0xec81,0x0866; srand(0) ->
     0x8474), which is what carts consume. The word layout at 0x5f44
     diverges from real PICO-8 (its state chain has an extra subtlety we
     have not fully pinned; see docs/real-pico8-oracle.md) — only carts
     that peek/poke 0x5f44 directly would notice. VERIFY if one does. */
  uint32_t lo = rnd_get(P8_HW_RNDLO);
  uint32_t hi = rnd_get(P8_HW_RNDLO + 4);
  hi = ((hi << 16) | (hi >> 16)) + lo;
  lo += hi;
  rnd_put(P8_HW_RNDLO, lo);
  rnd_put(P8_HW_RNDLO + 4, hi);
  return hi;
}

static void rnd_seed(uint32_t seed)
{
  /* srand(0) is srand(0xdeadbeef): same general path incl. warmup
     (0xdeadbeef ^ 0xbead29ba = 0x60009755, the BBS constants). Verified
     states/draws vs real PICO-8 for seeds 0, 42, 0x1234.5678. */
  int i;
  if (seed == 0)
    seed = 0xdeadbeefu;
  rnd_put(P8_HW_RNDLO, seed);
  rnd_put(P8_HW_RNDLO + 4, seed ^ 0xbead29bau);
  for (i = 0; i < 32; i++)
    rnd_step();
}

static int api_rnd(lua_State *L)
{
  if (lua_istable(L, 1)) {
    /* rnd(tbl): random element of the sequence part */
    long n = (long)lua_rawlen(L, 1);
    if (n == 0)
      return 0;
    lua_rawgeti(L, 1, (int)(rnd_step() % (uint32_t)n) + 1);
    return 1;
  }
  {
    uint32_t limit = 0x10000u; /* default: [0,1) */
    if (!lua_isnoneornil(L, 1))
      limit = (uint32_t)(long)lua_tonumber(L, 1);
    lua_pushnumber(L, limit ? (lua_Number)(long)(int32_t)(rnd_step() % limit) : 0);
    return 1;
  }
}

static int api_srand(lua_State *L)
{
  rnd_seed((uint32_t)(long)lua_tonumber(L, 1));
  return 0;
}

/* --- table helpers (PICO-8 built-ins; not part of z8lua) --- */

static int api_add(lua_State *L)
{
  long n, i, k;
  if (!lua_istable(L, 1))
    return 0;
  n = (long)lua_rawlen(L, 1);
  i = lua_isnoneornil(L, 3) ? n + 1 : p8fix_toint((long)lua_tonumber(L, 3));
  if (i < 1) i = 1;
  if (i > n + 1) i = n + 1;
  for (k = n; k >= i; k--) { /* shift up */
    lua_rawgeti(L, 1, (int)k);
    lua_rawseti(L, 1, (int)(k + 1));
  }
  lua_pushvalue(L, 2);
  lua_rawseti(L, 1, (int)i);
  lua_pushvalue(L, 2);
  return 1;
}

static void tbl_remove_at(lua_State *L, long i, long n)
{
  long k;
  for (k = i; k < n; k++) { /* shift down */
    lua_rawgeti(L, 1, (int)(k + 1));
    lua_rawseti(L, 1, (int)k);
  }
  lua_pushnil(L);
  lua_rawseti(L, 1, (int)n);
}

static int api_del(lua_State *L)
{
  long n, i;
  if (!lua_istable(L, 1))
    return 0;
  n = (long)lua_rawlen(L, 1);
  for (i = 1; i <= n; i++) {
    lua_rawgeti(L, 1, (int)i);
    if (lua_rawequal(L, -1, 2)) {
      tbl_remove_at(L, i, n); /* removed value stays on the stack */
      return 1;
    }
    lua_pop(L, 1);
  }
  return 0;
}

static int api_deli(lua_State *L)
{
  long n, i;
  if (!lua_istable(L, 1))
    return 0;
  n = (long)lua_rawlen(L, 1);
  if (n == 0)
    return 0;
  i = lua_isnoneornil(L, 2) ? n : p8fix_toint((long)lua_tonumber(L, 2));
  if (i < 1 || i > n)
    return 0;
  lua_rawgeti(L, 1, (int)i);
  tbl_remove_at(L, i, n);
  return 1;
}

/* all(t): stateful iterator; robust-ish to deletion of the current
   element (advances only when the slot still holds the last value).
   VERIFY edge cases against real PICO-8. */
static int all_iter(lua_State *L)
{
  long i = p8fix_toint((long)lua_tonumber(L, lua_upvalueindex(2)));
  long n = (long)lua_rawlen(L, lua_upvalueindex(1));
  lua_rawgeti(L, lua_upvalueindex(1), (int)i);
  if (!lua_rawequal(L, -1, lua_upvalueindex(3)))
    i--; /* current slot changed (deletion): revisit it */
  lua_pop(L, 1);
  do {
    i++;
    if (i > n)
      return 0;
    lua_rawgeti(L, lua_upvalueindex(1), (int)i);
  } while (lua_isnil(L, -1));
  lua_pushnumber(L, (lua_Number)p8fix_fromint(i));
  lua_replace(L, lua_upvalueindex(2));
  lua_pushvalue(L, -1);
  lua_replace(L, lua_upvalueindex(3));
  return 1;
}

static int api_all(lua_State *L)
{
  if (!lua_istable(L, 1)) {
    lua_pushcfunction(L, NULL); /* unreachable; keeps stack sane */
    return 0;
  }
  lua_pushvalue(L, 1);
  lua_pushnumber(L, 0);
  lua_pushnil(L);
  lua_pushcclosure(L, all_iter, 3);
  return 1;
}

static int api_foreach(lua_State *L)
{
  long i = 0, n;
  if (!lua_istable(L, 1))
    return 0;
  n = (long)lua_rawlen(L, 1);
  for (i = 1; i <= n; i++) {
    lua_rawgeti(L, 1, (int)i);
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
      continue;
    }
    lua_pushvalue(L, 2);
    lua_insert(L, -2);
    lua_call(L, 1, 0);
    n = (long)lua_rawlen(L, 1); /* table may shrink during iteration */
  }
  return 0;
}

static int api_count(lua_State *L)
{
  long n, i, c = 0;
  if (!lua_istable(L, 1))
    return 0;
  n = (long)lua_rawlen(L, 1);
  if (lua_isnoneornil(L, 2)) {
    lua_pushnumber(L, (lua_Number)p8fix_fromint(n));
    return 1;
  }
  for (i = 1; i <= n; i++) {
    lua_rawgeti(L, 1, (int)i);
    if (lua_rawequal(L, -1, 2))
      c++;
    lua_pop(L, 1);
  }
  lua_pushnumber(L, (lua_Number)p8fix_fromint(c));
  return 1;
}

/* --- memory API: function forms of @/%/$ plus poke/memcpy/memset --- */

static int api_peek(lua_State *L)
{
  unsigned int a = (unsigned int)p8a_int(L, 1) & 0xFFFFu;
  int n = lua_isnoneornil(L, 2) ? 1 : p8a_int(L, 2);
  int i;
  if (n > 8192) n = 8192;
  lua_checkstack(L, n);
  for (i = 0; i < n; i++)
    lua_pushnumber(L, (lua_Number)p8fix_fromint(p8_ram[(a + (unsigned int)i) & 0xFFFFu]));
  return n < 0 ? 0 : n;
}

static int api_poke(lua_State *L)
{
  unsigned int a = (unsigned int)p8a_int(L, 1) & 0xFFFFu;
  int argc = lua_gettop(L), i;
  for (i = 2; i <= argc; i++)
    p8_ram[(a + (unsigned int)(i - 2)) & 0xFFFFu] = (uint8_t)p8a_int(L, i);
  if (argc == 1)
    p8_ram[a] = 0;
  return 0;
}

static int api_peek2(lua_State *L)
{
  unsigned int a = (unsigned int)p8a_int(L, 1) & 0xFFFFu;
  long v = (long)p8_ram[a] | ((long)p8_ram[(a + 1) & 0xFFFFu] << 8);
  if (v >= 0x8000L) v -= 0x10000L; /* signed 16-bit */
  lua_pushnumber(L, (lua_Number)p8fix_fromint(v));
  return 1;
}

static int api_poke2(lua_State *L)
{
  unsigned int a = (unsigned int)p8a_int(L, 1) & 0xFFFFu;
  long v = p8a_int(L, 2);
  p8_ram[a] = (uint8_t)(v & 0xFF);
  p8_ram[(a + 1) & 0xFFFFu] = (uint8_t)((v >> 8) & 0xFF);
  return 0;
}

static int api_peek4(lua_State *L)
{
  unsigned int a = (unsigned int)p8a_int(L, 1) & 0xFFFFu;
  uint32_t v = (uint32_t)p8_ram[a] | ((uint32_t)p8_ram[(a + 1) & 0xFFFFu] << 8) |
               ((uint32_t)p8_ram[(a + 2) & 0xFFFFu] << 16) |
               ((uint32_t)p8_ram[(a + 3) & 0xFFFFu] << 24);
  lua_pushnumber(L, (lua_Number)(long)(int32_t)v); /* raw 16.16 */
  return 1;
}

static int api_poke4(lua_State *L)
{
  unsigned int a = (unsigned int)p8a_int(L, 1) & 0xFFFFu;
  uint32_t v = (uint32_t)(long)lua_tonumber(L, 2); /* raw 16.16 */
  p8_ram[a] = (uint8_t)(v & 0xFF);
  p8_ram[(a + 1) & 0xFFFFu] = (uint8_t)((v >> 8) & 0xFF);
  p8_ram[(a + 2) & 0xFFFFu] = (uint8_t)((v >> 16) & 0xFF);
  p8_ram[(a + 3) & 0xFFFFu] = (uint8_t)((v >> 24) & 0xFF);
  return 0;
}

static int api_memcpy(lua_State *L)
{
  unsigned int d = (unsigned int)p8a_int(L, 1) & 0xFFFFu;
  unsigned int s = (unsigned int)p8a_int(L, 2) & 0xFFFFu;
  long n = p8a_int(L, 3);
  long i;
  for (i = 0; i < n; i++) /* byte loop: overlapping copies match PICO-8 */
    p8_ram[(d + (unsigned int)i) & 0xFFFFu] = p8_ram[(s + (unsigned int)i) & 0xFFFFu];
  return 0;
}

static int api_memset(lua_State *L)
{
  unsigned int d = (unsigned int)p8a_int(L, 1) & 0xFFFFu;
  uint8_t v = (uint8_t)p8a_int(L, 2);
  long n = p8a_int(L, 3);
  long i;
  for (i = 0; i < n; i++)
    p8_ram[(d + (unsigned int)i) & 0xFFFFu] = v;
  return 0;
}

static int api_printh(lua_State *L)
{
  const char *s;
  if (lua_isnone(L, 1)) { /* zero args (e.g. printh(tonum("x"))) */
    fprintf(stderr, "\n");
    return 0;
  }
  s = luaL_tolstring(L, 1, NULL);
  fprintf(stderr, "%s\n", s);
  lua_pop(L, 1);
  return 0;
}

static const luaL_Reg p8_funcs[] = {
  { "cls", api_cls },
  { "pset", api_pset },
  { "pget", api_pget },
  { "color", api_color },
  { "camera", api_camera },
  { "clip", api_clip },
  { "flip", api_flip },
  { "btn", api_btn },
  { "time", api_time },
  { "t", api_time },
  { "printh", api_printh },
  { "peek", api_peek },
  { "poke", api_poke },
  { "peek2", api_peek2 },
  { "poke2", api_poke2 },
  { "peek4", api_peek4 },
  { "poke4", api_poke4 },
  { "memcpy", api_memcpy },
  { "memset", api_memset },
  { "rnd", api_rnd },
  { "srand", api_srand },
  { "add", api_add },
  { "del", api_del },
  { "deli", api_deli },
  { "all", api_all },
  { "foreach", api_foreach },
  { "count", api_count },
  { NULL, NULL }
};

void p8_open(lua_State *L)
{
  const luaL_Reg *r;
  for (r = p8_funcs; r->name; r++)
    lua_register(L, r->name, r->func);
  p8gfx_open(L);
}
