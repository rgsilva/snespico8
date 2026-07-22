/* Internal interfaces shared by p8.c (machine, misc API) and p8gfx.c
** (drawing API). Not for the runner. */
#ifndef P8INT_H
#define P8INT_H

#include "p8.h"
#include "lua/lua.h"

/* argument helpers (fix32-floored ints) */
int p8a_int(lua_State *L, int idx);
int p8a_optint(lua_State *L, int idx, int def);

/* camera state (from RAM) */
int p8i_cam_x(void);
int p8i_cam_y(void);

/* raw screen access, x/y must be 0..127 */
void p8i_screen_set(int x, int y, uint8_t c);
uint8_t p8i_screen_get(int x, int y);

/* shape pixel: world coords; camera + clip + fill pattern + draw
** palette. col is the full pen value (secondary color in the high
** nibble for fillp). */
void p8i_shape_plot(int x, int y, int col);

/* sprite pixel: world coords; camera + clip + draw palette with
** transparency (bit 4 of the draw palette entry skips the pixel). */
void p8i_sprite_plot(int x, int y, uint8_t c);

/* sprite sheet pixel (0..127 each; reads gfx RAM at 0x0000) */
uint8_t p8i_sheet_get(int x, int y);
void p8i_sheet_set(int x, int y, uint8_t c);

/* M9 T3: per-shape draw-context snapshot. Shape/sprite walkers load the
** draw state ONCE per API call and plot through it -- semantically
** identical to per-pixel re-reads (plotting only writes screen bytes,
** never draw state) while removing ~10 far-RAM reads and three far
** calls per pixel. p8i_ctx_pen resolves the pen's two fillp nibbles
** through the draw palette up front (every walker draws one col per
** call); p8i_ctx_sprite reads the snapshot palette per sheet pixel. */
typedef struct {
  int cam_x, cam_y;
  int clip_x0, clip_y0, clip_x1, clip_y1;
  unsigned int pat;    /* fillp bits (0 = solid) */
  uint8_t pat_transp;  /* 0x5f33 bit 0: secondary nibble transparent */
  uint8_t v_lo, v_hi;  /* p8i_ctx_pen: palette-resolved pen nibbles */
  uint8_t drawpal[16]; /* snapshot for ctx_sprite / ctx_pen / ctx_glyph */
} P8Ctx;
void p8i_ctx_load(P8Ctx *k);
void p8i_ctx_pen(P8Ctx *k, int col);
void p8i_ctx_plot(const P8Ctx *k, int x, int y);
void p8i_ctx_sprite(const P8Ctx *k, int x, int y, uint8_t c);
void p8i_ctx_glyph(const P8Ctx *k, int x, int y, uint8_t c);

/* button state for btnp (held-frame counters, advanced once per frame) */
extern uint16_t p8i_buttons[2];
extern uint8_t p8i_held[2][8];

void p8gfx_open(lua_State *L); /* registers the drawing API + stubs */

/* audio (p8audio.c) */
void p8audio_open(lua_State *L); /* registers sfx, music; resets state */
void p8_audio_reset(void);
int  p8_audio_stat(int n);       /* stat(16..24) or P8_STAT_NONE */

#endif
