; snes86 — SPC700 8-voice MIDI synth (M14). Replaces the beep driver;
; the beep is now just a raw-pitch note on voice 7.
;
; Mailbox (65816 -> SPC), written F4/F5/F6 first, F7 last:
;   $F7 = (seq<<4) | op   seq cycles 1-15; SPC acks by echoing to its
;                         own port3 ($F7 out) after processing
;   op 1 = KON     F4=MIDI note, F5=(inst<<4)|voice, F6=velocity 0-127
;   op 2 = KOF     F4=voice
;   op 3 = all notes off
;   op 4 = KON raw F4/F5 = DSP P lo/hi (voice 7, square, full volume —
;                          the PC-speaker/BEL path)
; dp $00 = keyed-voice bitmask (0 = silent; the test suite reads it).
;
; Instruments (inst 0-5): SRCN + ADSR pairs from inst_tab; inst 3 uses
; the 32-sample sine (pitch doubled); inst 5 = percussion (noise via
; NON + snappy envelope).

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

; ---- sample directory: 4 entries (start, loop) -----------------------
sdir:
    .dw wav_sq,  wav_sq
    .dw wav_tri, wav_tri
    .dw wav_saw, wav_saw
    .dw wav_sin, wav_sin

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

do_kon:
    mov a, $F4
    mov $04, a                   ; note
    mov a, $F6
    mov $06, a                   ; velocity
    mov a, $F5
    and a, #$07
    mov $03, a                   ; voice
    mov a, $F5
    xcn a
    and a, #$0F
    mov $05, a                   ; inst
    ; ---- voice register base = voice<<4 in $09
    mov a, $03
    xcn a
    mov $09, a
    ; ---- instrument setup: X = inst*4
    mov a, $05
    asl a
    asl a
    mov x, a
    mov a, !inst_tab+x            ; SRCN
    mov $08, a
    mov a, $09
    or a, #$04                   ; vN SRCN reg
    mov $F2, a
    mov a, $08
    mov $F3, a
    mov a, !inst_tab+1+x          ; ADSR1
    mov $08, a
    mov a, $09
    or a, #$05
    mov $F2, a
    mov a, $08
    mov $F3, a
    mov a, !inst_tab+2+x          ; ADSR2
    mov $08, a
    mov a, $09
    or a, #$06
    mov $F2, a
    mov a, $08
    mov $F3, a
    ; ---- noise on/off for drums (inst 5)
    mov a, $05
    cmp a, #$05
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
    mov $F2, #$3D
    mov a, $07
    mov $F3, a
    ; ---- pitch: pitch_tab[note*2], doubled when inst flag bit0
    mov a, $04
    asl a
    mov x, a
    mov a, !pitch_tab+x
    mov $08, a
    mov a, !pitch_tab+1+x
    mov y, a                     ; P hi in Y, lo in $08
    mov a, $05
    asl a
    asl a
    mov x, a
    mov a, !inst_tab+3+x          ; flags
    and a, #$01
    beq _p_wr
    mov a, $08                   ; double: 16-bit asl
    asl a
    mov $08, a
    mov a, y
    rol a
    mov y, a
_p_wr:
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
    ; ---- volume from velocity (drums are full-scale noise = far louder
    ; than the tonal BRR voices; attenuate inst 5 so it doesn't explode)
    mov a, $06
    lsr a                        ; 0-63
    mov $08, a
    mov a, $05
    cmp a, #$05
    bne _vol_wr
    lsr $08                      ; drum: 1/16 volume (vel>>5) — noise is
    lsr $08                      ; harsh + fires hundreds of times; keep it
    lsr $08                      ; well under the melody
    lsr $08
_vol_wr:
    mov a, $09
    mov $F2, a                   ; vN vol L (reg 0)
    mov a, $08
    mov $F3, a
    mov a, $09
    or a, #$01
    mov $F2, a
    mov a, $08
    mov $F3, a
    ; ---- key on: clear ONLY this voice's KOF shadow, then KON (leave
    ; every other voice's release phase untouched)
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

; inst -> SRCN, ADSR1, ADSR2, flags (bit0 = double pitch/sine)
inst_tab:
    .db 1, $AF, $6C, 0           ; 0 pluck  (triangle, A15 D2 S3 R12)
    .db 0, $8E, $E8, 0           ; 1 organ  (square,   A14 D0 S7 R8)
    .db 2, $8E, $E8, 0           ; 2 saw sustain (strings/brass)
    .db 3, $AF, $6C, 1           ; 3 sine pluck
    .db 2, $86, $E9, 0           ; 4 pad    (saw, slow attack)
    .db 0, $DF, $00, 0           ; 5 drum   (noise, instant attack, fast
                                 ; decay to silence — a short tick, no ring)

.INCLUDE "build/p8spc_tables.asm"

bitmask:
    .db $01, $02, $04, $08, $10, $20, $40, $80

.ENDS
