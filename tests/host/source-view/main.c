#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui/palette.h"
#include "ui/source_view.h"

#define SOURCE_LIMIT 4096U

static uint8_t framebuffer[MIGA80_SOURCE_VIEW_WIDTH *
                           MIGA80_SOURCE_VIEW_HEIGHT];
static uint8_t planar_bit_0[MIGA80_SOURCE_VIEW_PLANAR_BYTES_PER_ROW *
                            MIGA80_SOURCE_VIEW_HEIGHT];
static uint8_t planar_bit_1[MIGA80_SOURCE_VIEW_PLANAR_BYTES_PER_ROW *
                            MIGA80_SOURCE_VIEW_HEIGHT];

static int read_source(const char *path, char *source, size_t *source_size)
{
    FILE *input = fopen(path, "rb");
    size_t count;

    if (input == NULL) {
        return 0;
    }
    count = fread(source, 1U, SOURCE_LIMIT + 1U, input);
    if (ferror(input) || fclose(input) != 0 || count > SOURCE_LIMIT) {
        return 0;
    }
    *source_size = count;
    return 1;
}

static uint8_t expand_nibble(uint16_t value)
{
    value &= 0x0fU;
    return (uint8_t)((value << 4) | value);
}

static int write_preview(const char *path)
{
    FILE *output = fopen(path, "wb");
    size_t index;

    if (output == NULL ||
        fprintf(output, "P6\n%u %u\n255\n", MIGA80_SOURCE_VIEW_WIDTH,
                MIGA80_SOURCE_VIEW_HEIGHT) < 0) {
        if (output != NULL) {
            (void)fclose(output);
        }
        return 0;
    }
    for (index = 0U; index < sizeof(framebuffer); ++index) {
        const uint16_t rgb12 = miga80_workbench_sunset_rgb12[
            framebuffer[index] & 0x0fU];
        const uint8_t rgb[3] = {
            expand_nibble((uint16_t)(rgb12 >> 8)),
            expand_nibble((uint16_t)(rgb12 >> 4)),
            expand_nibble(rgb12)
        };

        if (fwrite(rgb, 1U, sizeof(rgb), output) != sizeof(rgb)) {
            (void)fclose(output);
            return 0;
        }
    }
    return fclose(output) == 0;
}

static int verify_rejections(void)
{
    struct Miga80SourceViewMetrics metrics;
    char too_wide[MIGA80_SOURCE_VIEW_COLUMNS + 2U];
    char too_tall[(MIGA80_SOURCE_VIEW_SOURCE_ROWS * 2U) + 2U];

    (void)memset(too_wide, 'x', sizeof(too_wide));
    too_wide[sizeof(too_wide) - 1U] = '\0';
    (void)memset(too_tall, '\n', sizeof(too_tall));
    too_tall[sizeof(too_tall) - 1U] = '\0';

    return miga80_source_view_render(
               framebuffer, MIGA80_SOURCE_VIEW_WIDTH - 1U, "x", 1U,
               &metrics) == MIGA80_SOURCE_VIEW_INVALID_STRIDE &&
           miga80_source_view_render(
               framebuffer, MIGA80_SOURCE_VIEW_WIDTH, too_wide,
               sizeof(too_wide) - 1U,
               &metrics) == MIGA80_SOURCE_VIEW_LINE_TOO_LONG &&
           miga80_source_view_render(
               framebuffer, MIGA80_SOURCE_VIEW_WIDTH, too_tall,
               sizeof(too_tall) - 1U,
               &metrics) == MIGA80_SOURCE_VIEW_TOO_MANY_LINES &&
           miga80_source_view_render(
               framebuffer, MIGA80_SOURCE_VIEW_WIDTH, "\t", 1U,
               &metrics) == MIGA80_SOURCE_VIEW_INVALID_CHARACTER;
}

static int verify_editor_view(void)
{
    struct Miga80SourceViewMetrics metrics;
    char source[256];
    size_t offset = 0U, line;

    (void)memset(source, 'x', 70U);
    offset = 70U;
    source[offset++] = '\t';
    source[offset++] = '\n';
    for (line = 0U; line < 31U; ++line) {
        source[offset++] = 'y';
        source[offset++] = '\n';
    }
    return miga80_source_view_render_editor(framebuffer,
               MIGA80_SOURCE_VIEW_WIDTH, source, offset, 90U, 92U, 10U, 0U,
               "EDITABLE", "READY L4:C1", &metrics) ==
               MIGA80_SOURCE_VIEW_OK &&
           metrics.source_bytes == offset && metrics.source_lines == 32U &&
           metrics.maximum_columns == 72U &&
           framebuffer[8U * MIGA80_SOURCE_VIEW_WIDTH] == 2U;
}

static unsigned int planar_pixel(size_t x, size_t y)
{
    const size_t byte = y * MIGA80_SOURCE_VIEW_PLANAR_BYTES_PER_ROW + x / 8U;
    const uint8_t mask = (uint8_t)(0x80U >> (x & 7U));

    return ((planar_bit_0[byte] & mask) != 0U ? 1U : 0U) |
           ((planar_bit_1[byte] & mask) != 0U ? 2U : 0U);
}

static int verify_planar_editor_view(void)
{
    struct Miga80SourceViewMetrics metrics;
    const char source[] = "ab\ncd";
    size_t x, y;
    int normal_ink = 0, cursor_ink = 0;

    if (miga80_source_view_measure_editor(source, sizeof(source) - 1U,
                                          &metrics) !=
            MIGA80_SOURCE_VIEW_OK ||
        metrics.source_lines != 2U || metrics.maximum_columns != 2U ||
        miga80_source_view_render_editor_planar(
            planar_bit_0, planar_bit_1,
            MIGA80_SOURCE_VIEW_PLANAR_BYTES_PER_ROW, source,
            sizeof(source) - 1U, 1U, (size_t)-1, 0U, 0U,
            "EDITABLE", "READY") != MIGA80_SOURCE_VIEW_OK ||
        planar_pixel(252U, 0U) != 1U || planar_pixel(252U, 8U) != 0U) {
        return 0;
    }
    for (y = 8U; y < 16U; ++y) {
        for (x = 0U; x < 4U; ++x) {
            const unsigned int color = planar_pixel(x, y);
            if (color == 2U) { normal_ink = 1; }
            if (color != 0U && color != 2U) { return 0; }
        }
        for (x = 4U; x < 8U; ++x) {
            const unsigned int color = planar_pixel(x, y);
            if (color == 3U) { cursor_ink = 1; }
            if (color != 1U && color != 3U) { return 0; }
        }
    }
    if (!normal_ink || !cursor_ink) { return 0; }

    (void)memset(planar_bit_0, 0xa5, sizeof(planar_bit_0));
    (void)memset(planar_bit_1, 0x5a, sizeof(planar_bit_1));
    if (miga80_source_view_render_editor_planar_rows(
            planar_bit_0, planar_bit_1,
            MIGA80_SOURCE_VIEW_PLANAR_BYTES_PER_ROW, source,
            sizeof(source) - 1U, 0U, (size_t)-1, 0U, 0U,
            "EDITABLE", "READY", 1U, 1U) != MIGA80_SOURCE_VIEW_OK) {
        return 0;
    }
    for (x = 0U; x < MIGA80_SOURCE_VIEW_PLANAR_BYTES_PER_ROW * 8U; ++x) {
        if (planar_bit_0[x] != 0xa5U || planar_bit_1[x] != 0x5aU ||
            planar_bit_0[x + MIGA80_SOURCE_VIEW_PLANAR_BYTES_PER_ROW * 16U] !=
                0xa5U ||
            planar_bit_1[x + MIGA80_SOURCE_VIEW_PLANAR_BYTES_PER_ROW * 16U] !=
                0x5aU) {
            return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv)
{
    char source[SOURCE_LIMIT + 1U];
    size_t source_size;
    struct Miga80SourceViewMetrics metrics;

    if (argc != 3) {
        fprintf(stderr, "usage: %s source.lua preview.ppm\n", argv[0]);
        return 2;
    }
    if (!read_source(argv[1], source, &source_size)) {
        fprintf(stderr, "unable to read source fixture\n");
        return 1;
    }
    if (miga80_source_view_render(framebuffer, MIGA80_SOURCE_VIEW_WIDTH,
                                  source, source_size, &metrics) !=
            MIGA80_SOURCE_VIEW_OK ||
        metrics.source_lines != MIGA80_SOURCE_VIEW_SOURCE_ROWS ||
        metrics.maximum_columns > MIGA80_SOURCE_VIEW_COLUMNS ||
        !verify_rejections() ||
        !verify_editor_view() ||
        !verify_planar_editor_view() ||
        miga80_source_view_render(framebuffer, MIGA80_SOURCE_VIEW_WIDTH,
                                  source, source_size, &metrics) !=
            MIGA80_SOURCE_VIEW_OK ||
        !write_preview(argv[2])) {
        fprintf(stderr, "source-view regression failed\n");
        return 1;
    }

    printf("font_cell=4x8\n");
    printf("source_bytes=%lu\n", (unsigned long)metrics.source_bytes);
    printf("source_lines=%lu\n", (unsigned long)metrics.source_lines);
    printf("maximum_columns=%lu\n", (unsigned long)metrics.maximum_columns);
    printf("source_checksum=%08lx\n",
           (unsigned long)metrics.source_checksum);
    printf("framebuffer_checksum=%08lx\n",
           (unsigned long)metrics.framebuffer_checksum);
    printf("rejections=pass\n");
    printf("result=pass\n");
    return 0;
}
