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

extern unsigned long p8_vmops;  /* lvm.c, M1.5 measurement */

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

/* collect hex nibbles (0..15) from a line, skipping non-hex; returns count */
static int gather_nibbles(const char *line, uint8_t *nb, int max)
{
  int cnt = 0;
  const char *p;
  for (p = line; *p && *p != '\n' && *p != '\r' && cnt < max; p++) {
    int v = hexval(*p);
    if (v >= 0)
      nb[cnt++] = (uint8_t)v;
  }
  return cnt;
}

/* __sfx__ line (168 chars): mode/speed/loopstart/loopend (2 hex each) then
** 32 notes of 5 hex (pitch=2, wave=1, vol=1, eff=1). Packs to 68 bytes:
** 32 LE note words [pitch | wave<<6 | vol<<9 | eff<<12] then the 4 footer
** bytes. dst = &p8_ram[P8_SFX + sfx*68]. (docs/audio-format.md) */
static void parse_sfx_line(const char *line, uint8_t *dst)
{
  uint8_t nb[168];
  int cnt = gather_nibbles(line, nb, 168), k;
  if (cnt < 8)
    return;
  dst[64] = (uint8_t)((nb[0] << 4) | nb[1]);  /* mode  */
  dst[65] = (uint8_t)((nb[2] << 4) | nb[3]);  /* speed */
  dst[66] = (uint8_t)((nb[4] << 4) | nb[5]);  /* loop start */
  dst[67] = (uint8_t)((nb[6] << 4) | nb[7]);  /* loop end */
  for (k = 0; k < 32; k++) {
    int o = 8 + k * 5;
    unsigned word;
    if (o + 5 > cnt)
      break;
    word = (unsigned)(((nb[o] << 4) | nb[o + 1]) & 0x3f)  /* pitch */
         | ((unsigned)nb[o + 2] << 6)                     /* wave  */
         | ((unsigned)nb[o + 3] << 9)                     /* vol   */
         | ((unsigned)nb[o + 4] << 12);                   /* eff   */
    dst[k * 2] = (uint8_t)(word & 0xff);
    dst[k * 2 + 1] = (uint8_t)(word >> 8);
  }
}

/* __music__ line "FF CCCCCCCC": flags byte + 4 channel bytes. Memory is
** the 4 channel bytes with the loop flags folded into bit 7 of bytes 0/1/2
** (loop-start / loop-end / stop). dst = &p8_ram[P8_SONG + pat*4]. */
static void parse_music_line(const char *line, uint8_t *dst)
{
  uint8_t nb[16];
  int cnt = gather_nibbles(line, nb, 16), j, flags;
  if (cnt < 10)
    return;
  flags = (nb[0] << 4) | nb[1];
  for (j = 0; j < 4; j++)
    dst[j] = (uint8_t)((nb[2 + j * 2] << 4) | nb[3 + j * 2]);
  if (flags & 1) dst[0] |= 0x80;
  if (flags & 2) dst[1] |= 0x80;
  if (flags & 4) dst[2] |= 0x80;
}

/* returns malloc'd lua source; fills RAM sections directly */
static char *load_cart(const char *path)
{
  FILE *f = fopen(path, "rb");
  char line[512];
  char *lua = NULL;
  size_t lua_len = 0, lua_cap = 0;
  enum { S_NONE, S_LUA, S_GFX, S_MAP, S_GFF, S_SFX, S_MUSIC } sec = S_NONE;
  unsigned gfx_off = 0, map_off = 0, gff_off = 0, sfx_idx = 0, music_idx = 0;

  if (!f) {
    fprintf(stderr, "runp8: cannot open %s\n", path);
    exit(2);
  }
  while (fgets(line, sizeof line, f)) {
    if (!strncmp(line, "__lua__", 7)) { sec = S_LUA; continue; }
    if (!strncmp(line, "__gfx__", 7)) { sec = S_GFX; continue; }
    if (!strncmp(line, "__map__", 7)) { sec = S_MAP; continue; }
    if (!strncmp(line, "__gff__", 7)) { sec = S_GFF; continue; }
    if (!strncmp(line, "__sfx__", 7)) { sec = S_SFX; continue; }
    if (!strncmp(line, "__music__", 9)) { sec = S_MUSIC; continue; }
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
    case S_SFX:
      if (sfx_idx < 64)
        parse_sfx_line(line, &p8_ram[P8_SFX + sfx_idx++ * 68u]);
      break;
    case S_MUSIC:
      if (music_idx < 64)
        parse_music_line(line, &p8_ram[P8_SONG + music_idx++ * 4u]);
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

static unsigned long ops_at_last_flip;
static int hash_mode; /* -H: emit machine-RAM parity hashes per flip */
static int audio_trace; /* -A: print the sfx()/music() command trace */

/* audio command seam: on the host this is the parity trace (the SNES build
** mails the same commands to the SPC). Deterministic per cart, so host and
** SNES must emit identical lines. */
static void audio_cmd_trace(int kind, int a, int b, int c, int d)
{
  if (!audio_trace)
    return;
  if (kind == P8_ACMD_SFX)
    printf("SFX %d %d %d %d f=%u\n", a, b, c, d, (unsigned)p8_frame_count());
  else
    printf("MUS %d %d %d f=%u\n", a, b, c, (unsigned)p8_frame_count());
}

/* the note stream the SPC sequencer must reproduce (M6c golden) */
static void audio_note_trace(int channel, int wave, int pitch, int vol, int eff)
{
  if (audio_trace)
    printf("NOTE ch=%d w=%d p=%d v=%d e=%d f=%u\n", channel, wave, pitch,
           vol, eff, (unsigned)p8_frame_count());
}

static void on_flip(void *ud)
{
  (void)ud;
  printf("FLIP %u %08x ops=%lu\n", (unsigned)p8_frame_count(),
         (unsigned)fnv1a(&p8_ram[P8_SCREEN], 0x2000),
         p8_vmops - ops_at_last_flip);
  ops_at_last_flip = p8_vmops;
  if (hash_mode)
    /* two regions so a divergence localizes: low RAM [0,0x6000)
       (gfx/map/flags/sfx/music/user/persist/draw+hw state) and the
       screen [0x6000,0x8000). Must match p8main's snes_flip exactly. */
    printf("HASH %08x %08x\n",
           (unsigned)p8_hash_region(0x0000u, 0x6000u),
           (unsigned)p8_hash_region(0x6000u, 0x8000u));
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

/* --- optional p8heap measurement mode (M5): run the cart through the
** SNES region allocator with a chosen window layout to measure whether a
** real cart fits and the largest single allocation it needs. Each window
** is a separate malloc so cross-window coalescing can't happen (faithful
** to the SNES's non-contiguous SRAM windows). */
extern void p8heap_add_region(void *base, unsigned long size);
extern void *p8heap_lua_alloc(void *ud, void *ptr, size_t o, size_t n);
extern unsigned long p8heap_free_bytes(void);
extern unsigned long p8heap_peak_used, p8heap_max_ok, p8heap_max_fail, p8heap_nfail;

static int measure_win_kb, measure_total_kb;

static void build_measure_heap(void)
{
  /* One pool, windows separated by a guard gap so p8heap can't coalesce
     across window boundaries (faithful to the SNES's non-contiguous SRAM
     windows; plain per-window malloc gave adjacent blocks that merged). */
  int nwin = measure_total_kb / measure_win_kb, i;
  size_t win = (size_t)measure_win_kb * 1024, gap = 64;
  char *pool = (char *)malloc((win + gap) * (size_t)nwin);
  if (!pool) { fprintf(stderr, "measure: pool malloc failed\n"); return; }
  for (i = 0; i < nwin; i++)
    p8heap_add_region(pool + (size_t)i * (win + gap), (unsigned long)win);
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
    else if (!strcmp(argv[i], "-H")) hash_mode = 1;
    else if (!strcmp(argv[i], "-A")) audio_trace = 1;
    else if (!strcmp(argv[i], "-M") && i + 2 < argc) {
      measure_win_kb = atoi(argv[++i]);
      measure_total_kb = atoi(argv[++i]);
    }
    else cart = argv[i];
  }
  if (!cart) {
    fprintf(stderr, "usage: runp8 cart.p8 [-n frames] [-i inputs.txt] [-p out.ppm]"
                    " [-M windowKB totalKB]\n");
    return 1;
  }

  p8_reset();
  src = load_cart(cart);

  if (measure_win_kb) {
    build_measure_heap();
    L = lua_newstate(p8heap_lua_alloc, NULL);
    if (!L) { fprintf(stderr, "measure: newstate OOM\n"); return 4; }
  } else
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
  p8_set_audio_cmd(audio_cmd_trace);
  p8_set_audio_note(audio_note_trace);

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
    int is60 = has_global(L, "_update60");
    const char *upd = is60 ? "_update60" : "_update";
    for (i = 0; i < nframes; i++) {
      unsigned mask = 0;
      if (inf) {
        char ln[16];
        if (fgets(ln, sizeof ln, inf))
          mask = (unsigned)strtoul(ln, NULL, 16);
      }
      p8_set_buttons((uint16_t)mask, 0);
      p8_input_frame();
      call_global(L, upd);
      call_global(L, "_draw");
      p8_audio_tick(is60 ? 60 : 30);  /* advance the audio model one frame */
      lua_getglobal(L, "flip");   /* implicit flip after _draw */
      lua_call(L, 0, 0);
    }
  }
  if (inf)
    fclose(inf);
  if (ppm)
    dump_ppm(ppm);
  {
    unsigned f = (unsigned)p8_frame_count();
    printf("DONE frames=%u vmops=%lu ops/frame=%lu\n", f, p8_vmops,
           f ? p8_vmops / f : 0);
  }
  if (measure_win_kb) {
    /* report BEFORE lua_close so peak reflects live state */
    printf("HEAP win=%dKB total=%dKB peak_used=%lu free=%lu"
           " max_alloc=%lu max_fail=%lu nfail=%lu\n",
           measure_win_kb, measure_total_kb, p8heap_peak_used,
           p8heap_free_bytes(), p8heap_max_ok, p8heap_max_fail, p8heap_nfail);
  }
  free(src);
  lua_close(L);
  return 0;
}
