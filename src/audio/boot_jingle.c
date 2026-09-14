#include "audio/boot_jingle.h"
#include <string.h>

static int16_t triangle(uint16_t phase)
{
    uint16_t value = phase >> 8;
    if (value >= 128U) { value = 255U - value; }
    return (int16_t)(value * 2U) - 127;
}

void miga80_boot_jingle_generate(int8_t *samples)
{
    /* A major / E major / A major. The strongest voice sings A4, E5, C#5:
     * low -> high -> middle, the three attacks of "A - MI - GA".
     * Phase steps use nominal 11025 Hz; PAL Paula at period 322 is 11015 Hz. */
    static const uint16_t steps[3][4] = {
        {1308U, 1648U, 1960U, 2615U},
        {980U, 1468U, 2469U, 3920U},
        {1308U, 1960U, 2615U, 3295U}
    };
    static const uint16_t starts[3] = {1764U, 9702U, 17640U};
    static const uint16_t lengths[3] = {6174U, 6174U, 13230U};
    unsigned int chord;
    (void)memset(samples, 0, MIGA80_BOOT_JINGLE_SAMPLES);
    for (chord = 0U; chord < 3U; ++chord) {
        uint16_t phase[4] = {0U, 0U, 0U, 0U};
        unsigned int age;
        for (age = 0U; age < lengths[chord]; ++age) {
            int16_t mix, envelope, release;
            unsigned int voice;
            for (voice = 0U; voice < 4U; ++voice) {
                phase[voice] = (uint16_t)(phase[voice] + steps[chord][voice]);
            }
            mix = (int16_t)((triangle(phase[0]) + triangle(phase[1]) +
                triangle(phase[2]) + 3 * triangle(phase[3])) / 8);
            /* A restrained odd harmonic gives the lead an arcade timbre. */
            mix += phase[3] < 32768U ? 9 : -9;
            envelope = age < 128U ? (int16_t)(age * 2U) :
                (int16_t)(240 - (int)(age >> 5));
            if (age >= 128U && envelope < 120) { envelope = 120; }
            release = (int16_t)((lengths[chord] - age - 1U) >> (chord == 2U ? 4 : 2));
            if (release < envelope) { envelope = release; }
            samples[starts[chord] + age] = (int8_t)((mix * envelope) / 256);
        }
    }
}
