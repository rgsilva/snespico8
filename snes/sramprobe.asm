; SRAM extent probe (M0c), extended from emu8086/snes/sramtest.asm.
; Parameterized header SRAMSIZE via SRAMSZ define (wrapper files set it).
; Writes a unique signature to each LoROM SRAM bank $70-$7D ($0000-$7FFF),
; reads them back: a physically backed bank returns its own signature, a
; mirror returns a later writer's, open bus returns neither.
; Results in WRAM: $04 = distinct-bank count (x32 = KB), $06 = $CAFE done
; flag, $10+i*2 = raw word read back from bank $70+i.

.IFNDEF SRAMSZ
.DEFINE SRAMSZ $07
.ENDIF

.MEMORYMAP
  SLOTSIZE $8000
  DEFAULTSLOT 0
  SLOT 0 $8000
.ENDME
.ROMBANKSIZE $8000
.ROMBANKS 4

.SNESHEADER
  ID "SNES"
  NAME "SRAM PROBE           "
  LOROM
  CARTRIDGETYPE $02             ; ROM + RAM + battery
  ROMSIZE $08                   ; 128 KB ROM (4 banks; plenty)
  SRAMSIZE SRAMSZ
  COUNTRY $01
  LICENSEECODE $00
  VERSION $00
.ENDSNES

.SNESNATIVEVECTOR
  COP EmptyHandler
  BRK EmptyHandler
  ABORT EmptyHandler
  NMI EmptyHandler
  IRQ EmptyHandler
.ENDNATIVEVECTOR
.SNESEMUVECTOR
  COP EmptyHandler
  ABORT EmptyHandler
  NMI EmptyHandler
  RESET Reset
  IRQBRK EmptyHandler
.ENDEMUVECTOR

.BANK 0 SLOT 0
.ORG 0
.SECTION "Main" SEMIFREE

.DEFINE NBANKS 14                ; $70..$7D

EmptyHandler:
    rti

Reset:
    sei
    clc
    xce
    rep #$30
    ldx #$1FFF
    txs
    lda #$0000
    tcd

    ; -- WRITE PHASE: [$70+i : 0000] = $C000 | i, for i = 0..NBANKS-1.
    ; $F0/$F1 = pointer offset (0), $F2 = pointer bank.
    ldy #0
_wr:
    stz $F0                      ; offset low16 = $0000
    tya
    clc
    adc #$0070
    sep #$20
    sta $F2                      ; bank byte = $70 + i
    rep #$20
    tya
    ora #$C000                   ; distinct signature per bank
    sta [$F0]
    iny
    cpy #NBANKS
    bcc _wr

    ; -- READ PHASE: count self-matching banks, store raw readbacks.
    ldy #0
    ldx #0                       ; X = distinct-bank count
_rd:
    stz $F0
    tya
    clc
    adc #$0070
    sep #$20
    sta $F2
    rep #$20
    lda [$F0]
    sta $00                      ; got
    phx                          ; save count; dump raw word to $10+i*2
    tya
    asl
    tax
    lda $00
    sta $10,x
    plx
    tya
    ora #$C000                   ; expected
    cmp $00
    bne _rd_n
    inx                          ; self-match: a distinct physical bank
_rd_n:
    iny
    cpy #NBANKS
    bcc _rd
    stx $04                      ; RESULT: distinct 32 KB banks (x32 = KB)
    lda #$CAFE
    sta $06                      ; done flag

_halt:
    wai
    bra _halt

.ENDS
