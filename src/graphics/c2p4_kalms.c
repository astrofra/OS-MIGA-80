#include "graphics/c2p4_reference.h"

#if defined(__m68k__)
extern void miga80_c2p4_kalms_color4_core(const uint8_t *chunky,
    size_t width, size_t height, size_t source_skip,
    uint8_t *planes[4], size_t destination_skip);
#endif

enum Miga80C2P4Status miga80_c2p4_kalms_color4(
    const uint8_t *chunky, size_t width, size_t height, size_t chunky_stride,
    uint8_t *planes[4], size_t plane_stride)
{
    size_t plane;
    if (chunky == NULL || planes == NULL) { return MIGA80_C2P4_INVALID_ARGUMENT; }
    for (plane = 0U; plane < 4U; ++plane) {
        if (planes[plane] == NULL) { return MIGA80_C2P4_INVALID_ARGUMENT; }
    }
    if (width == 0U || height == 0U || (width & 31U) != 0U) {
        return MIGA80_C2P4_INVALID_DIMENSIONS;
    }
    if (chunky_stride < width || plane_stride < (width >> 3) ||
        height > SIZE_MAX / chunky_stride || height > SIZE_MAX / plane_stride) {
        return MIGA80_C2P4_INVALID_STRIDE;
    }
#if defined(__m68k__)
    /* A tight surface can use one pipeline across all rows, as upstream. */
    if (chunky_stride == width && plane_stride == (width >> 3)) {
        miga80_c2p4_kalms_color4_core(chunky, width * height, 1U, 0U, planes, 0U);
    } else {
        miga80_c2p4_kalms_color4_core(chunky, width, height,
            chunky_stride - width, planes, plane_stride - (width >> 3));
    }
    return MIGA80_C2P4_OK;
#else
    return miga80_c2p4_reference_byte4(chunky, width, height, chunky_stride,
                                     planes, plane_stride);
#endif
}
