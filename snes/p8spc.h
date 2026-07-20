/* p8spc.h — SPC700 audio driver interface (SNES side).
**
** p8spc_boot() IPL-uploads the resident driver (p8spc.asm) into the SPC
** and starts it; the other calls drive it over the $2140-$2143 mailbox.
** M6a bring-up reuses snes86's proven synth + upload protocol; M6b/c
** evolve the driver into the PICO-8 real-time sequencer. On the host these
** are no-ops (no SPC hardware). */
#ifndef P8SPC_H
#define P8SPC_H
#include <stdint.h>

void p8spc_boot(void);   /* upload + start the driver (silent if no APU) */
void p8spc_kon(uint8_t note, uint8_t inst, uint8_t voice, uint8_t vel);
void p8spc_kof(uint8_t voice);
void p8spc_alloff(void);

#endif
