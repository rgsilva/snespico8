/* snespico8 PICO-8 machine: the 64 KB RAM and the API surface.
** Same source compiles on the host (reference core / runp8) and for the
** SNES via Calypsi — keep it C99, no host-isms, no int64 (broken on
** Calypsi, see bugs/), single allocations under 32 KB.
**
** Draw state lives AT the documented PICO-8 addresses inside p8_ram so
** peek/poke see the real thing from day one. */
#ifndef P8_H
#define P8_H

#include <stdint.h>
#include "lua/lua.h"

#define P8_RAM_SIZE 0x10000u

/* memory map (PICO-8 manual / wiki) */
#define P8_GFX        0x0000u  /* sprite sheet, 8 KB */
#define P8_MAP        0x2000u  /* map rows 0-31, 4 KB (32-63 shared w/ gfx) */
#define P8_GFLAGS     0x3000u  /* sprite flags, 256 B */
#define P8_SONG       0x3100u
#define P8_SFX        0x3200u
#define P8_USER       0x4300u
#define P8_PERSIST    0x5e00u
#define P8_DRAWSTATE  0x5f00u
#define P8_HWSTATE    0x5f40u
#define P8_GPIO       0x5f80u
#define P8_SCREEN     0x6000u  /* 128x128 @ 2px/byte, low nibble = left px */
#define P8_EXTRAM     0x8000u  /* extended user RAM (0.2.4+) */

/* draw-state cells */
#define P8_DS_DRAWPAL 0x5f00u  /* [16] bits 0-3 color, bit 4 transparent */
#define P8_DS_SCRNPAL 0x5f10u  /* [16] screen palette */
#define P8_DS_CLIP_X0 0x5f20u
#define P8_DS_CLIP_Y0 0x5f21u
#define P8_DS_CLIP_X1 0x5f22u  /* exclusive */
#define P8_DS_CLIP_Y1 0x5f23u  /* exclusive */
#define P8_DS_PEN     0x5f25u
#define P8_DS_CURS_X  0x5f26u
#define P8_DS_CURS_Y  0x5f27u
#define P8_DS_CAM_X   0x5f28u  /* int16 LE */
#define P8_DS_CAM_Y   0x5f2au  /* int16 LE */
#define P8_HW_RNDLO   0x5f44u  /* PRNG state, 2x u32 LE (lo, hi) */

#ifdef __CALYPSI__
/* the PICO-8 machine RAM is WRAM bank $7F, mapped 1:1 (large data
** model: plain pointers are 24-bit) */
#define p8_ram ((uint8_t *)0x7f0000ul)
#else
extern uint8_t p8_ram[P8_RAM_SIZE];
#endif

/* runner hooks */
typedef void (*p8_flip_fn)(void *ud);
void p8_set_flip(p8_flip_fn fn, void *ud);
void p8_set_buttons(uint16_t p0, uint16_t p1); /* bit i = button i held */
void p8_input_frame(void);      /* advance btnp held counters, once per frame */

void p8_reset(void);            /* clear RAM, default draw state */
void p8_open(lua_State *L);     /* register the API as globals */
uint32_t p8_frame_count(void);  /* frames presented (flip count) */

/* FNV-1a over p8_ram[lo..hi). Identical on host and SNES (runtime index,
** Calypsi-safe) so it can't itself be a source of parity divergence.
** hi <= 0x8000 (16-bit size_t on Calypsi). */
uint32_t p8_hash_region(unsigned lo, unsigned hi);

#endif
