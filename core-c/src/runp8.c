/* runp8 — headless PICO-8 reference runner (M1).
** Loads a .p8 text cart, runs _init then N frames of _update/_draw with
** an implicit flip after _draw, and prints a per-flip FNV-1a hash of
** the 8 KB screen. Deterministic by construction (fixed hash seed, no
** wall clock). This is the oracle the SNES build is diffed against.
**
** usage: runp8 cart.p8 [-n frames] [-i inputs.txt] [-p out.ppm]
**   inputs.txt: one hex byte per line = player-0 button mask per frame
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "p8.h"
#include "lua/lauxlib.h"
#include "lua/lualib.h"

/* ---- .p8 text cart parsing ---- */

static int hexval(int c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/* __gfx__: 128 hex chars/line, one pixel each, left pixel = low nibble.
** __map__: 256 hex chars/line, one BYTE each pair (big-nibble first).
** __gff__: like map, 256 bytes total. */
static void parse_hex_pixels(const char *line, uint8_t *dst, unsigned *off)
{
  int lo = -1;
  const char *p;
  for (p = line; *p && *p != '\n' && *p != '\r'; p++) {
    int v = hexval(*p);
    if (v < 0)
      continue;
    if (lo < 0) {
      lo = v;
    } else {
      dst[(*off)++] = (uint8_t)(lo | (v << 4)); /* left px low nibble */
      lo = -1;
    }
  }
}

static void parse_hex_bytes(const char *line, uint8_t *dst, unsigned *off)
{
  int hi = -1;
  const char *p;
  for (p = line; *p && *p != '\n' && *p != '\r'; p++) {
    int v = hexval(*p);
    if (v < 0)
      continue;
    if (hi < 0) {
      hi = v;
    } else {
      dst[(*off)++] = (uint8_t)((hi << 4) | v); /* "ab" -> 0xab */
      hi = -1;
    }
  }
}

/* returns malloc'd lua source; fills RAM sections directly */
static char *load_cart(const char *path)
{
  FILE *f = fopen(path, "rb");
  char line[512];
  char *lua = NULL;
  size_t lua_len = 0, lua_cap = 0;
  enum { S_NONE, S_LUA, S_GFX, S_MAP, S_GFF } sec = S_NONE;
  unsigned gfx_off = 0, map_off = 0, gff_off = 0;

  if (!f) {
    fprintf(stderr, "runp8: cannot open %s\n", path);
    exit(2);
  }
  while (fgets(line, sizeof line, f)) {
    if (!strncmp(line, "__lua__", 7)) { sec = S_LUA; continue; }
    if (!strncmp(line, "__gfx__", 7)) { sec = S_GFX; continue; }
    if (!strncmp(line, "__map__", 7)) { sec = S_MAP; continue; }
    if (!strncmp(line, "__gff__", 7)) { sec = S_GFF; continue; }
    if (!strncmp(line, "__", 2) && strstr(line, "__")) { sec = S_NONE; continue; }
    switch (sec) {
    case S_LUA: {
      size_t n = strlen(line);
      if (lua_len + n + 1 > lua_cap) {
        lua_cap = lua_cap ? lua_cap * 2 : 4096;
        while (lua_cap < lua_len + n + 1)
          lua_cap *= 2;
        lua = (char *)realloc(lua, lua_cap);
      }
      memcpy(lua + lua_len, line, n);
      lua_len += n;
      break;
    }
    case S_GFX:
      if (gfx_off < 0x2000)
        parse_hex_pixels(line, &p8_ram[P8_GFX], &gfx_off);
      break;
    case S_MAP:
      if (map_off < 0x1000)
        parse_hex_bytes(line, &p8_ram[P8_MAP], &map_off);
      break;
    case S_GFF:
      if (gff_off < 0x100)
        parse_hex_bytes(line, &p8_ram[P8_GFLAGS], &gff_off);
      break;
    default:
      break;
    }
  }
  fclose(f);
  if (lua)
    lua[lua_len] = 0;
  else
    lua = strdup("");
  return lua;
}

/* ---- flip hook: FNV-1a over the screen ---- */

static uint32_t fnv1a(const uint8_t *p, size_t n)
{
  uint32_t h = 2166136261u;
  size_t i;
  for (i = 0; i < n; i++) {
    h ^= p[i];
    h *= 16777619u;
  }
  return h;
}

static void on_flip(void *ud)
{
  (void)ud;
  printf("FLIP %u %08x\n", (unsigned)p8_frame_count(),
         (unsigned)fnv1a(&p8_ram[P8_SCREEN], 0x2000));
}

/* ---- PPM dump for eyeballing ---- */

static const unsigned char PAL[16][3] = {
  {0x00,0x00,0x00},{0x1D,0x2B,0x53},{0x7E,0x25,0x53},{0x00,0x87,0x51},
  {0xAB,0x52,0x36},{0x5F,0x57,0x4F},{0xC2,0xC3,0xC7},{0xFF,0xF1,0xE8},
  {0xFF,0x00,0x4D},{0xFF,0xA3,0x00},{0xFF,0xEC,0x27},{0x00,0xE4,0x36},
  {0x29,0xAD,0xFF},{0x83,0x76,0x9C},{0xFF,0x77,0xA8},{0xFF,0xCC,0xAA}
};

static void dump_ppm(const char *path)
{
  FILE *f = fopen(path, "wb");
  int x, y;
  if (!f)
    return;
  fprintf(f, "P6\n128 128\n255\n");
  for (y = 0; y < 128; y++)
    for (x = 0; x < 128; x++) {
      uint8_t b = p8_ram[P8_SCREEN + y * 64 + (x >> 1)];
      uint8_t c = (x & 1) ? (b >> 4) : (b & 0x0F);
      fwrite(PAL[c & 0x0F], 1, 3, f);
    }
  fclose(f);
  fprintf(stderr, "wrote %s\n", path);
}

/* ---- main loop ---- */

static void call_global(lua_State *L, const char *name)
{
  lua_getglobal(L, name);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return;
  }
  if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
    fprintf(stderr, "runp8: error in %s: %s\n", name, lua_tostring(L, -1));
    exit(3);
  }
}

static int has_global(lua_State *L, const char *name)
{
  int r;
  lua_getglobal(L, name);
  r = !lua_isnil(L, -1);
  lua_pop(L, 1);
  return r;
}

int main(int argc, char **argv)
{
  const char *cart = NULL, *inputs = NULL, *ppm = NULL;
  int nframes = 30, i;
  FILE *inf = NULL;
  char *src;
  lua_State *L;

  for (i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-n") && i + 1 < argc) nframes = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-i") && i + 1 < argc) inputs = argv[++i];
    else if (!strcmp(argv[i], "-p") && i + 1 < argc) ppm = argv[++i];
    else cart = argv[i];
  }
  if (!cart) {
    fprintf(stderr, "usage: runp8 cart.p8 [-n frames] [-i inputs.txt] [-p out.ppm]\n");
    return 1;
  }

  p8_reset();
  src = load_cart(cart);

  L = luaL_newstate();
  luaL_requiref(L, "_G", luaopen_base, 1);
  lua_pop(L, 1);
  luaL_requiref(L, "coroutine", luaopen_coroutine, 1);
  lua_pop(L, 1);
  luaL_requiref(L, "pico8", luaopen_pico8, 1);
  lua_pop(L, 1);
  lua_setpico8memory(L, p8_ram);
  /* PICO-8 exposes coroutines under global names */
  lua_getglobal(L, "coroutine");
  lua_getfield(L, -1, "create");
  lua_setglobal(L, "cocreate");
  lua_getfield(L, -1, "resume");
  lua_setglobal(L, "coresume");
  lua_getfield(L, -1, "status");
  lua_setglobal(L, "costatus");
  lua_getfield(L, -1, "yield");
  lua_setglobal(L, "yield");
  lua_pop(L, 1);
  p8_open(L);
  p8_set_flip(on_flip, NULL);

  if (luaL_loadbuffer(L, src, strlen(src), cart) != LUA_OK) {
    fprintf(stderr, "runp8: parse error: %s\n", lua_tostring(L, -1));
    return 3;
  }
  if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
    fprintf(stderr, "runp8: cart main chunk: %s\n", lua_tostring(L, -1));
    return 3;
  }

  if (inputs)
    inf = fopen(inputs, "rb");

  call_global(L, "_init");
  {
    const char *upd = has_global(L, "_update60") ? "_update60" : "_update";
    for (i = 0; i < nframes; i++) {
      unsigned mask = 0;
      if (inf) {
        char ln[16];
        if (fgets(ln, sizeof ln, inf))
          mask = (unsigned)strtoul(ln, NULL, 16);
      }
      p8_set_buttons((uint16_t)mask, 0);
      call_global(L, upd);
      call_global(L, "_draw");
      lua_getglobal(L, "flip");   /* implicit flip after _draw */
      lua_call(L, 0, 0);
    }
  }
  if (inf)
    fclose(inf);
  if (ppm)
    dump_ppm(ppm);
  printf("DONE frames=%u\n", (unsigned)p8_frame_count());
  free(src);
  lua_close(L);
  return 0;
}
