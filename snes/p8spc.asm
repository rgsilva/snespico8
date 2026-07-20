; snespico8 — SPC700 audio driver (M6). Derived from snes86's 8-voice
; synth: same DSP setup, mailbox handshake and key-on/off, but the note
; command is PICO-8-native (waveform + pitch 0-63) and the 8 PICO-8
; waveforms replace the MIDI instruments. M6c adds the autonomous
; sequencer (ticking SFX/music rows) on top of this note primitive.
;
; Mailbox (65816 -> SPC), written F4/F5/F6 first, F7 last:
;   $F7 = (seq<<4) | op   seq cycles 1-15; SPC acks by echoing to its
;                         own port3 ($F7 out) after processing
;   op 1 = NOTE ON F4=pitch 0-63, F5=(waveform<<4)|voice, F6=volume 0-7
;   op 2 = KOF     F4=voice
;   op 3 = all notes off
;   op 4 = KON raw F4/F5 = DSP P lo/hi (voice 7, waveform 0 — a raw beep)
; dp $00 = keyed-voice bitmask (0 = silent; the test suite reads it).
; $30 + voice*2 = the DSP P last programmed for that voice (test readback).
;
; Waveforms 0-7 (SRCN = waveform): triangle, tilted saw, saw, square,
; pulse, organ, noise (via DSP NON), phaser. See tools/mkp8snd.py.

.MEMORYMAP
DEFAULTSLOT 0
SLOT 0 $0000 SIZE $10000
.ENDME

.ROMBANKMAP
BANKSTOTAL 1
BANKSIZE $10000
BANKS 1
.ENDRO

.BANK 0 SLOT 0
.ORG $0200
.SECTION "Midi" FORCE

; ---- sample directory: 8 PICO-8 waveforms (start, loop) --------------
; SRCN = PICO-8 waveform number 0..7; each loops its whole single cycle.
; 8 entries * 4 bytes = 32 bytes -> entry: is at $0220 (see spc2c.py).
sdir:
    .dw wav_tri,    wav_tri      ; 0 triangle
    .dw wav_tsaw,   wav_tsaw     ; 1 tilted saw
    .dw wav_saw,    wav_saw      ; 2 saw
    .dw wav_sqr,    wav_sqr      ; 3 square
    .dw wav_pulse,  wav_pulse    ; 4 pulse
    .dw wav_organ,  wav_organ    ; 5 organ
    .dw wav_noise,  wav_noise    ; 6 noise (voice uses DSP NON)
    .dw wav_phaser, wav_phaser   ; 7 phaser (VERIFY, ear-tuned)

; dp usage: $00 keyed mask, $01 last F7, $02 cmd, $03 voice, $04 note,
;           $05 inst, $06 vel, $07 NON shadow, $08/$09 scratch
entry:
    mov x, #$EF
    mov sp, x
    mov a, #$00
    mov $00, a
    mov $01, a
    mov $07, a
    mov $0A, a                   ; KOF shadow (per-voice pending key-offs)
    mov $F2, #$6C
    mov $F3, #$20                ; FLG: echo off, noise clock 0 for now
    mov $F2, #$0C
    mov $F3, #$7F                ; master vol L
    mov $F2, #$1C
    mov $F3, #$7F                ; master vol R
    mov $F2, #$2C
    mov $F3, #$00
    mov $F2, #$3C
    mov $F3, #$00                ; echo vol off
    mov $F2, #$5D
    mov $F3, #$02                ; DIR = $0200
    mov $F2, #$2D
    mov $F3, #$00                ; PMON off
    mov $F2, #$3D
    mov $F3, #$00                ; NON off
    mov $F2, #$4D
    mov $F3, #$00                ; EON off
    mov $F2, #$5C
    mov $F3, #$00                ; clear KOF
    mov $F2, #$6C
    mov $F3, #$2C                ; FLG: lower noise clock = duller, less
                                 ; crashy drum (echo-off bit5 kept)
hold:
    mov a, $F7                   ; wait out stale handshake values
    bne hold

loop:
    mov a, $F7
    cmp a, $01
    beq loop                     ; unchanged
    mov $01, a
    mov y, a
    and a, #$0F                  ; op
    cmp a, #$01
    bne _d_n1
    jmp !do_kon                  ; (do_kon grew past 8-bit branch range)
_d_n1:
    cmp a, #$02
    beq do_kof
    cmp a, #$03
    beq do_alloff
    cmp a, #$04
    beq do_raw
ack:
    mov a, $01
    mov $F7, a                   ; echo = ack
    bra loop

do_kof:
    mov a, $F4
    and a, #$07
    mov $03, a
    mov x, a
    mov a, !bitmask+x
    mov $08, a
    or a, $0A                    ; ADD to the KOF shadow — writing just this
    mov $0A, a                   ; voice's bit would clear the others' pending
    mov $F2, #$5C                ; key-offs and un-release fading voices
    mov $F3, a
    mov a, $08
    eor a, #$FF
    and a, $00
    mov $00, a
    bra ack

do_alloff:
    mov a, #$FF
    mov $0A, a                   ; shadow: everything off
    mov $F2, #$5C
    mov $F3, a
    mov a, #$00
    mov $00, a
    bra ack

do_raw:                          ; beep: voice 7, square, raw pitch
    mov $F2, #$74                ; v7 SRCN
    mov $F3, #$00
    mov $F2, #$75
    mov $F3, #$00                ; ADSR off -> GAIN
    mov $F2, #$77
    mov $F3, #$7F                ; GAIN direct max
    mov $F2, #$70
    mov $F3, #$50                ; v7 vol L
    mov $F2, #$71
    mov $F3, #$50
    mov a, $F4
    mov $F2, #$72
    mov $F3, a                   ; P lo
    mov a, $F5
    mov $F2, #$73
    mov $F3, a                   ; P hi
    mov a, $00
    and a, #$80
    bne _raw_on                  ; already keyed: pitch update only
    mov a, $0A
    and a, #$7F                  ; clear only voice 7's KOF shadow bit
    mov $0A, a
    mov $F2, #$5C
    mov $F3, a
    mov $F2, #$4C
    mov $F3, #$80                ; KON v7
_raw_on:
    mov a, $00
    or a, #$80
    mov $00, a
    jmp !ack

; op 1 = PICO-8 NOTE ON
;   F4 = pitch    0..63
;   F5 = (waveform<<4) | voice     waveform 0..7, voice 0..7 (PICO-8 uses 0..3)
;   F6 = volume   0..7
; SRCN = waveform (direct); GAIN direct-max (the sequencer sets per-tick
; volume in M6c); NON if waveform 6; pitch from pitch_tab[pitch]. The P we
; program is stashed at $30 + voice*2 so the M6b test can read it back and
; confirm the note is in tune.
do_kon:
    mov a, $F4
    mov $04, a                   ; pitch
    mov a, $F6
    mov $06, a                   ; volume 0..7
    mov a, $F5
    and a, #$07
    mov $03, a                   ; voice
    mov a, $F5
    xcn a
    and a, #$0F
    mov $05, a                   ; waveform 0..7
    ; ---- voice register base = voice<<4 in $09
    mov a, $03
    xcn a
    mov $09, a
    ; ---- SRCN = waveform (direct)
    mov a, $09
    or a, #$04                   ; vN SRCN reg
    mov $F2, a
    mov a, $05
    mov $F3, a
    ; ---- ADSR off -> GAIN direct max (sustained tone)
    mov a, $09
    or a, #$05                   ; vN ADSR1
    mov $F2, a
    mov a, #$00
    mov $F3, a
    mov a, $09
    or a, #$07                   ; vN GAIN
    mov $F2, a
    mov a, #$7F
    mov $F3, a
    ; ---- noise on/off: waveform 6 = noise
    mov a, $05
    cmp a, #$06
    beq _kon_noise
    mov a, $03
    mov x, a
    mov a, !bitmask+x
    eor a, #$FF
    and a, $07
    mov $07, a
    bra _kon_non
_kon_noise:
    mov a, $03
    mov x, a
    mov a, !bitmask+x
    or a, $07
    mov $07, a
_kon_non:
    mov $F2, #$3D                ; NON
    mov a, $07
    mov $F3, a
    ; ---- pitch: P = pitch_tab[pitch*2]  (32-sample loop, no doubling)
    mov a, $04
    asl a
    mov x, a
    mov a, !pitch_tab+x
    mov $08, a                   ; P lo
    mov a, !pitch_tab+1+x
    mov y, a                     ; P hi
    ; stash P at $30 + voice*2 for the test to verify tuning
    mov a, $03
    asl a
    mov x, a
    mov a, $08
    mov $30+x, a
    inc x
    mov a, y
    mov $30+x, a
    ; ---- write pitch to the DSP
    mov a, $09
    or a, #$02                   ; vN P lo
    mov $F2, a
    mov a, $08
    mov $F3, a
    mov a, $09
    or a, #$03                   ; vN P hi
    mov $F2, a
    mov a, y
    mov $F3, a
    ; ---- volume: vol(0..7) -> DSP VOL L/R = vol<<4  (0..112)
    mov a, $06
    xcn a
    and a, #$70
    mov $08, a
    mov a, $09
    mov $F2, a                   ; vN vol L
    mov a, $08
    mov $F3, a
    mov a, $09
    or a, #$01
    mov $F2, a                   ; vN vol R
    mov a, $08
    mov $F3, a
    ; ---- key on: clear this voice's KOF shadow, then KON
    mov a, $03
    mov x, a
    mov a, !bitmask+x
    mov $08, a
    eor a, #$FF
    and a, $0A
    mov $0A, a
    mov $F2, #$5C
    mov $F3, a
    mov $F2, #$4C
    mov a, $08
    mov $F3, a                   ; KON
    or a, $00
    mov $00, a
    jmp !ack

.INCLUDE "build/p8spc_tables.asm"

bitmask:
    .db $01, $02, $04, $08, $10, $20, $40, $80

.ENDS
