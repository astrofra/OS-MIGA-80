#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "audio/boot_jingle.h"
#include "demo/intro_effect.h"
#include "boot_logo_data.h"

static uint32_t checksum(const void *data, size_t size)
{
    const uint8_t *p = data;
    uint32_t hash = 2166136261U;
    while (size-- != 0U) { hash = (hash ^ *p++) * 16777619U; }
    return hash;
}

static FILE *output(const char *directory, const char *name)
{
    char path[1024];
    FILE *file;
    assert(snprintf(path, sizeof(path), "%s/%s", directory, name) < (int)sizeof(path));
    file = fopen(path, "wb");
    assert(file != NULL);
    return file;
}

int main(int argc, char **argv)
{
    uint8_t image[MIGA80_INTRO_STRIP_BYTES + 32U];
    int8_t audio[MIGA80_BOOT_JINGLE_SAMPLES + 32U];
    uint32_t clean, enter, leave;
    unsigned int tick, x, y, i;
    FILE *frames, *pcm, *palette;
    assert(argc == 2);
    frames = output(argv[1], "intro-strip-frames.bin");
    pcm = output(argv[1], "boot-jingle.s8");
    palette = output(argv[1], "intro-palette.bin");
    memset(image, 0xa5, sizeof(image));
    miga80_intro_render(image + 16U, 60U);
    clean = checksum(image + 16U, MIGA80_INTRO_STRIP_BYTES);
    for (y = 0U; y < MIGA80_INTRO_STRIP_HEIGHT; ++y) {
        for (x = 0U; x < MIGA80_INTRO_WIDTH; ++x) {
            uint8_t expected = 0U;
            if (x >= 30U && x < 226U && y >= 10U && y < 53U) {
                expected = miga80_boot_logo_pixels[(y - 10U) * 196U + x - 30U];
            }
            assert(image[16U + y * 256U + x] == expected);
        }
    }
    miga80_intro_render(image + 16U, 14U);
    enter = checksum(image + 16U, MIGA80_INTRO_STRIP_BYTES);
    miga80_intro_render(image + 16U, 138U);
    leave = checksum(image + 16U, MIGA80_INTRO_STRIP_BYTES);
    assert(clean != enter && clean != leave && enter != leave);
    for (tick = 0U; tick < MIGA80_INTRO_TICKS; ++tick) {
        miga80_intro_render(image + 16U, tick);
        for (i = 0U; i < 16U; ++i) {
            assert(image[i] == 0xa5U && image[sizeof(image) - 1U - i] == 0xa5U);
        }
        for (i = 0U; i < MIGA80_INTRO_STRIP_BYTES; ++i) {
            assert(image[16U + i] < 6U);
            if (tick == 0U || tick >= 156U) { assert(image[16U + i] == 0U); }
        }
        assert(fwrite(image + 16U, 1U, MIGA80_INTRO_STRIP_BYTES, frames) == MIGA80_INTRO_STRIP_BYTES);
    }
    memset(audio, 0x5a, sizeof(audio));
    miga80_boot_jingle_generate(audio + 16U);
    for (i = 0U; i < 16U; ++i) {
        assert(audio[i] == 0x5a && audio[sizeof(audio) - 1U - i] == 0x5a);
    }
    for (i = 0U; i < MIGA80_BOOT_JINGLE_SAMPLES; ++i) {
        assert(audio[16U + i] > -110 && audio[16U + i] < 110);
        if (i < 1764U || i >= 30870U) { assert(audio[16U + i] == 0); }
    }
    assert(fwrite(audio + 16U, 1U, MIGA80_BOOT_JINGLE_SAMPLES, pcm) == MIGA80_BOOT_JINGLE_SAMPLES);
    printf("pcm_checksum=%08x\n", (unsigned int)checksum(audio + 16U, MIGA80_BOOT_JINGLE_SAMPLES));
    for (i = 0U; i < 16U; ++i) {
        const uint32_t rgb = miga80_intro_rgb(i);
        const uint8_t channels[3] = {(uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb};
        assert(fwrite(channels, 1U, 3U, palette) == 3U);
    }
    assert(fclose(frames) == 0 && fclose(pcm) == 0 && fclose(palette) == 0);
    puts("PASS centered native logo, bounded glitches, silence and unclipped integer PCM");
    return 0;
}
