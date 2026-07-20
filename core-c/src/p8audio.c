/* p8audio.c — PICO-8 audio sequencer: host reference model + SPC spec.
**
** The 8 waveforms, per-note effects and BRR synthesis run on the SPC700
** in real time (M6c). This file is the reference implementation of the
** sequencer LOGIC — channel allocation, note advancement, sfx loops and
** music-pattern chaining — that the SPC driver mirrors. It also provides
** stat(16..24) audio state and drives the sfx()/music() COMMAND callback
** that is the host<->SNES parity signal (what the cart requests is
** deterministic and identical on both; the real-time playback position
** diverges, like time() and the raw PRNG — see docs/audio-format.md).
**
** Portable C99, Calypsi-safe: no int64, 16-bit-int-safe, all RAM access
** runtime-indexed (no const-ptr+0x8000 folding). */
#include "p8.h"
#include "p8num.h"
#include "p8int.h"
#include "lua/lauxlib.h"

/* ---- sfx / music memory layout (verified: docs/audio-format.md) ---- */
#define SFX_BYTES 68u                       /* 32 note words + 4 footer */
#define SFX_BASE(n)    (P8_SFX + (unsigned)(n) * SFX_BYTES)
#define SFX_SPEED(n)   p8_ram[SFX_BASE(n) + 65u]
#define SFX_LSTART(n)  p8_ram[SFX_BASE(n) + 66u]
#define SFX_LEND(n)    p8_ram[SFX_BASE(n) + 67u]

/* current note word of sfx n at row (0..31); fields below */
static unsigned sfx_note(int n, int row)
{
  unsigned b = SFX_BASE(n) + (unsigned)row * 2u;
  return (unsigned)p8_ram[b] | ((unsigned)p8_ram[b + 1] << 8);
}
#define NOTE_PITCH(w) ((int)((w) & 0x3f))
#define NOTE_WAVE(w)  ((int)(((w) >> 6) & 7))
#define NOTE_VOL(w)   ((int)(((w) >> 9) & 7))
#define NOTE_EFF(w)   ((int)(((w) >> 12) & 7))

static int spd(int n) { int s = SFX_SPEED(n); return s < 1 ? 1 : s; }

/* ---- per-channel state (mirrors the SPC per-voice sequencer struct) ---- */
typedef struct {
  int sfx;      /* -1 idle, else 0..63 */
  int row;      /* current note row 0..31 */
  int endrow;   /* stop when row reaches this (exclusive); 32 = whole sfx */
  int tick;     /* audio-ticks elapsed in the current row */
  int speed;    /* cached speed (>=1) */
  int lstart;   /* cached loop start row */
  int lend;     /* cached loop end row (0 = no loop) */
  int music;    /* 1 if this channel is owned by the music player */
} chan_t;

static chan_t ch[4];

/* ---- music player ---- */
static int mus_pat;    /* -1 stopped, else current pattern 0..63 */
static int mus_ticks;  /* audio-ticks elapsed in the current pattern */
static int mus_len;    /* length of the current pattern, in audio-ticks */

/* ---- command seam: host prints the trace, SNES mails it to the SPC ---- */
static p8_audio_cmd_fn cmd_fn;
void p8_set_audio_cmd(p8_audio_cmd_fn fn) { cmd_fn = fn; }

/* ---- note-event seam: the note stream the SPC must reproduce (M6c) ---- */
static p8_audio_note_fn note_fn;
void p8_set_audio_note(p8_audio_note_fn fn) { note_fn = fn; }

/* emit channel c's current note (its decoded wave/pitch/vol/eff) to the
** note sink, if one is set. This is exactly what the SPC voice plays. */
static void emit_note(int c)
{
  if (note_fn) {
    unsigned w = sfx_note(ch[c].sfx, ch[c].row);
    note_fn(c, NOTE_WAVE(w), NOTE_PITCH(w), NOTE_VOL(w), NOTE_EFF(w));
  }
}

/* ---------------------------------------------------------------- */

static void chan_stop(int c) { ch[c].sfx = -1; ch[c].music = 0; }

static void chan_start(int c, int n, int offset, int length, int music)
{
  ch[c].sfx = n;
  ch[c].speed = spd(n);
  ch[c].lstart = SFX_LSTART(n);
  ch[c].lend = SFX_LEND(n);
  ch[c].row = offset < 0 ? 0 : (offset > 31 ? 31 : offset);
  ch[c].tick = 0;
  ch[c].endrow = (length >= 0) ? ch[c].row + length : 32;
  if (ch[c].endrow > 32) ch[c].endrow = 32;
  ch[c].music = music;
  emit_note(c);
}

/* pick a channel for an auto (channel==-1) sfx: first idle, else 0.
** VERIFY: PICO-8's real priority replaces the oldest same-or-lower; this
** first-idle rule is close enough for the common cases and ear-tuned. */
static int chan_alloc(void)
{
  int c;
  for (c = 0; c < 4; c++)
    if (ch[c].sfx < 0) return c;
  return 0;
}

/* ---- music-pattern control ---- */

static void music_stop(void)
{
  int c;
  for (c = 0; c < 4; c++)
    if (ch[c].music) chan_stop(c);
  mus_pat = -1;
}

/* the pattern's length in audio-ticks: driven by the non-looping channels
** (they must all finish); if every active channel loops, fall back to the
** full 32-row length of the lowest active channel. VERIFY (ear-tune). */
static void music_begin(int pat)
{
  unsigned mem = P8_SONG + (unsigned)pat * 4u;
  int c, len = 0, fallback = 0;
  for (c = 0; c < 4; c++) {
    int b = p8_ram[mem + (unsigned)c];
    if (b & 0x40) {           /* channel disabled in this pattern */
      if (ch[c].music) chan_stop(c);
      continue;
    }
    {
      int n = b & 0x3f;
      chan_start(c, n, 0, -1, 1);
      if (!fallback) fallback = 32 * spd(n);
      if (SFX_LSTART(n) == 0 && SFX_LEND(n) == 0) {
        int dur = 32 * spd(n);
        if (dur > len) len = dur;
      }
    }
  }
  mus_len = len > 0 ? len : fallback;
  if (mus_len < 1) mus_len = 1;
  mus_ticks = 0;
  mus_pat = pat;
}

/* on a loop-end flag, jump back to the most recent loop-start pattern */
static int music_loop_target(int from)
{
  int p;
  for (p = from; p >= 0; p--)
    if (p8_ram[P8_SONG + (unsigned)p * 4u] & 0x80) return p;
  return 0;
}

/* ---- one 120.5 Hz audio-tick (the unit the SPC timer reproduces) ---- */

static void audio_advance_one(void)
{
  int c;

  if (mus_pat >= 0) {
    if (++mus_ticks >= mus_len) {
      unsigned mem = P8_SONG + (unsigned)mus_pat * 4u;
      if (p8_ram[mem + 2u] & 0x80) {            /* byte2 bit7 = stop */
        music_stop();
      } else if (p8_ram[mem + 1u] & 0x80) {     /* byte1 bit7 = loop-end */
        music_begin(music_loop_target(mus_pat));
      } else if (mus_pat + 1 <= 63) {
        music_begin(mus_pat + 1);
      } else {
        music_stop();
      }
    }
  }

  for (c = 0; c < 4; c++) {
    if (ch[c].sfx < 0) continue;
    if (++ch[c].tick >= ch[c].speed) {
      ch[c].tick = 0;
      ch[c].row++;
      if (ch[c].lend > 0 && ch[c].row >= ch[c].lend) {
        ch[c].row = ch[c].lstart;             /* sfx loop */
        emit_note(c);
      } else if (ch[c].row >= ch[c].endrow) {
        chan_stop(c);                          /* sfx (or its slice) done */
      } else {
        emit_note(c);                          /* next note in the sfx */
      }
    }
  }
}

/* advance the host model by one game frame. fps picks the audio-ticks/frame
** (120.5 Hz): ~2 at 60 fps, ~4 at 30 fps. Fixed-point accumulator in
** 1/256 units keeps it from drifting. SNES does NOT call this — the SPC
** sequences in real wall-clock time. */
void p8_audio_tick(int fps)
{
  static int accum;
  int inc = (fps >= 60) ? 514 : 1028;   /* round(120.5/fps * 256) */
  int n;
  accum += inc;
  n = accum >> 8;
  accum &= 0xff;
  while (n-- > 0)
    audio_advance_one();
}

void p8_audio_reset(void)
{
  int c;
  for (c = 0; c < 4; c++)
    chan_stop(c);
  mus_pat = -1;
  mus_ticks = 0;
  mus_len = 1;
}

/* stat(16..24): sfx/note per channel, current music pattern. Returns the
** value for audio stats, or a large sentinel for non-audio stats so the
** caller falls through to its own handling. */
int p8_audio_stat(int n)
{
  if (n >= 16 && n <= 19) return ch[n - 16].sfx;             /* sfx idx / -1 */
  if (n >= 20 && n <= 23) return ch[n - 20].sfx < 0 ? -1 : ch[n - 20].row;
  if (n == 24) return mus_pat;                               /* pattern / -1 */
  return P8_STAT_NONE;
}

/* ---------------------------- Lua API ---------------------------- */

/* sfx(n, [channel], [offset], [length])
**   n:       0..63 play; -1 stop; -2 release loop (finish current pass)
**   channel: -1 auto (default), 0..3 explicit, -2 = stop n on any channel
**   offset:  start row 0..31 (default 0)
**   length:  rows to play (default: to the sfx end / its loop) */
static int api_sfx(lua_State *L)
{
  int n       = p8a_optint(L, 1, 0);
  int channel = p8a_optint(L, 2, -1);
  int offset  = p8a_optint(L, 3, 0);
  int length  = p8a_optint(L, 4, -1);

  if (cmd_fn) cmd_fn(P8_ACMD_SFX, n, channel, offset, length);

  if (n == -1) {                              /* stop */
    if (channel >= 0 && channel < 4) chan_stop(channel);
    else { int c; for (c = 0; c < 4; c++) if (!ch[c].music) chan_stop(c); }
    return 0;
  }
  if (n == -2) {                              /* release loop */
    int c;
    for (c = 0; c < 4; c++)
      if (ch[c].sfx == n || channel == c) ch[c].lend = 0;
    return 0;
  }
  if (channel == -2) {                        /* stop this sfx anywhere */
    int c; for (c = 0; c < 4; c++) if (ch[c].sfx == n) chan_stop(c);
    return 0;
  }
  if (channel < 0) channel = chan_alloc();
  if (channel > 3) channel = 3;
  chan_start(channel, n & 63, offset, length, 0);
  return 0;
}

/* music(n, [fade_ms], [channel_mask])
**   n: 0..63 start pattern; -1 stop. fade/mask VERIFY (accepted, fade is
**   instant here; the SPC applies the fade envelope in M6c). */
static int api_music(lua_State *L)
{
  int n    = p8a_optint(L, 1, 0);
  int fade = p8a_optint(L, 2, 0);
  int mask = p8a_optint(L, 3, 0);

  if (cmd_fn) cmd_fn(P8_ACMD_MUSIC, n, fade, mask, 0);

  if (n < 0) music_stop();
  else music_begin(n & 63);
  return 0;
}

static const luaL_Reg audio_funcs[] = {
  { "sfx", api_sfx },
  { "music", api_music },
  { NULL, NULL }
};

void p8audio_open(lua_State *L)
{
  const luaL_Reg *r;
  for (r = audio_funcs; r->name; r++)
    lua_register(L, r->name, r->func);
  p8_audio_reset();
}
