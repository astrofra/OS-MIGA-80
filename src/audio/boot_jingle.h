#ifndef MIGA80_AUDIO_BOOT_JINGLE_H
#define MIGA80_AUDIO_BOOT_JINGLE_H
#include <stddef.h>
#include <stdint.h>
#define MIGA80_BOOT_JINGLE_RATE 11025U
#define MIGA80_BOOT_JINGLE_PERIOD 322U
#define MIGA80_BOOT_JINGLE_SAMPLES 33076U
/* Procedural, integer-only PCM synthesis. This object is built for 68000. */
void miga80_boot_jingle_generate(int8_t *samples);
#endif
