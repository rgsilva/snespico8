# PICO-8 audio format & timing (M6) — verified against real PICO-8

All of the below was confirmed by loading a hand-crafted cart in real
PICO-8 (`pico8 -x`) and reading back memory with `peek`, except timing
(headless PICO-8 does not run the audio mixer, so stat(16)=sfx-allocated
works but stat(20)=note-position stays frozen — timing uses the
zepto8/fake-08 reference constant and is ear-validated in Mesen).

## SFX memory — 0x3200, 64 sfx × 68 bytes

Per sfx: 32 note words (2 bytes LE each, bytes 0..63) + 4-byte footer.

Note word (16 bits):
    bits 0-5   pitch     0..63  (semitones, 0 = C0)
    bits 6-8   waveform  0..7   (tri, tilted-saw, saw, sqr, pulse, organ, noise, phaser)
    bits 9-11  volume    0..7
    bits 12-14 effect    0..7   (none, slide, vibrato, drop, fadein, fadeout, arp-fast, arp-slow)
    bit  15    (custom-instrument flag; deferred)

Footer (bytes 64..67): mode, speed, loop_start, loop_end.
  VERIFIED: note{pitch1,wave2,vol3,eff4} -> word 0x4681 (0x3200=0x81,0x3201=0x46);
            footer speed=16 at 0x3241.

## Music memory — 0x3100, 64 patterns × 4 bytes

Four channel bytes:
    bits 0-5  sfx index 0..63
    bit  6    channel disabled (not played in this pattern)
    bit  7    loop/stop flag, distributed: byte0=loop-start, byte1=loop-end, byte2=stop
  VERIFIED: text "01 01424344" -> mem 0x81,0x42,0x43,0x44 (loop-start bit7 on ch0).

## .p8 text encoding

__sfx__ line = 168 hex chars: mode(2) speed(2) loopstart(2) loopend(2)
               + 32 notes × 5 chars (pitch=2, wave=1, vol=1, eff=1).
__music__ line = "FF CCCCCCCC": flags byte (2 hex) + space + 4 channel bytes.
               flags bit0=loop-start, bit1=loop-end, bit2=stop -> bit7 of
               memory bytes 0/1/2 respectively.

## Timing (reference constant, ear-validated)

Note duration = `speed` audio-ticks; audio-tick rate = 22050/183 ≈ 120.5 Hz
(one tick = 183 samples @ 22050). So note_seconds = speed × 183 / 22050;
speed 16 ≈ 0.133 s ≈ 8 frames @ 60 fps. speed 0 treated as 1.

Design decision (locked, PROJECT.md): the SPC sequences in REAL wall-clock
time (its own timer at ~120.5 Hz) while the 65816 game loop crawls. So
stat() audio position on the SNES reflects real time, diverging from the
frame-locked host model — same accepted divergence as time()/the raw PRNG.
Parity is the COMMAND/event trace (which sfx/pattern the core triggers),
not sample- or frame-exact position.
