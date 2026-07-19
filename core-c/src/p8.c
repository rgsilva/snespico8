/* PICO-8 machine core: RAM, draw state, and the M1 API slice
** (cls/pset/pget/color/camera/clip/flip/btn/printh/time).
** Semantics notes are tagged VERIFY where the behavior must be pinned
** against real PICO-8 by the vector suite (M1 goldens). */
#include <stdio.h>
#include <string.h>
#include "p8.h"
#include "p8num.h"
#include "lua/lauxlib.h"

uint8_t p8_ram[P8_RAM_SIZE];

static p8_flip_fn flip_fn;
static void *flip_ud;
static uint16_t buttons[2];
static uint32_t frames;

void p8_set_flip(p8_flip_fn fn, void *ud) { flip_fn = fn; flip_ud = ud; }
void p8_set_buttons(uint16_t p0, uint16_t p1) { buttons[0] = p0; buttons[1] = p1; }
uint32_t p8_frame_count(void) { return frames; }

/* number->int with PICO-8 flooring (toward -inf).
** M1: lua_Number is still double; becomes a >>16 under fix32 (M1b). */
static int p8_toint(lua_State *L, int idx)
{
  return (int)p8fix_toint((long)lua_tonumber(L, idx));
}

static int p8_optint(lua_State *L, int idx, int def)
{
  if (lua_isnoneornil(L, idx))
    return def;
  return p8_toint(L, idx);
}

void p8_reset(void)
{
  int i;
  memset(p8_ram, 0, sizeof p8_ram);
  for (i = 0; i < 16; i++) {
    p8_ram[P8_DS_DRAWPAL + i] = (uint8_t)(i == 0 ? 0x10 : i); /* 0 transparent */
    p8_ram[P8_DS_SCRNPAL + i] = (uint8_t)i;
  }
  p8_ram[P8_DS_CLIP_X1] = 128;
  p8_ram[P8_DS_CLIP_Y1] = 128;
  p8_ram[P8_DS_PEN] = 6;
  frames = 0;
}

static int16_t cam_x(void) { return (int16_t)(p8_ram[P8_DS_CAM_X] | (p8_ram[P8_DS_CAM_X + 1] << 8)); }
static int16_t cam_y(void) { return (int16_t)(p8_ram[P8_DS_CAM_Y] | (p8_ram[P8_DS_CAM_Y + 1] << 8)); }

/* raw screen poke, no camera/clip/palette: x,y must be in range */
static void screen_set(int x, int y, uint8_t c)
{
  uint8_t *b = &p8_ram[P8_SCREEN + (uint16_t)(y * 64 + (x >> 1))];
  if (x & 1)
    *b = (uint8_t)((*b & 0x0F) | (c << 4));
  else
    *b = (uint8_t)((*b & 0xF0) | c);
}

static uint8_t screen_get(int x, int y)
{
  uint8_t b = p8_ram[P8_SCREEN + (uint16_t)(y * 64 + (x >> 1))];
  return (x & 1) ? (uint8_t)(b >> 4) : (uint8_t)(b & 0x0F);
}

/* one camera-relative, clipped, draw-palette-mapped pixel */
static void draw_pixel(int x, int y, uint8_t c)
{
  x -= cam_x();
  y -= cam_y();
  if (x < p8_ram[P8_DS_CLIP_X0] || x >= p8_ram[P8_DS_CLIP_X1] ||
      y < p8_ram[P8_DS_CLIP_Y0] || y >= p8_ram[P8_DS_CLIP_Y1])
    return;
  screen_set(x, y, (uint8_t)(p8_ram[P8_DS_DRAWPAL + (c & 0x0F)] & 0x0F));
}

/* --- API --- */

static int api_cls(lua_State *L)
{
  uint8_t c = (uint8_t)(p8_optint(L, 1, 0) & 0x0F);
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
  int x = p8_toint(L, 1);
  int y = p8_toint(L, 2);
  if (!lua_isnoneornil(L, 3))
    p8_ram[P8_DS_PEN] = (uint8_t)(p8_toint(L, 3) & 0xFF);
  draw_pixel(x, y, p8_ram[P8_DS_PEN]);
  return 0;
}

static int api_pget(lua_State *L)
{
  /* VERIFY: camera applied to pget (0.2.x behavior) */
  int x = p8_toint(L, 1) - cam_x();
  int y = p8_toint(L, 2) - cam_y();
  uint8_t c = 0;
  if (x >= 0 && x < 128 && y >= 0 && y < 128)
    c = screen_get(x, y);
  lua_pushnumber(L, (lua_Number)p8fix_fromint(c));
  return 1;
}

static int api_color(lua_State *L)
{
  uint8_t old = p8_ram[P8_DS_PEN];
  p8_ram[P8_DS_PEN] = (uint8_t)(p8_optint(L, 1, 6) & 0xFF);
  lua_pushnumber(L, (lua_Number)p8fix_fromint(old));
  return 1;
}

static int api_camera(lua_State *L)
{
  int x = p8_optint(L, 1, 0);
  int y = p8_optint(L, 2, 0);
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
    int x = p8_toint(L, 1), y = p8_toint(L, 2);
    int w = p8_toint(L, 3), h = p8_toint(L, 4);
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
    lua_pushnumber(L, (lua_Number)p8fix_fromint((long)(buttons[0] | ((uint32_t)buttons[1] << 8))));
    return 1;
  }
  {
    int i = p8_toint(L, 1);
    int p = p8_optint(L, 2, 0) & 1;
    lua_pushboolean(L, (buttons[p] >> (i & 7)) & 1);
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

static int api_printh(lua_State *L)
{
  const char *s = luaL_tolstring(L, 1, NULL);
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
  { NULL, NULL }
};

void p8_open(lua_State *L)
{
  const luaL_Reg *r;
  for (r = p8_funcs; r->name; r++)
    lua_register(L, r->name, r->func);
}
