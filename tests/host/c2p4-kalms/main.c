#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "graphics/c2p4_reference.h"

int main(void)
{
    uint8_t source[3 * 264], actual[4][3 * 36], expected[4][3 * 36];
    uint8_t *a[4], *e[4];
    unsigned int p, x, y;
    for (p = 0; p < 4; ++p) { a[p] = actual[p]; e[p] = expected[p]; }
    memset(source, 0xa5, sizeof(source));
    for (y = 0; y < 3; ++y) {
        for (x = 0; x < 256; ++x) { source[y * 264 + x] = (uint8_t)((x + y) & 15); }
    }
    memset(actual, 0x5a, sizeof(actual));
    memset(expected, 0x5a, sizeof(expected));
    assert(miga80_c2p4_kalms_color4(NULL, 256, 3, 264, a, 36) == MIGA80_C2P4_INVALID_ARGUMENT);
    assert(miga80_c2p4_kalms_color4(source, 256, 3, 264, NULL, 36) == MIGA80_C2P4_INVALID_ARGUMENT);
    a[2] = NULL;
    assert(miga80_c2p4_kalms_color4(source, 256, 3, 264, a, 36) == MIGA80_C2P4_INVALID_ARGUMENT);
    a[2] = actual[2];
    assert(miga80_c2p4_kalms_color4(source, 0, 3, 264, a, 36) == MIGA80_C2P4_INVALID_DIMENSIONS);
    assert(miga80_c2p4_kalms_color4(source, 256, 0, 264, a, 36) == MIGA80_C2P4_INVALID_DIMENSIONS);
    assert(miga80_c2p4_kalms_color4(source, 16, 3, 264, a, 36) == MIGA80_C2P4_INVALID_DIMENSIONS);
    assert(miga80_c2p4_kalms_color4(source, 256, 3, 255, a, 36) == MIGA80_C2P4_INVALID_STRIDE);
    assert(miga80_c2p4_kalms_color4(source, 256, 3, 264, a, 31) == MIGA80_C2P4_INVALID_STRIDE);
    assert(miga80_c2p4_kalms_color4(source, 256, SIZE_MAX, 264, a, 36) == MIGA80_C2P4_INVALID_STRIDE);
    assert(miga80_c2p4_kalms_color4(source, 32, 2, 32, a, SIZE_MAX) == MIGA80_C2P4_INVALID_STRIDE);
    assert(memcmp(actual, expected, sizeof(actual)) == 0);
    assert(miga80_c2p4_kalms_color4(source, 256, 3, 264, a, 36) == MIGA80_C2P4_OK);
    assert(miga80_c2p4_reference_byte4(source, 256, 3, 264, e, 36) == MIGA80_C2P4_OK);
    assert(memcmp(actual, expected, sizeof(actual)) == 0);
    puts("PASS  Kalms wrapper validation, unchanged output on rejection, colors and padding (host fallback)");
    return 0;
}
