; snespico8 — SPC700 audio driver (M6). Derived from snes86's 8-voice
; synth: same DSP setup, mailbox handshake and key-on/off, but PICO-8-native
; (waveform + pitch 0-63) and, from M6c, an AUTONOMOUS SEQUENCER: a hardware
; timer ticks SFX/music rows in real wall-clock time while the 65816 game
; loop crawls. The SFX (0x3200, 64x68B) and music (0x3100, 64x4B) data are
; streamed into SPC RAM once (op 8/9) and the driver sequences from them.
;
; Mailbox (65816 -> SPC), F4/F5/F6 first, F7 last; SPC echoes F7 to ack:
;   $F7 = (seq<<4) | op   seq cycles 1-15
;   op 1 = NOTE ON  F4=pitch 0-63, F5=(waveform<<4)|voice, F6=vol 0-7
;   op 2 = KOF      F4=voice
;   op 3 = all off
;   op 4 = KON raw  F4/F5 = DSP P lo/hi (voice 7 beep)
;   op 5 = PLAY SFX F4=sfx 0-63, F5=channel 0-3
;   op 6 = PLAY MUS F4=pattern 0-63
;   op 7 = STOP MUS
;   op 8 = SET PTR  F4/F5 = SPC load address lo/hi
;   op 9 = LOAD B   F4 = byte -> [ptr]++, for streaming the audio data
;
; DSP: SRCN = PICO-8 waveform 0-7 (triangle..phaser); noise via NON.
; dp $00 keyed mask, $01 last F7, $03 voice, $04 pitch, $05 wave, $06 vol,
;    $07 NON shadow, $08/$09 scratch, $0A KOF shadow,
;    $30+voice*2 = last DSP P programmed (test readback),
;    $40+c ch_sfx, $44+c row, $48+c endrow, $4C+c tick, $50+c speed,
;    $54+c loopstart, $58+c loopend, $5C+c music-owned  (c = 0..3),
;    $60 mus_pat, $61/$62 mus_ticks, $63/$64 mus_len, $65/$66 load ptr,
;    $67/$68 sfx-base ptr, $69 scratch, $6A tick-count, $6B/$6C heartbeat.
; Audio data in SPC RAM: SFX at $1000 (64x68), music at $2200 (64x4).

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
.SECTION "P8spc" FORCE

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

entry:
    mov x, #$EF
    mov sp, x
    mov a, #$00
    mov $00, a
    mov $01, a
    mov $07, a
    mov $0A, a                   ; KOF shadow
    mov $F2, #$6C
    mov $F3, #$20                ; FLG: echo off
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
    mov $F3, #$2C                ; FLG: lower noise clock (echo-off kept)

    ; ---- M6c: clear sequencer state $40..$6F, set idle markers, timer ----
    mov x, #0
_si_z:
    mov a, #0
    mov $40+x, a
    inc x
    cmp x, #$30
    bne _si_z
    mov a, #$FF
    mov $40, a                   ; ch_sfx[0..3] = idle
    mov $41, a
    mov $42, a
    mov $43, a
    mov $60, a                   ; mus_pat = stopped
    mov $FA, #66                 ; Timer 0 target -> 8000/66 = 121.2 Hz
    mov $F1, #$01                ; enable T0 (bit0); do NOT clear the ports

hold:
    mov a, $F7                   ; wait out stale handshake values
    bne hold

loop:
    ; ---- timer: tick the sequencer once per elapsed 121 Hz tick ----
    mov a, $FD                   ; T0 output counter (reads and clears)
    beq _no_tick
    mov $6A, a                   ; ticks this pass
    clrc                         ; heartbeat += ticks (16-bit)
    adc a, $6B
    mov $6B, a
    bcc _hb_nc
    inc $6C
_hb_nc:
_tk:
    call !seq_tick
    dec $6A
    bne _tk
_no_tick:
    ; ---- mailbox dispatch ----
    mov a, $F7
    cmp a, $01
    beq loop                     ; unchanged
    mov $01, a
    and a, #$0F                  ; op
    cmp a, #$01
    bne _d2
    jmp !do_kon
_d2:
    cmp a, #$02
    bne _d3
    jmp !do_kof
_d3:
    cmp a, #$03
    bne _d4
    jmp !do_alloff
_d4:
    cmp a, #$04
    bne _d5
    jmp !do_raw
_d5:
    cmp a, #$05
    bne _d6
    jmp !do_play_sfx
_d6:
    cmp a, #$06
    bne _d7
    jmp !do_play_mus
_d7:
    cmp a, #$07
    bne _d8
    jmp !do_stop_mus
_d8:
    cmp a, #$08
    bne _d9
    jmp !do_setptr
_d9:
    cmp a, #$09
    bne ack
    jmp !do_loadb
ack:
    mov a, $01
    mov $F7, a                   ; echo = ack
    bra loop

; ---- KOF voice in $03 ------------------------------------------------
kof_v:
    mov a, $03
    mov x, a
    mov a, !bitmask+x
    mov $08, a
    or a, $0A
    mov $0A, a
    mov $F2, #$5C
    mov $F3, a
    mov a, $08
    eor a, #$FF
    and a, $00
    mov $00, a
    ret

do_kof:
    mov a, $F4
    and a, #$07
    mov $03, a
    call !kof_v
    jmp !ack

do_alloff:
    mov a, #$FF
    mov $0A, a
    mov $F2, #$5C
    mov $F3, a
    mov a, #$00
    mov $00, a
    jmp !ack

do_raw:                          ; beep: voice 7, waveform 0, raw pitch
    mov $F2, #$74
    mov $F3, #$00
    mov $F2, #$75
    mov $F3, #$00
    mov $F2, #$77
    mov $F3, #$7F
    mov $F2, #$70
    mov $F3, #$50
    mov $F2, #$71
    mov $F3, #$50
    mov a, $F4
    mov $F2, #$72
    mov $F3, a
    mov a, $F5
    mov $F2, #$73
    mov $F3, a
    mov a, $00
    and a, #$80
    bne _raw_on
    mov a, $0A
    and a, #$7F
    mov $0A, a
    mov $F2, #$5C
    mov $F3, a
    mov $F2, #$4C
    mov $F3, #$80
_raw_on:
    mov a, $00
    or a, #$80
    mov $00, a
    jmp !ack

; ---- op 1: NOTE ON from the mailbox ----------------------------------
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
    call !note_on
    jmp !ack

; ---- note_on: $03=voice $04=pitch $05=wave $06=vol -> DSP + KON -------
note_on:
    mov a, $03
    xcn a
    mov $09, a                   ; voice register base = voice<<4
    ; SRCN = waveform
    mov a, $09
    or a, #$04
    mov $F2, a
    mov a, $05
    mov $F3, a
    ; ADSR off -> GAIN direct max
    mov a, $09
    or a, #$05
    mov $F2, a
    mov a, #$00
    mov $F3, a
    mov a, $09
    or a, #$07
    mov $F2, a
    mov a, #$7F
    mov $F3, a
    ; NON: waveform 6 = noise
    mov a, $05
    cmp a, #$06
    beq _no_noise
    mov a, $03
    mov x, a
    mov a, !bitmask+x
    eor a, #$FF
    and a, $07
    mov $07, a
    bra _no_non
_no_noise:
    mov a, $03
    mov x, a
    mov a, !bitmask+x
    or a, $07
    mov $07, a
_no_non:
    mov $F2, #$3D
    mov a, $07
    mov $F3, a
    ; pitch: P = pitch_tab[pitch*2]
    mov a, $04
    asl a
    mov x, a
    mov a, !pitch_tab+x
    mov $08, a                   ; P lo
    mov a, !pitch_tab+1+x
    mov y, a                     ; P hi
    ; stash P at $30+voice*2 (test readback)
    mov a, $03
    asl a
    mov x, a
    mov a, $08
    mov $30+x, a
    inc x
    mov a, y
    mov $30+x, a
    ; write pitch to DSP
    mov a, $09
    or a, #$02
    mov $F2, a
    mov a, $08
    mov $F3, a
    mov a, $09
    or a, #$03
    mov $F2, a
    mov a, y
    mov $F3, a
    ; volume: vol(0..7) -> VOL L/R = vol<<4
    mov a, $06
    xcn a
    and a, #$70
    mov $08, a
    mov a, $09
    mov $F2, a
    mov a, $08
    mov $F3, a
    mov a, $09
    or a, #$01
    mov $F2, a
    mov a, $08
    mov $F3, a
    ; key on: clear this voice's KOF shadow, then KON
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
    mov $F3, a
    or a, $00
    mov $00, a
    ret

; ---- op 8/9: stream the audio data into SPC RAM ----------------------
do_setptr:
    mov a, $F4
    mov $65, a
    mov a, $F5
    mov $66, a
    jmp !ack

do_loadb:
    mov y, #0
    mov a, $F4
    mov [$65]+y, a
    inc $65
    bne _lb_nc
    inc $66
_lb_nc:
    jmp !ack

; ---- sfx-base ptr $67/$68 = $1000 + ch_sfx[X]*68 ---------------------
sfx_base_x:
    mov a, $40+x
    mov y, a
    mov a, #68
    mul ya                       ; YA = sfx*68
    mov $67, a
    mov a, y
    clrc
    adc a, #$10                  ; + $1000
    mov $68, a
    ret

; ---- seq_key: key channel X's current (sfx,row) note -----------------
seq_key:
    call !sfx_base_x
    mov a, $44+x                 ; row
    asl a
    mov y, a
    mov a, [$67]+y               ; note lo
    mov $08, a
    inc y
    mov a, [$67]+y               ; note hi
    mov $09, a
    mov a, x
    mov $03, a                   ; voice = channel
    ; pitch = lo & 0x3F
    mov a, $08
    and a, #$3F
    mov $04, a
    ; wave = ((lo>>6)&3) | ((hi&1)<<2)
    mov a, $08
    xcn a
    lsr a
    lsr a
    and a, #$03
    mov $05, a
    mov a, $09
    and a, #$01
    asl a
    asl a
    or a, $05
    mov $05, a
    ; vol = (hi>>1)&7
    mov a, $09
    lsr a
    and a, #$07
    mov $06, a
    ; NOTE: the effect field (word bits 12-14: slide/vibrato/drop/fade/arp)
    ; is not yet applied -- notes play at their base pitch/volume. Per-tick
    ; effect modulation is the remaining M6c piece (see docs/PROJECT.md).
    beq _sk_off                  ; volume 0 -> silence
    call !note_on
    ret
_sk_off:
    call !kof_v
    ret

; ---- chan_start_x: set channel X to play sfx A ($69 = music flag) ----
chan_start_x:
    and a, #$3F
    mov $40+x, a                 ; ch_sfx[x]
    call !sfx_base_x
    mov y, #65
    mov a, [$67]+y               ; speed (>=1)
    bne _cs_sp
    mov a, #1
_cs_sp:
    mov $50+x, a
    mov y, #66
    mov a, [$67]+y               ; loopstart
    mov $54+x, a
    mov y, #67
    mov a, [$67]+y               ; loopend
    mov $58+x, a
    mov a, #32
    mov $48+x, a                 ; endrow = 32
    mov a, #0
    mov $44+x, a                 ; row = 0
    mov $4C+x, a                 ; tick = 0
    mov a, $69
    mov $5C+x, a                 ; music-owned = flag
    call !seq_key                ; key the first note now
    ret

; ---- op 5: play sfx F4 on channel F5 ---------------------------------
do_play_sfx:
    mov a, $F5
    and a, #$03
    mov x, a
    mov a, #0
    mov $69, a                   ; music flag = 0
    mov a, $F4
    call !chan_start_x
    jmp !ack

; ---- op 6: play music pattern F4 -------------------------------------
do_play_mus:
    mov a, $F4
    and a, #$3F
    call !music_begin
    jmp !ack

; ---- op 7: stop music ------------------------------------------------
do_stop_mus:
    call !music_stop
    jmp !ack

; ---- music_begin: start pattern A ------------------------------------
music_begin:
    mov $60, a                   ; mus_pat = pattern
    asl a
    asl a
    mov $6E, a                   ; pattern base = $2200 + pat*4
    mov a, #$22
    mov $6F, a
    mov a, #0
    mov $61, a                   ; mus_ticks = 0
    mov $62, a
    mov $6D, a                   ; found = 0
    mov x, #0
_mb_ch:
    mov a, x
    mov y, a
    mov a, [$6E]+y               ; channel byte
    push a
    and a, #$40                  ; bit6 = disabled?
    bne _mb_dis
    mov a, #1
    mov $69, a                   ; music flag
    pop a                        ; channel byte (chan_start_x masks to sfx)
    push x
    call !chan_start_x
    pop x
    mov a, $6D
    bne _mb_next                 ; length already taken from an earlier channel
    mov a, #1
    mov $6D, a
    mov a, $50+x                 ; mus_len = 32 * this channel's speed
    mov y, a
    mov a, #32
    mul ya
    mov $63, a
    mov a, y
    mov $64, a
    bra _mb_next
_mb_dis:
    pop a
    mov a, $5C+x                 ; disabled: stop it if music-owned
    beq _mb_next
    mov a, #$FF
    mov $40+x, a
    mov a, x
    mov $03, a
    push x
    call !kof_v
    pop x
_mb_next:
    inc x
    cmp x, #4
    bne _mb_ch
    mov a, $6D                   ; no enabled channel -> default length 64
    bne _mb_done
    mov a, #64
    mov $63, a
    mov a, #0
    mov $64, a
_mb_done:
    ret

; ---- music_next: current pattern ended -> chain / loop / stop --------
music_next:
    mov a, $60
    asl a
    asl a
    mov $6E, a
    mov a, #$22
    mov $6F, a
    mov y, #2
    mov a, [$6E]+y               ; byte2 bit7 = stop
    and a, #$80
    bne _mn_stop
    mov y, #1
    mov a, [$6E]+y               ; byte1 bit7 = loop-end
    and a, #$80
    bne _mn_loop
    mov a, $60                   ; else advance to next pattern
    inc a
    cmp a, #64
    bcs _mn_stop
    jmp !music_begin
_mn_loop:
    call !music_loop_target
    jmp !music_begin
_mn_stop:
    call !music_stop
    ret

; ---- music_loop_target: scan back for the loop-start pattern ---------
music_loop_target:
    mov a, $60
_mlt:
    push a
    asl a
    asl a
    mov $6E, a
    mov a, #$22
    mov $6F, a
    mov y, #0
    mov a, [$6E]+y               ; byte0 bit7 = loop-start
    and a, #$80
    bne _mlt_found
    pop a
    cmp a, #0
    beq _mlt_zero
    dec a
    bra _mlt
_mlt_found:
    pop a
    ret
_mlt_zero:
    mov a, #0
    ret

; ---- music_stop: stop the song, silence its channels -----------------
music_stop:
    mov a, #$FF
    mov $60, a
    mov x, #0
_ms:
    mov a, $5C+x
    beq _ms_next
    mov a, #$FF
    mov $40+x, a
    mov a, x
    mov $03, a
    push x
    call !kof_v
    pop x
_ms_next:
    inc x
    cmp x, #4
    bne _ms
    ret

; ---- seq_tick: advance the music player + all channels one tick ------
seq_tick:
    mov a, $60                   ; mus_pat
    cmp a, #$FF
    beq _st_chans                ; music stopped
    inc $61                      ; mus_ticks++ (16-bit)
    bne _mt_nc
    inc $62
_mt_nc:
    mov a, $62                   ; mus_ticks >= mus_len ?
    cmp a, $64
    bcc _st_chans
    bne _mt_adv
    mov a, $61
    cmp a, $63
    bcc _st_chans
_mt_adv:
    call !music_next
_st_chans:
    mov x, #0
_st_loop:
    mov a, $40+x                 ; ch_sfx[x]
    cmp a, #$FF
    beq _st_next                 ; idle
    inc $4C+x                    ; tick++
    mov a, $4C+x
    cmp a, $50+x                 ; tick < speed?
    bcc _st_next
    mov a, #0
    mov $4C+x, a                 ; tick = 0
    inc $44+x                    ; row++
    mov a, $58+x                 ; loopend
    beq _st_endchk
    mov $69, a
    mov a, $44+x                 ; row >= loopend?
    cmp a, $69
    bcc _st_key
    mov a, $54+x                 ; row = loopstart
    mov $44+x, a
    bra _st_key
_st_endchk:
    mov a, $44+x                 ; row >= endrow?
    cmp a, $48+x
    bcc _st_key
    mov a, #$FF                  ; end: stop channel + KOF
    mov $40+x, a
    mov a, x
    mov $03, a
    push x
    call !kof_v
    pop x
    bra _st_next
_st_key:
    push x
    call !seq_key
    pop x
_st_next:
    inc x
    cmp x, #4
    bne _st_loop
    ret

.INCLUDE "build/p8spc_tables.asm"

bitmask:
    .db $01, $02, $04, $08, $10, $20, $40, $80

.ENDS
