#ifndef MIGA80_DEMO_INTRO_EFFECT_H
#define MIGA80_DEMO_INTRO_EFFECT_H
#include <stdint.h>
#define MIGA80_INTRO_WIDTH 256U
#define MIGA80_INTRO_STRIP_Y 96U
#define MIGA80_INTRO_STRIP_HEIGHT 64U
#define MIGA80_INTRO_STRIP_BYTES (MIGA80_INTRO_WIDTH * MIGA80_INTRO_STRIP_HEIGHT)
#define MIGA80_INTRO_TICKS 160U
#define MIGA80_INTRO_LOGO_X 30U
#define MIGA80_INTRO_LOGO_Y 106U
/* A centered strip of canonical colour4 pixels; all other scanlines stay black. */
void miga80_intro_render(uint8_t *strip, unsigned int tick);
uint32_t miga80_intro_rgb(unsigned int index);
#endif
