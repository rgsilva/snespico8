/* p8spc.c — SPC700 audio: IPL-upload the resident driver and drive it over
** the $2140-$2143 mailbox. M6a reuses snes86's proven 8-voice synth
** (p8spc.asm) and apu_upload handshake verbatim; M6b/c evolve the driver
** into the PICO-8 real-time sequencer. SNES-only; host builds are no-ops. */
#include "p8spc.h"

#ifdef __CALYPSI__
#include "p8spc_blob.h"   /* generated: p8spc_blob[], P8SPC_ADDR/ENTRY/LEN */

#define REG8(a)  (*(volatile uint8_t *)(unsigned long)(a))
#define REG16(a) (*(volatile uint16_t *)(unsigned long)(a))

/* IPL-ROM upload (ported verbatim from snes86 dosboot.asm:apu_upload):
** wait for the $AA boot signature, set the destination, kick with $CC,
** then stream the driver byte-by-byte with the rolling-index echo, and
** finally jump to ENTRY with the (last_index+2) "go" value. If the APU
** never answers, return quietly and run silent. */
static void apu_upload(void)
{
  uint16_t i, timeout = 0;
  while (REG8(0x2140) != 0xAA) {
    if (++timeout == 0)
      return;                          /* no APU: silent */
  }
  REG16(0x2142) = P8SPC_ADDR;          /* destination address */
  REG8(0x2141) = 0x01;                 /* kind = start & run */
  REG8(0x2140) = 0xCC;                 /* kick */
  while (REG8(0x2140) != 0xCC) { }
  for (i = 0; i < P8SPC_LEN; i++) {
    REG8(0x2141) = p8spc_blob[i];
    REG8(0x2140) = (uint8_t)i;
    while (REG8(0x2140) != (uint8_t)i) { }
  }
  REG16(0x2142) = P8SPC_ENTRY;         /* entry point (past the sample dir) */
  REG8(0x2141) = 0x00;                 /* kind = 0 -> jump */
  REG8(0x2140) = (uint8_t)(P8SPC_LEN + 1);  /* (last index + 2) mod 256 */
  { volatile uint16_t d; for (d = 0; d < 400; d++) { } }  /* let IPL jump */
  REG8(0x2140) = 0; REG8(0x2141) = 0; REG16(0x2142) = 0;  /* scrub mailbox */
}

/* one mailbox command: F4/F5/F6 then the trigger F7 = (seq<<4)|op last.
** seq cycles 1..15 so consecutive triggers always differ (the driver
** detects a new command by the change). Wait for the SPC to echo the
** trigger to its port3 output (bounded, so a driver bug can't hang the
** ROM). */
static uint8_t spc_seq = 1;
static void spc_cmd(uint8_t f4, uint8_t f5, uint8_t f6, uint8_t op)
{
  uint8_t trig = (uint8_t)((spc_seq << 4) | (op & 0x0f));
  uint16_t to = 0;
  REG8(0x2140) = f4;
  REG8(0x2141) = f5;
  REG8(0x2142) = f6;
  REG8(0x2143) = trig;                 /* trigger, written last */
  while (REG8(0x2143) != trig) {
    if (++to == 0)
      break;                           /* ack timeout: don't hang */
  }
  spc_seq = (spc_seq >= 15) ? 1 : (uint8_t)(spc_seq + 1);
}

void p8spc_boot(void) { apu_upload(); }

void p8spc_note(uint8_t pitch, uint8_t wave, uint8_t voice, uint8_t vol)
{
  spc_cmd(pitch, (uint8_t)((wave << 4) | (voice & 7)), vol, 1);
}
void p8spc_kof(uint8_t voice) { spc_cmd(voice, 0, 0, 2); }
void p8spc_alloff(void)       { spc_cmd(0, 0, 0, 3); }

#else  /* host: no SPC hardware */
void p8spc_boot(void) { }
void p8spc_note(uint8_t p, uint8_t w, uint8_t v, uint8_t vol)
{ (void)p; (void)w; (void)v; (void)vol; }
void p8spc_kof(uint8_t v) { (void)v; }
void p8spc_alloff(void) { }
#endif
