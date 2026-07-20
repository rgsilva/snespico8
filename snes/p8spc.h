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
/* key voice (0..3) with PICO-8 waveform (0..7) at pitch (0..63), vol (0..7) */
void p8spc_note(uint8_t pitch, uint8_t wave, uint8_t voice, uint8_t vol);
void p8spc_kof(uint8_t voice);
void p8spc_alloff(void);

/* stream the cart's SFX (0x3200) + music (0x3100) into the SPC's own RAM so
** the driver can sequence them autonomously. Call once after the cart's
** audio is in place (p8_reset + asset copy). */
void p8spc_load_audio(void);
/* the autonomous sequencer: play sfx (0..63) on channel (0..3), real-time */
void p8spc_play_sfx(uint8_t sfx, uint8_t channel);
void p8spc_play_music(uint8_t pattern);   /* start the song at pattern 0..63 */
void p8spc_stop_music(void);

#endif
