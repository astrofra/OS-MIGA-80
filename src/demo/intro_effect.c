#include "demo/intro_effect.h"
#include <string.h>
#include "boot_logo_data.h"

uint32_t miga80_intro_rgb(unsigned int index)
{
    if (index < 4U) { return miga80_boot_logo_rgb[index]; }
    if (index == 4U) { return 0x13cfe7U; }
    if (index == 5U) { return 0xe84cabU; }
    return 0U;
}

static uint32_t scramble(uint32_t value)
{
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return value;
}

static void put(uint8_t *strip, int x, int y, uint8_t color)
{
    if ((unsigned int)x < MIGA80_INTRO_WIDTH &&
        (unsigned int)y < MIGA80_INTRO_STRIP_HEIGHT) {
        strip[(unsigned int)y * MIGA80_INTRO_WIDTH + (unsigned int)x] = color;
    }
}

void miga80_intro_render(uint8_t *strip, unsigned int tick)
{
    unsigned int y, intensity;
    (void)memset(strip, 0, MIGA80_INTRO_STRIP_BYTES);
    if (tick == 0U || tick >= 156U) { return; }
    intensity = tick < 32U ? 32U - tick : tick >= 120U ? tick - 119U : 0U;
    /* Two short chromatic shivers follow the second and third chord attacks. */
    if ((tick >= 44U && tick < 47U) || (tick >= 80U && tick < 83U)) { intensity = 5U; }
    for (y = 0U; y < MIGA80_BOOT_LOGO_HEIGHT; ++y) {
        const uint32_t noise = scramble(0x19801990UL + (tick / 2U) * 7349U + (y / 3U) * 977U);
        const int affected = intensity != 0U && (noise & 31U) < intensity;
        const int shift = affected ? ((int)((noise >> 8) & 31U) - 16) * (int)intensity / 16 : 0;
        const int target_y = (int)(MIGA80_INTRO_LOGO_Y - MIGA80_INTRO_STRIP_Y + y);
        unsigned int x;
        if ((intensity > 8U && ((noise >> 20) & 31U) < intensity / 2U) ||
            (tick >= 120U && ((noise >> 16) & 31U) < intensity - 1U)) { continue; }
        for (x = 0U; x < MIGA80_BOOT_LOGO_WIDTH; ++x) {
            const uint8_t color = miga80_boot_logo_pixels[y * MIGA80_BOOT_LOGO_WIDTH + x];
            const int target_x = (int)(MIGA80_INTRO_LOGO_X + x) + shift;
            if (color == 0U) { continue; }
            if (affected) {
                put(strip, target_x - 3, target_y, 4U);
                put(strip, target_x + 3, target_y, 5U);
            }
            put(strip, target_x, target_y, color);
        }
        if (affected && intensity > 12U && (noise & 7U) == 0U) {
            for (x = 0U; x < 6U + intensity / 2U; ++x) {
                put(strip, (int)((noise >> 9) & 255U) + (int)x, target_y, 4U);
            }
        }
    }
}
