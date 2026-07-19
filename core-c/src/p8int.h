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

/* button state for btnp (held-frame counters, advanced once per frame) */
extern uint16_t p8i_buttons[2];
extern uint8_t p8i_held[2][8];

void p8gfx_open(lua_State *L); /* registers the drawing API + stubs */

#endif
