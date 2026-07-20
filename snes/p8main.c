/* snespico8 SNES entry point (M3).
**
** Boots an embedded .p8 cart on real SNES hardware: the same core-c
** source compiled with Calypsi. PICO-8 machine RAM is WRAM bank $7F
** (1:1, see p8.h). The Lua heap lives in a runtime-probed set of cart
** SRAM windows plus a slice of WRAM $7E. flip() renders the PICO-8
** framebuffer through Mode 7.
**
** WRAM $7E map (see calypsi/linker.scm):
**   $7E:2000-7FFF  Calypsi far .data/.bss/.heap (linker)
**   $7E:8000-BFFF  Mode 7 staging buffer (16 KB, tile-major 8bpp)
**   $7E:C000-EFFF  WRAM Lua-heap region (12 KB)
**   $7E:F000-FFFF  test mailbox (calypsi/mailbox.h)
*/
#include <stdint.h>
#include <string.h>
#include "calypsi/mailbox.h"
#include "p8heap.h"
#include "p8.h"
#include "lua/lua.h"
#include "lua/lauxlib.h"
#include "lua/lualib.h"
#include "cart_hello.h"

extern unsigned long p8_vmops; /* lvm.c instruction counter */

/* --- hardware register access (large data model = 24-bit pointers) --- */
#define REG8(a)  (*(volatile uint8_t *)(unsigned long)(a))
#define REG16(a) (*(volatile uint16_t *)(unsigned long)(a))

#define M7BUF ((uint8_t *)0x7e8000ul)      /* 16 KB tile-major staging */
#define HEAP_WRAM_BASE ((void *)0x7ec000ul)
#define HEAP_WRAM_SIZE 0x3000ul            /* 12 KB */

/* PICO-8 master palette -> BGR555 (mkm7.py formula) */
static const uint32_t PICO8_RGB[16] = {
  0x000000, 0x1D2B53, 0x7E2553, 0x008751, 0xAB5236, 0x5F574F, 0xC2C3C7, 0xFFF1E8,
  0xFF004D, 0xFFA300, 0xFFEC27, 0x00E436, 0x29ADFF, 0x83769C, 0xFF77A8, 0xFFCCAA,
};

static uint16_t bgr555(uint32_t rgb)
{
  uint16_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
  return (uint16_t)(((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3));
}

/* --- DMA a byte stream from WRAM to a single PPU register --- */
static void dma_to_ppu(uint8_t breg, const uint8_t *src, uint16_t size)
{
  unsigned long a = (unsigned long)src;
  REG8(0x4300) = 0x00;                 /* mode 0: 1 reg, A incrementing */
  REG8(0x4301) = breg;                 /* B bus = $2100 + breg */
  REG16(0x4302) = (uint16_t)(a & 0xFFFF);
  REG8(0x4304) = (uint8_t)((a >> 16) & 0xFF);
  REG16(0x4305) = size;                /* 0 means 65536; size <= 16384 here */
  REG8(0x420B) = 0x01;                 /* start channel 0 */
}

/* Letterbox: the 128x128 image is centered (M7HOFS=-64, M7VOFS=-48) and
** M7SEL=$80 makes out-of-plane samples transparent (free top/left border).
** Window 1 clips the right border; this HDMA blanks BG1 below line 176. */
static const uint8_t hdma_tm[] = { 0x7F, 0x01, 49, 0x01, 48, 0x00, 0 };

/* --- Mode 7 one-time setup: mode, matrix, palette, identity tilemap --- */
static void video_init(void)
{
  int i;
  unsigned long ht = (unsigned long)hdma_tm;
  REG8(0x2100) = 0x8F;                  /* force blank */
  REG8(0x2105) = 0x07;                  /* BGMODE 7 */
  REG8(0x211A) = 0x80;                  /* M7SEL: outside plane = transparent */
  /* identity matrix A=D=$0100, B=C=0 (write low then high byte) */
  REG8(0x211B) = 0x00; REG8(0x211B) = 0x01;   /* M7A = $0100 */
  REG8(0x211C) = 0x00; REG8(0x211C) = 0x00;   /* M7B = 0 */
  REG8(0x211D) = 0x00; REG8(0x211D) = 0x00;   /* M7C = 0 */
  REG8(0x211E) = 0x00; REG8(0x211E) = 0x01;   /* M7D = $0100 */
  REG8(0x211F) = 0x00; REG8(0x211F) = 0x00;   /* center X = 0 */
  REG8(0x2120) = 0x00; REG8(0x2120) = 0x00;   /* center Y = 0 */
  REG8(0x210D) = 0xC0; REG8(0x210D) = 0x1F;   /* M7HOFS = -64 */
  REG8(0x210E) = 0xD0; REG8(0x210E) = 0x1F;   /* M7VOFS = -48 */

  /* window 1 clips BG1 to the horizontal image span [64,191] (inverted:
     masks outside) */
  REG8(0x2123) = 0x03;                  /* W12SEL: win1 on BG1, inverted */
  REG8(0x2126) = 64;                    /* WH0 */
  REG8(0x2127) = 191;                   /* WH1 */
  REG8(0x212E) = 0x01;                  /* TMW: window masks BG1 on main */

  /* HDMA ch1 -> $212C (TM): BG1 off below the image */
  REG8(0x4310) = 0x00;                  /* mode 0, 1 reg */
  REG8(0x4311) = 0x2C;                  /* B addr $212C */
  REG16(0x4312) = (uint16_t)(ht & 0xFFFF);
  REG8(0x4314) = (uint8_t)((ht >> 16) & 0xFF);
  REG8(0x420C) = 0x02;                  /* enable HDMA ch1 */

  /* palette -> CGRAM 0..15 */
  REG8(0x2121) = 0x00;
  for (i = 0; i < 16; i++) {
    uint16_t c = bgr555(PICO8_RGB[i]);
    REG8(0x2122) = (uint8_t)(c & 0xFF);
    REG8(0x2122) = (uint8_t)(c >> 8);
  }

  /* build a 16x16 identity tilemap into M7BUF and DMA it to VRAM low
     bytes (Mode 7 tilemap is 128x128 bytes at word 0) */
  {
    int my, mx;
    uint8_t *p = M7BUF;
    for (my = 0; my < 128; my++)
      for (mx = 0; mx < 128; mx++)
        *p++ = (my < 16 && mx < 16) ? (uint8_t)(my * 16 + mx) : 0;
    REG8(0x2115) = 0x00;               /* VMAIN: inc after $2118 (low) */
    REG16(0x2116) = 0x0000;            /* VRAM word addr */
    dma_to_ppu(0x18, M7BUF, 16384);
  }
}

/* expand PICO-8 screen ($7F:6000, 2px/byte) into M7BUF (tile-major 8bpp) */
static void expand_screen(void)
{
  const uint8_t *src = p8_ram + 0x6000;
  uint8_t *dst = M7BUF;
  int ty, tx, r, b;
  for (ty = 0; ty < 16; ty++)
    for (tx = 0; tx < 16; tx++)
      for (r = 0; r < 8; r++) {
        const uint8_t *sr = src + ((ty * 8 + r) * 64) + (tx * 4);
        for (b = 0; b < 4; b++) {
          uint8_t byte = sr[b];
          *dst++ = (uint8_t)(byte & 0x0F);        /* left pixel  */
          *dst++ = (uint8_t)((byte >> 4) & 0x0F); /* right pixel */
        }
      }
}

/* flip hook: render the framebuffer and publish telemetry */
static void snes_flip(void *ud)
{
  (void)ud;
  expand_screen();
  REG8(0x2100) = 0x8F;                  /* force blank for the VRAM DMA */
  REG8(0x2115) = 0x80;                  /* VMAIN: inc after $2119 (high) */
  REG16(0x2116) = 0x0000;
  dma_to_ppu(0x19, M7BUF, 16384);       /* pixel data -> Mode 7 tile data */
  REG8(0x2100) = 0x0F;                  /* screen on, full brightness */
  /* TM ($212C) is driven per-scanline by the HDMA set up in video_init */
  MB_FLIPS = (uint16_t)p8_frame_count();
  MB_VMOPS = p8_vmops;
}

/* --- SRAM probe: add distinct HiROM 8 KB windows to the heap --- */
static unsigned long probe_sram(void)
{
  unsigned bank;
  unsigned long added = 0;
  for (bank = 0x20; bank <= 0x3F; bank++)
    REG16(((unsigned long)bank << 16) | 0x6000) = (uint16_t)(0xC000u | bank);
  for (bank = 0x20; bank <= 0x3F; bank++) {
    unsigned long w = ((unsigned long)bank << 16) | 0x6000;
    if (REG16(w) == (uint16_t)(0xC000u | bank)) {
      p8heap_add_region((void *)w, 0x2000);  /* 8 KB window */
      added += 0x2000;
    }
  }
  return added;
}

/* --- Lua panic -> mailbox --- */
static int on_panic(lua_State *L)
{
  mb_puts("PANIC ");
  mb_puts(lua_tostring(L, -1));
  mb_putc('\n');
  mb_finish(MB_STATUS_PANIC);
  return 0;
}

static void run_global(lua_State *L, const char *name)
{
  lua_getglobal(L, name);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return;
  }
  if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
    mb_puts("ERR ");
    mb_puts(name);
    mb_puts(": ");
    mb_puts(lua_tostring(L, -1));
    mb_putc('\n');
    mb_finish(MB_STATUS_PANIC);
  }
}

int main(void)
{
  lua_State *L;
  unsigned long sram;
  int frame;

  mb_init();
  mb_puts("boot\n");
  video_init();

  /* heap: WRAM slice + probed SRAM windows */
  p8heap_add_region(HEAP_WRAM_BASE, HEAP_WRAM_SIZE);
  sram = probe_sram();
  mb_puts("sram ");
  mb_puthex32(sram);
  mb_puts(" heap ");
  mb_puthex32(p8heap_free_bytes());
  mb_putc('\n');

  /* PICO-8 RAM + cart assets */
  p8_reset();
  if (cart_gfx_len) memcpy(p8_ram + 0x0000, cart_gfx, cart_gfx_len);
  if (cart_map_len) memcpy(p8_ram + 0x2000, cart_map, cart_map_len);
  if (cart_gff_len) memcpy(p8_ram + 0x3000, cart_gff, cart_gff_len);

  L = lua_newstate(p8heap_lua_alloc, NULL);
  if (!L) {
    mb_puts("newstate FAIL\n");
    mb_finish(MB_STATUS_PANIC);
  }
  lua_atpanic(L, on_panic);
  luaL_requiref(L, "_G", luaopen_base, 1);
  lua_pop(L, 1);
  luaL_requiref(L, "coroutine", luaopen_coroutine, 1);
  lua_pop(L, 1);
  luaL_requiref(L, "pico8", luaopen_pico8, 1);
  lua_pop(L, 1);
  lua_setpico8memory(L, p8_ram);
  p8_open(L);
  p8_set_flip(snes_flip, NULL);
  mb_puts("state ok\n");

  if (luaL_loadbuffer(L, cart_lua, cart_lua_len, "cart") != LUA_OK) {
    mb_puts("load FAIL: ");
    mb_puts(lua_tostring(L, -1));
    mb_putc('\n');
    mb_finish(MB_STATUS_PANIC);
  }
  if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
    mb_puts("chunk FAIL: ");
    mb_puts(lua_tostring(L, -1));
    mb_putc('\n');
    mb_finish(MB_STATUS_PANIC);
  }
  mb_puts("chunk ok\n");

  run_global(L, "_init");
  mb_puts("init ok\n");

  for (frame = 0; frame < 8; frame++) {
    run_global(L, "_update");
    run_global(L, "_draw");
    lua_getglobal(L, "flip");        /* implicit flip after _draw */
    if (!lua_isnil(L, -1))
      lua_call(L, 0, 0);
    else
      lua_pop(L, 1);
  }

  mb_puts("frames ");
  mb_puthex32(p8_frame_count());
  mb_puts(" vmops ");
  mb_puthex32(p8_vmops);
  mb_putc('\n');
  mb_puts("M3 PASS\n");
  mb_finish(MB_STATUS_PASS);
  return 0;
}
