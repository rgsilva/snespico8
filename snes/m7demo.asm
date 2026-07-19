; M0d: Mode 7 as a PICO-8 128x128 chunky framebuffer.
;
; Source of truth: an 8 KB PICO-8-format framebuffer (2 px/byte, low
; nibble = left pixel) at $7E:2000. Expansion: 256-entry word LUT turns
; each source byte into two 8bpp pixel bytes, written tile-major into a
; 16 KB staging buffer at $7E:4000, then DMA'd to the Mode 7 tile data
; (VRAM high bytes). The 128x128 tilemap (low bytes) holds tiles 0-255
; as a 16x16 identity block, staged at $7E:8000.
;
; Letterbox: image at plane top-left, scrolled to screen center
; (M7HOFS=-64, M7VOFS=-48). Top/left borders come free from M7SEL "over
; = transparent"; the right border is clipped with window 1; the bottom
; band is blanked by an HDMA that turns BG1 off from line 176.
;
; Benchmark: NMI counts frames at $20. 8 full expansions, then 8 full
; 16 KB DMAs (force blank). Results in WRAM: $10 magic $CAFE,
; $12/$14/$16 = frame counter before/mid/after.

.MEMORYMAP
  SLOTSIZE $8000
  DEFAULTSLOT 0
  SLOT 0 $8000
.ENDME
.ROMBANKSIZE $8000
.ROMBANKS 4

.SNESHEADER
  ID "SNES"
  NAME "SNESPICO8 M7 SPIKE   "
  LOROM
  CARTRIDGETYPE $00
  ROMSIZE $08
  SRAMSIZE $00
  COUNTRY $01
  LICENSEECODE $00
  VERSION $00
.ENDSNES

.SNESNATIVEVECTOR
  COP EmptyHandler
  BRK EmptyHandler
  ABORT EmptyHandler
  NMI NmiHandler
  IRQ EmptyHandler
.ENDNATIVEVECTOR
.SNESEMUVECTOR
  COP EmptyHandler
  ABORT EmptyHandler
  NMI EmptyHandler
  RESET Reset
  IRQBRK EmptyHandler
.ENDEMUVECTOR

; --- WRAM layout (bank $7E, low mirror for DP) ---
.DEFINE vSrc    $00        ; DP: expansion source pointer
.DEFINE vDst    $02        ; DP: expansion dest pointer
.DEFINE vTmp    $04        ; DP: scratch (byte pair)
.DEFINE vTy     $06        ; DP: tile row 0-15
.DEFINE vTx     $08        ; DP: tile col 0-15
.DEFINE vRy     $0A        ; DP: row within tile 0-7
.DEFINE rMagic  $10        ; result: $CAFE when done
.DEFINE rC0     $12        ; frame counter before expansions
.DEFINE rC1     $14        ; frame counter after 8 expansions
.DEFINE rC2     $16        ; frame counter after 8 DMAs
.DEFINE vFrames $20        ; NMI frame counter
.DEFINE FB      $2000      ; PICO-8 framebuffer, 8 KB
.DEFINE XB      $4000      ; expanded 8bpp buffer, 16 KB
.DEFINE MAPB    $8000      ; tilemap staging, 16 KB

.BANK 0 SLOT 0
.ORG 0
.SECTION "Main" SEMIFREE

EmptyHandler:
    rti

NmiHandler:
    rep #$30
    pha
    lda.l $004210            ; ack NMI (16-bit read also grabs $4211, harmless)
    lda.l $7E0020
    inc a
    sta.l $7E0020
    pla
    rti

Reset:
    sei
    clc
    xce
    rep #$30
    ldx #$1FF0
    txs
    lda #$0000
    tcd
    sep #$20                 ; A 8-bit for register setup
    lda #$8F
    sta.l $002100            ; force blank
    lda #$00
    sta.l $7E0020            ; frame counter = 0
    sta.l $7E0021

; --- PPU setup: mode 7, matrix, scroll, palette, window ---
    lda #$07
    sta.l $002105            ; BGMODE 7
    lda #$80
    sta.l $00211A            ; M7SEL: outside plane = transparent
    ; identity matrix A=D=$0100, B=C=0, center (0,0)
    lda #$00
    sta.l $00211B
    lda #$01
    sta.l $00211B            ; M7A = $0100
    lda #$00
    sta.l $00211C
    sta.l $00211C            ; M7B = 0
    sta.l $00211D
    sta.l $00211D            ; M7C = 0
    sta.l $00211E
    lda #$01
    sta.l $00211E            ; M7D = $0100
    lda #$00
    sta.l $00211F
    sta.l $00211F            ; M7X = 0
    sta.l $002120
    sta.l $002120            ; M7Y = 0
    lda #$C0                 ; M7HOFS = -64 ($1FC0)
    sta.l $00210D
    lda #$1F
    sta.l $00210D
    lda #$D0                 ; M7VOFS = -48 ($1FD0)
    sta.l $00210E
    lda #$1F
    sta.l $00210E
    ; palette: 16 PICO-8 colors into CGRAM 0-15
    lda #$00
    sta.l $002121
    ldx #0
_pal:
    lda.l PalPico8,x
    sta.l $002122
    inx
    cpx #32
    bne _pal
    ; window 1 clips the right band: visible [64,191], invert = mask outside
    lda #$03
    sta.l $002123            ; W12SEL: win1 on BG1, inverted
    lda #64
    sta.l $002126            ; WH0
    lda #191
    sta.l $002127            ; WH1
    lda #$01
    sta.l $00212E            ; TMW: window masking on BG1

; --- build PICO-8 test pattern in FB: 16 vertical stripes + diagonal ---
    rep #$20
    pea $7E7E
    plb
    plb                      ; DBR = $7E
    sep #$20
    ldx #0                   ; byte index 0..8191
_pat:
    txa                      ; low byte of index
    and #$3F                 ; byte-in-row 0..63
    lsr
    lsr                      ; stripe color 0..15
    sta vTmp
    asl
    asl
    asl
    asl
    ora vTmp                 ; c | c<<4
    sta.w FB,x
    inx
    cpx #8192
    bne _pat
    ; diagonal: pixel (i,i) = white (7), i = 0..127
    ldx #0                   ; i
_diag:
    rep #$20
    txa
    xba                      ; i*256
    lsr
    lsr                      ; i*64
    sta vTmp
    txa
    lsr                      ; i/2
    clc
    adc vTmp
    tay                      ; y = i*64 + i/2 (offset in FB)
    sep #$20
    txa
    and #$01
    bne _diag_odd
    lda.w FB,y               ; even x: low nibble
    and #$F0
    ora #$07
    bra _diag_w
_diag_odd:
    lda.w FB,y               ; odd x: high nibble
    and #$0F
    ora #$70
_diag_w:
    sta.w FB,y
    inx
    cpx #128
    bne _diag

; --- build tilemap staging: 16x16 identity block in a 128x128 map ---
    ; entry(my,mx) = my*16+mx if my<16 && mx<16 else 0
    ldx #0                   ; write index 0..16383
_map:
    sep #$20
    lda #$00
    sta vTmp
    rep #$20
    txa
    and #$3F80               ; my*128
    cmp #2048                ; my >= 16 -> entry 0
    bcs _map_store
    txa
    and #$007F               ; mx
    cmp #16                  ; mx >= 16 -> entry 0
    bcs _map_store
    sta vTmp                 ; mx (16-bit store, high byte 0)
    txa
    and #$0F80               ; my*128
    lsr
    lsr
    lsr                      ; my*16
    ora vTmp
    sep #$20
    sta vTmp
_map_store:
    sep #$20
    lda vTmp
    sta.w MAPB,x
    rep #$20
    inx
    cpx #16384
    bne _map

; --- upload tilemap: DMA to $2118 (low bytes), VMAIN inc-after-low ---
    sep #$20
    lda #$00
    sta.l $002115            ; VMAIN: inc after low byte
    sta.l $002116            ; VRAM word addr = 0
    sta.l $002117
    sta.l $004300            ; DMA0 mode 0
    lda #$18
    sta.l $004301            ; B addr $2118
    rep #$20
    lda #MAPB
    sta.l $004302
    sep #$20
    lda #$7E
    sta.l $004304
    rep #$20
    lda #16384
    sta.l $004305
    sep #$20
    lda #$01
    sta.l $00420B            ; fire

; --- enable NMI for the frame counter ---
    lda #$80
    sta.l $004200

; --- benchmark: 8 expansions, then 8 tile-data DMAs ---
    rep #$20
    lda.w vFrames
    sta.w rC0
    ldy #8
_bench_x:
    phy
    jsr Expand
    ply
    dey
    bne _bench_x
    lda.w vFrames
    sta.w rC1
    ldy #8
_bench_d:
    phy
    jsr DmaTiles
    ply
    dey
    bne _bench_d
    lda.w vFrames
    sta.w rC2

; --- screen on + bottom-band HDMA, publish results ---
    sep #$20
    lda #$01
    sta.l $00212C            ; TM: BG1 on main screen
    lda #$00
    sta.l $004310            ; HDMA ch1 mode 0
    lda #$2C
    sta.l $004311            ; -> $212C
    rep #$20
    lda #HdmaTm
    sta.l $004312
    sep #$20
    lda #:HdmaTm
    sta.l $004314
    lda #$02
    sta.l $00420C            ; HDMA enable ch1
    lda #$0F
    sta.l $002100            ; screen on, full brightness
    rep #$20
    lda #$CAFE
    sta.w rMagic

_halt:
    wai
    bra _halt

; --- Expand: FB (4bpp packed) -> XB (8bpp tile-major); A/X/Y 16-bit, DBR=$7E
Expand:
    rep #$30
    lda #XB
    sta vDst
    stz vTy
_e_ty:
    stz vTx
_e_tx:
    ; src = FB + ty*512 + tx*4
    lda vTy
    xba                      ; *256
    asl a                    ; *512
    sta vSrc
    lda vTx
    asl a
    asl a
    clc
    adc vSrc
    adc #FB
    sta vSrc
    stz vRy
_e_ry:
.REPT 2
    lda (vSrc)               ; b1:b0
    sta vTmp
    and #$00FF
    asl a
    tax
    lda.l LUT16,x
    sta (vDst)
    lda vTmp
    xba
    and #$00FF
    asl a
    tax
    lda.l LUT16,x
    ldy #2
    sta (vDst),y
    lda vSrc
    inc a
    inc a
    sta vSrc
    lda vDst
    clc
    adc #4
    sta vDst
.ENDR
    ; next row within tile: src += 64-4
    lda vSrc
    clc
    adc #60
    sta vSrc
    lda vRy
    inc a
    sta vRy
    cmp #8
    beq +
    jmp _e_ry
+   lda vTx
    inc a
    sta vTx
    cmp #16
    beq +
    jmp _e_tx
+   lda vTy
    inc a
    sta vTy
    cmp #16
    beq +
    jmp _e_ty
+   rts

; --- DmaTiles: XB -> VRAM high bytes (Mode 7 tile data); force blank assumed
DmaTiles:
    sep #$20
    lda #$80
    sta.l $002115            ; VMAIN: inc after high byte
    lda #$00
    sta.l $002116
    sta.l $002117
    sta.l $004300            ; mode 0
    lda #$19
    sta.l $004301            ; -> $2119
    rep #$20
    lda #XB
    sta.l $004302
    sep #$20
    lda #$7E
    sta.l $004304
    rep #$20
    lda #16384
    sta.l $004305
    sep #$20
    lda #$01
    sta.l $00420B
    rep #$30
    rts

HdmaTm:
    .db $7F, $01             ; 127 lines: BG1 on
    .db 49,  $01             ; 49 more lines: BG1 on (total 176)
    .db 48,  $00             ; 48 lines: BG1 off
    .db 0                    ; end

.ENDS

.BANK 1 SLOT 0
.ORG 0
.SECTION "Tables" SEMIFREE
.INCLUDE "build/m7tables.inc"
.ENDS
