#include "ui/source_view.h"

#include <string.h>

#include "font4x8_data.h"

enum {
    VIEW_COLOR_BACKGROUND = 0,
    VIEW_COLOR_HEADER_BACKGROUND = 2,
    VIEW_COLOR_HEADER_TEXT = 9,
    VIEW_COLOR_SOURCE_TEXT = 8,
    VIEW_COLOR_STATUS_BACKGROUND = 14,
    VIEW_COLOR_STATUS_TEXT = 9
};

static const char view_title[] = "MIGA-80 / DEFAULT.LUA / READ ONLY";
static const char view_status[] = "SOURCE READY - F5 RUN - ESC EXIT";

uint32_t miga80_source_view_checksum(const void *bytes, size_t size)
{
    const uint8_t *cursor = (const uint8_t *)bytes;
    uint32_t hash = UINT32_C(2166136261);
    size_t index;

    for (index = 0U; index < size; ++index) {
        hash ^= cursor[index];
        hash *= UINT32_C(16777619);
    }
    return hash;
}

static void fill_rows(uint8_t *pixels, size_t stride, size_t first_y,
                      size_t row_count, uint8_t color)
{
    size_t y;

    for (y = first_y; y < first_y + row_count; ++y) {
        (void)memset(pixels + (y * stride), color,
                     MIGA80_SOURCE_VIEW_WIDTH);
    }
}

static const uint8_t *glyph_for(unsigned char character)
{
    if (character < MIGA80_FONT4X8_FIRST ||
        character > MIGA80_FONT4X8_LAST) {
        character = (unsigned char)'?';
    }
    return miga80_font4x8_ascii[character - MIGA80_FONT4X8_FIRST];
}

static void draw_character(uint8_t *pixels, size_t stride, size_t column,
                           size_t row, unsigned char character, uint8_t color)
{
    const uint8_t *glyph = glyph_for(character);
    const size_t origin_x = column * MIGA80_FONT4X8_WIDTH;
    const size_t origin_y = row * MIGA80_FONT4X8_HEIGHT;
    size_t glyph_y;

    for (glyph_y = 0U; glyph_y < MIGA80_FONT4X8_HEIGHT; ++glyph_y) {
        size_t glyph_x;

        for (glyph_x = 0U; glyph_x < MIGA80_FONT4X8_WIDTH; ++glyph_x) {
            const uint8_t mask =
                (uint8_t)(1U << (MIGA80_FONT4X8_WIDTH - 1U - glyph_x));

            if ((glyph[glyph_y] & mask) != 0U) {
                pixels[((origin_y + glyph_y) * stride) + origin_x + glyph_x] =
                    color;
            }
        }
    }
}

static void draw_text(uint8_t *pixels, size_t stride, size_t column,
                      size_t row, const char *text, size_t length,
                      uint8_t color)
{
    size_t index;

    for (index = 0U; index < length; ++index) {
        draw_character(pixels, stride, column + index, row,
                       (unsigned char)text[index], color);
    }
}

static enum Miga80SourceViewStatus validate_source(
    const char *source, size_t source_size, struct Miga80SourceViewMetrics *metrics)
{
    size_t index;
    size_t columns = 0U;
    size_t maximum_columns = 0U;
    size_t lines = source_size == 0U ? 0U : 1U;

    for (index = 0U; index < source_size; ++index) {
        const unsigned char character = (unsigned char)source[index];

        if (character == (unsigned char)'\n') {
            if (columns > maximum_columns) {
                maximum_columns = columns;
            }
            columns = 0U;
            if (index + 1U < source_size) {
                ++lines;
            }
        } else {
            if (character < 0x20U || character > 0x7eU) {
                return MIGA80_SOURCE_VIEW_INVALID_CHARACTER;
            }
            ++columns;
            if (columns > MIGA80_SOURCE_VIEW_COLUMNS) {
                return MIGA80_SOURCE_VIEW_LINE_TOO_LONG;
            }
        }
    }
    if (columns > maximum_columns) {
        maximum_columns = columns;
    }
    if (lines > MIGA80_SOURCE_VIEW_SOURCE_ROWS) {
        return MIGA80_SOURCE_VIEW_TOO_MANY_LINES;
    }

    metrics->source_bytes = source_size;
    metrics->source_lines = lines;
    metrics->maximum_columns = maximum_columns;
    metrics->source_checksum =
        miga80_source_view_checksum(source, source_size);
    return MIGA80_SOURCE_VIEW_OK;
}

enum Miga80SourceViewStatus miga80_source_view_draw_status(
    uint8_t *pixels, size_t stride, const char *status_text)
{
    size_t length;
    size_t index;

    if (pixels == NULL || status_text == NULL) {
        return MIGA80_SOURCE_VIEW_INVALID_ARGUMENT;
    }
    if (stride < MIGA80_SOURCE_VIEW_WIDTH) {
        return MIGA80_SOURCE_VIEW_INVALID_STRIDE;
    }
    length = strlen(status_text);
    if (length > MIGA80_SOURCE_VIEW_COLUMNS) {
        return MIGA80_SOURCE_VIEW_LINE_TOO_LONG;
    }
    for (index = 0U; index < length; ++index) {
        const unsigned char character = (unsigned char)status_text[index];

        if (character < 0x20U || character > 0x7eU) {
            return MIGA80_SOURCE_VIEW_INVALID_CHARACTER;
        }
    }
    fill_rows(pixels, stride,
              MIGA80_SOURCE_VIEW_HEIGHT - MIGA80_FONT4X8_HEIGHT,
              MIGA80_FONT4X8_HEIGHT, VIEW_COLOR_STATUS_BACKGROUND);
    draw_text(pixels, stride, 0U, MIGA80_SOURCE_VIEW_ROWS - 1U,
              status_text, length, VIEW_COLOR_STATUS_TEXT);
    return MIGA80_SOURCE_VIEW_OK;
}

enum Miga80SourceViewStatus miga80_source_view_render_with_status(
    uint8_t *pixels, size_t stride, const char *source, size_t source_size,
    const char *status_text, struct Miga80SourceViewMetrics *metrics)
{
    enum Miga80SourceViewStatus status;
    size_t index;
    size_t line_start = 0U;
    size_t row = 1U;

    if (pixels == NULL || source == NULL || status_text == NULL ||
        metrics == NULL) {
        return MIGA80_SOURCE_VIEW_INVALID_ARGUMENT;
    }
    if (stride < MIGA80_SOURCE_VIEW_WIDTH) {
        return MIGA80_SOURCE_VIEW_INVALID_STRIDE;
    }

    status = validate_source(source, source_size, metrics);
    if (status != MIGA80_SOURCE_VIEW_OK) {
        return status;
    }

    fill_rows(pixels, stride, 0U, MIGA80_SOURCE_VIEW_HEIGHT,
              VIEW_COLOR_BACKGROUND);
    fill_rows(pixels, stride, 0U, MIGA80_FONT4X8_HEIGHT,
              VIEW_COLOR_HEADER_BACKGROUND);
    fill_rows(pixels, stride,
              MIGA80_SOURCE_VIEW_HEIGHT - MIGA80_FONT4X8_HEIGHT,
              MIGA80_FONT4X8_HEIGHT, VIEW_COLOR_STATUS_BACKGROUND);
    draw_text(pixels, stride, 0U, 0U, view_title, sizeof(view_title) - 1U,
              VIEW_COLOR_HEADER_TEXT);

    for (index = 0U; index <= source_size; ++index) {
        if (index == source_size || source[index] == '\n') {
            if (index > line_start) {
                draw_text(pixels, stride, 0U, row, source + line_start,
                          index - line_start, VIEW_COLOR_SOURCE_TEXT);
            }
            ++row;
            line_start = index + 1U;
        }
    }

    status = miga80_source_view_draw_status(pixels, stride, status_text);
    if (status != MIGA80_SOURCE_VIEW_OK) {
        return status;
    }
    metrics->framebuffer_checksum = miga80_source_view_checksum(
        pixels, stride * MIGA80_SOURCE_VIEW_HEIGHT);
    return MIGA80_SOURCE_VIEW_OK;
}

enum Miga80SourceViewStatus miga80_source_view_render(
    uint8_t *pixels, size_t stride, const char *source, size_t source_size,
    struct Miga80SourceViewMetrics *metrics)
{
    return miga80_source_view_render_with_status(
        pixels, stride, source, source_size, view_status, metrics);
}
