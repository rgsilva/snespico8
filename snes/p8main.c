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
#include "p8spc.h"
#include "p8.h"
#include "lua/lua.h"
#include "lua/lauxlib.h"
#include "lua/lualib.h"
#ifdef P8_MULTICART
#include "cart_multi.h"   /* MultiCart carts[], cart_count (tools/multicart2c.py) */
#else
#include "cart_hello.h"
#endif

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
#ifdef P8_PARITY
  /* M4 parity: same two-region hash runp8 emits with -H. Gated because
     the 32 KB scan is slow at -O0 and would push the plain demo/boot ROM
     past Mesen's script time limit. */
  mb_puts("HASH ");
  mb_puthex32(p8_hash_region(0x0000u, 0x6000u));
  mb_putc(' ');
  mb_puthex32(p8_hash_region(0x6000u, 0x8000u));
  mb_putc('\n');
#endif
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

/* M6d: route the core's sfx()/music() to the SPC sequencer. Args are the
** raw PICO-8 call arguments (p8.h P8_ACMD_*). The SPC then sequences in
** real time. Auto-channel (channel < 0) maps to channel 3 -- carts
** usually reserve 0-2 for music; VERIFY per-cart. */
static void snes_audio_cmd(int kind, int a, int b, int c, int d)
{
  (void)c; (void)d;
  if (kind == P8_ACMD_SFX) {
    if (a >= 0)
      p8spc_play_sfx((uint8_t)(a & 63), (uint8_t)(b >= 0 && b < 4 ? b : 3));
    else if (b >= 0 && b < 4)
      p8spc_kof((uint8_t)b);       /* sfx(-1, channel): stop that channel */
    else
      p8spc_alloff();              /* sfx(-1): stop everything */
  } else {                          /* P8_ACMD_MUSIC */
    if (a >= 0)
      p8spc_play_music((uint8_t)(a & 63));
    else
      p8spc_stop_music();           /* music(-1): stop the song */
  }
}

/* --- live controller: SNES auto-joypad ($4218 = joy1, $421A = joy2) into a
** PICO-8 button mask. P8 bits: 0=Left 1=Right 2=Up 3=Down 4=O 5=X. Either
** face-button column works (O = SNES B or Y, X = SNES A or X) so the mapping
** feels natural whichever pair a cart treats as primary. Auto-joypad ($4200
** bit 0) makes the PPU relatch the pad every vblank, so a single read per
** (slow) game frame reflects the current state -- no manual $4016 strobe. */
static uint16_t read_pad(unsigned long joy)
{
  uint16_t w = REG16(joy);
  uint16_t m = 0;
  if (w & 0x0200u) m |= 1u << 0;   /* Left  (bit 9)  */
  if (w & 0x0100u) m |= 1u << 1;   /* Right (bit 8)  */
  if (w & 0x0800u) m |= 1u << 2;   /* Up    (bit 11) */
  if (w & 0x0400u) m |= 1u << 3;   /* Down  (bit 10) */
  if (w & 0xC000u) m |= 1u << 4;   /* O = B (15) or Y (14) */
  if (w & 0x00C0u) m |= 1u << 5;   /* X = A (7)  or X (6)  */
  return m;
}

/* Configure a fresh lua_State exactly like runp8: base + coroutine + pico8
** libs, PICO-8 RAM bound, coroutine aliases under global names, draw API
** registered. Shared by the single-cart boot and the menu's per-cart launch
** so the only host<->SNES difference stays the compiler. */
static void open_pico8_libs(lua_State *L)
{
  luaL_requiref(L, "_G", luaopen_base, 1);
  lua_pop(L, 1);
  luaL_requiref(L, "coroutine", luaopen_coroutine, 1);
  lua_pop(L, 1);
  luaL_requiref(L, "pico8", luaopen_pico8, 1);
  lua_pop(L, 1);
  lua_setpico8memory(L, p8_ram);
  /* PICO-8 exposes coroutines under global names (match runp8 exactly) */
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
}

#ifdef P8_MULTICART
/* ---- built-in multi-cart menu (M8) --------------------------------------
** main() uploads the SPC + probes the heap once, then loops: run_menu()
** draws the cart list and returns the pad's pick; run_cart() boots it in a
** fresh lua_State and, on Select, tears it down. lua_close frees the whole
** Lua heap back (the p8heap free list coalesces) so the next cart reuses it
** -- return-to-menu works with no reset. */

/* raw auto-joypad bits for menu control (Select/Start aren't PICO-8 buttons
** so they're read here directly, not through read_pad's 6-button map) */
#define PAD_UP     0x0800u
#define PAD_DOWN   0x0400u
#define PAD_A      0x0080u
#define PAD_B      0x8000u
#define PAD_START  0x1000u
#define PAD_SELECT 0x2000u

static uint16_t pad_raw(void) { return REG16(0x4218); }

static void wait_release(uint16_t bits)
{
  while (pad_raw() & bits) { }     /* spin until those buttons are let go */
}

/* like run_global but returns -1 instead of killing the ROM, so a crashing
** cart just drops back to the menu */
static int menu_run_global(lua_State *L, const char *name)
{
  lua_getglobal(L, name);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
    mb_puts("cart err ");
    mb_puts(name);
    mb_puts(": ");
    mb_puts(lua_tostring(L, -1));
    mb_putc('\n');
    return -1;
  }
  return 0;
}

static void draw_menu(int sel)
{
  int i;
  memset(p8_ram + 0x6000, 0x11, 0x2000);   /* clear screen to color 1 */
  p8_text(30, 6, 7, "snespico8");
  p8_text(34, 16, 6, "pick a cart");
  for (i = 0; i < cart_count; i++) {
    int y = 32 + i * 9;
    if (i == sel)
      p8_text(14, y, 10, ">");
    p8_text(22, y, (unsigned char)(i == sel ? 7 : 5), carts[i].name);
  }
  p8_text(8, 120, 13, "up/dn move   o play");
}

static int run_menu(void)
{
  static int sel = 0;              /* remembered across menu visits */
  uint16_t prev = pad_raw();       /* seed with held buttons so releasing the
                                      launch/return press isn't a fresh hit */
  int dirty = 1;
  if (sel >= cart_count)
    sel = 0;
  for (;;) {
    uint16_t cur = pad_raw();
    uint16_t hit = (uint16_t)(cur & ~prev);
    prev = cur;
    if ((hit & PAD_UP) && sel > 0) {
      sel--;
      dirty = 1;
    }
    if ((hit & PAD_DOWN) && sel < cart_count - 1) {
      sel++;
      dirty = 1;
    }
    if (hit & (PAD_A | PAD_B | PAD_START)) {
      wait_release(PAD_A | PAD_B | PAD_START);
      return sel;
    }
    if (dirty) {                    /* redraw only on change: polling stays snappy.
                                    ** NB: the ~10-frame flip briefly blocks input
                                    ** sampling, so a press+release entirely within
                                    ** it is missed -- fine for a human, who holds
                                    ** the button far longer than 156 ms. */
      draw_menu(sel);
      snes_flip(NULL);
      dirty = 0;
    }
  }
}

static void run_cart(const MultiCart *mc)
{
  lua_State *L;
  int ok;

  p8_reset();                       /* clears PICO-8 RAM + resets audio model */
  if (mc->gfx_len)   memcpy(p8_ram + 0x0000, mc->gfx, mc->gfx_len);
  if (mc->map_len)   memcpy(p8_ram + 0x2000, mc->map, mc->map_len);
  if (mc->gff_len)   memcpy(p8_ram + 0x3000, mc->gff, mc->gff_len);
  if (mc->music_len) memcpy(p8_ram + 0x3100, mc->music, mc->music_len);
  if (mc->sfx_len)   memcpy(p8_ram + 0x3200, mc->sfx, mc->sfx_len);

  L = lua_newstate(p8heap_lua_alloc, NULL);
  if (!L) {
    mb_puts("newstate FAIL\n");
    return;                         /* back to the menu */
  }
  lua_atpanic(L, on_panic);
  open_pico8_libs(L);
  p8_set_flip(snes_flip, NULL);
  p8spc_load_audio();               /* stream THIS cart's sfx/music to the SPC */
  p8_set_audio_cmd(snes_audio_cmd);

  ok = (luaL_loadbuffer(L, mc->lua, mc->lua_len, "cart") == LUA_OK) &&
       (lua_pcall(L, 0, 0, 0) == LUA_OK);
  if (!ok) {
    mb_puts("cart load err: ");
    mb_puts(lua_tostring(L, -1));
    mb_putc('\n');
  } else if (menu_run_global(L, "_init") == 0) {
    long frame;
    const char *upd;
    lua_getglobal(L, "_update60");
    upd = lua_isnil(L, -1) ? "_update" : "_update60";
    lua_pop(L, 1);
    for (frame = 0; frame < mc->frames; frame++) {
      if (pad_raw() & PAD_SELECT)   /* hold Select -> back to the menu */
        break;
      p8_set_buttons(read_pad(0x4218ul), read_pad(0x421Aul));
      p8_input_frame();
      if (menu_run_global(L, upd) < 0)
        break;
      if (menu_run_global(L, "_draw") < 0)
        break;
      lua_getglobal(L, "flip");     /* implicit flip after _draw */
      if (!lua_isnil(L, -1))
        lua_call(L, 0, 0);
      else
        lua_pop(L, 1);
    }
  }

  p8spc_alloff();                   /* silence before returning to the menu */
  p8spc_stop_music();
  lua_close(L);                     /* frees the whole Lua heap back */
  /* a still-held Select is harmless: run_menu re-seeds prev on entry, so it
  ** is never seen as a fresh menu press (and Select isn't a PICO-8 button). */
}
#endif /* P8_MULTICART */

int main(void)
{
  lua_State *L;
  unsigned long sram;
  long frame;                 /* long: cart_frames can exceed the 16-bit int range */

  mb_init();
  mb_puts("boot\n");
  video_init();
  REG8(0x4200) = 0x01;   /* auto-joypad read on (NMI/IRQ off): the PPU
                            latches both pads every vblank for read_pad() */

#ifdef P8_SPC_TEST
  /* M6a/b: IPL-upload the driver, then key four voices with four PICO-8
   * waveforms at four known pitches. The harness reads the voice mask at
   * SPC $00 (upload + transport OK) and the programmed DSP pitch at
   * $30+voice*2 (in tune: matches pitch_tab). Then hold so it can sample. */
  p8spc_boot();
  mb_puts("spc up\n");
  p8spc_note(24, 0, 0, 6);    /* pitch 24 (C4)  triangle  voice 0 */
  p8spc_note(36, 3, 1, 6);    /* pitch 36 (C5)  square    voice 1 */
  p8spc_note(45, 2, 2, 6);    /* pitch 45 (A5)  saw       voice 2 */
  p8spc_note(52, 5, 3, 6);    /* pitch 52 (E6)  organ     voice 3 */
  mb_puts("notes x4\n");
  for (;;) { }                /* SPC keeps ringing; harness samples + stops */
#endif

  /* heap: WRAM slice + probed SRAM windows */
  p8heap_add_region(HEAP_WRAM_BASE, HEAP_WRAM_SIZE);
  sram = probe_sram();
  mb_puts("sram ");
  mb_puthex32(sram);
  mb_puts(" heap ");
  mb_puthex32(p8heap_free_bytes());
  mb_putc('\n');

#ifdef P8_MULTICART
  /* M8: the built-in cart menu. Upload the SPC driver once, then loop
  ** forever: draw the list, pick a cart, run it, come back on Select. */
  (void)L; (void)frame;
  p8spc_boot();
  mb_puts("menu ready\n");
  for (;;) {
    int pick = run_menu();
    mb_puts("launch ");
    mb_puthex32((unsigned long)pick);
    mb_putc('\n');
    run_cart(&carts[pick]);
    mb_puts("to menu\n");
  }
#else
  /* PICO-8 RAM + cart assets */
  p8_reset();
  if (cart_gfx_len) memcpy(p8_ram + 0x0000, cart_gfx, cart_gfx_len);
  if (cart_map_len) memcpy(p8_ram + 0x2000, cart_map, cart_map_len);
  if (cart_gff_len) memcpy(p8_ram + 0x3000, cart_gff, cart_gff_len);
  if (cart_music_len) memcpy(p8_ram + 0x3100, cart_music, cart_music_len);
  if (cart_sfx_len) memcpy(p8_ram + 0x3200, cart_sfx, cart_sfx_len);

#ifdef P8_SPC_SEQ
  /* M6c: upload the driver, stream the cart audio into the SPC, and let it
   * sequence sfx 0 on channel 0 autonomously in real time. The harness
   * checks the timer heartbeat, the streamed data, and the channel's row
   * advancing on its own. Needs a cart with sfx (CART=m6_audio). */
  p8spc_boot();
  mb_puts("spc up\n");
  p8spc_load_audio();
  mb_puts("audio loaded\n");
  p8spc_play_music(0);          /* start the song; the SPC chains patterns */
  mb_puts("play music0\n");
  for (;;) { }
#endif

  L = lua_newstate(p8heap_lua_alloc, NULL);
  if (!L) {
    mb_puts("newstate FAIL\n");
    mb_finish(MB_STATUS_PANIC);
  }
  lua_atpanic(L, on_panic);
  open_pico8_libs(L);
  p8_set_flip(snes_flip, NULL);
  /* M6d: bring up the SPC audio driver, stream the cart's sfx/music into
   * it, and route sfx()/music() there. The SPC then plays in real time
   * while the game loop crawls. Registered before _init so a cart that
   * starts music in _init is heard. */
  p8spc_boot();
  p8spc_load_audio();
  p8_set_audio_cmd(snes_audio_cmd);
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

  /* pick _update60 vs _update once, like runp8 */
  lua_getglobal(L, "_update60");
  {
    const char *upd = lua_isnil(L, -1) ? "_update" : "_update60";
    lua_pop(L, 1);
    for (frame = 0; frame < cart_frames; frame++) {
      /* A baked cart_input trace (from cart2c) keeps the parity/input tests
      ** deterministic; a normal cart ships none -> read the live pad. Carts
      ** without a trace also don't move the hashed RAM off input, so parity
      ** is unaffected (an idle pad reads 0, exactly like the old path). */
      uint16_t mask, mask2;
      if (cart_input_len) {
        mask = (frame < (int)cart_input_len) ? (uint16_t)cart_input[frame] : 0;
        mask2 = 0;
      } else {
        mask = read_pad(0x4218ul);
        mask2 = read_pad(0x421Aul);
      }
      p8_set_buttons(mask, mask2);
      p8_input_frame();
      run_global(L, upd);
      run_global(L, "_draw");
      lua_getglobal(L, "flip");      /* implicit flip after _draw */
      if (!lua_isnil(L, -1))
        lua_call(L, 0, 0);
      else
        lua_pop(L, 1);
    }
  }

  mb_puts("frames ");
  mb_puthex32(p8_frame_count());
  mb_puts(" vmops ");
  mb_puthex32(p8_vmops);
  mb_putc('\n');
  mb_puts("M3 PASS\n");
  mb_finish(MB_STATUS_PASS);
#endif /* P8_MULTICART */
  return 0;
}
