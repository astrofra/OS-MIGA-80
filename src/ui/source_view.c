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

static const char view_title[] = "MIGA-80 / LUA SOURCE / READ ONLY";
static const char view_status[] = "SOURCE READY - F5 RUN - CTRL-Q EXIT";

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

static void fill_cell(uint8_t *pixels, size_t stride, size_t column,
                      size_t row, uint8_t color)
{
    size_t y;
    const size_t origin_x = column * MIGA80_FONT4X8_WIDTH;
    const size_t origin_y = row * MIGA80_FONT4X8_HEIGHT;

    for (y = 0U; y < MIGA80_FONT4X8_HEIGHT; ++y) {
        (void)memset(pixels + ((origin_y + y) * stride) + origin_x,
                     color, MIGA80_FONT4X8_WIDTH);
    }
}

void miga80_source_view_draw_row(uint8_t *pixels, size_t stride, size_t row,
    const char *text, uint8_t foreground, uint8_t background)
{
    size_t length = 0U;
    if (pixels == NULL || text == NULL || stride < MIGA80_SOURCE_VIEW_WIDTH ||
        row >= MIGA80_SOURCE_VIEW_ROWS) {
        return;
    }
    while (length < MIGA80_SOURCE_VIEW_COLUMNS && text[length] != '\0') {
        ++length;
    }
    fill_rows(pixels, stride, row * MIGA80_FONT4X8_HEIGHT,
              MIGA80_FONT4X8_HEIGHT, background & 15U);
    draw_text(pixels, stride, 0U, row, text, length, foreground & 15U);
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

enum Miga80SourceViewStatus miga80_source_view_measure_editor(
    const char *source, size_t source_size,
    struct Miga80SourceViewMetrics *metrics)
{
    size_t index, columns = 0U, maximum_columns = 0U;
    size_t lines = source_size == 0U ? 0U : 1U;

    if (source == NULL || metrics == NULL) {
        return MIGA80_SOURCE_VIEW_INVALID_ARGUMENT;
    }

    for (index = 0U; index < source_size; ++index) {
        const unsigned char character = (unsigned char)source[index];

        if (character == (unsigned char)'\n') {
            if (columns > maximum_columns) { maximum_columns = columns; }
            columns = 0U;
            if (index + 1U < source_size) { ++lines; }
        } else if (character == (unsigned char)'\t') {
            columns += 4U - (columns % 4U);
        } else if (character >= 0x20U && character <= 0x7eU) {
            ++columns;
        } else {
            return MIGA80_SOURCE_VIEW_INVALID_CHARACTER;
        }
    }
    if (columns > maximum_columns) { maximum_columns = columns; }
    metrics->source_bytes = source_size;
    metrics->source_lines = lines;
    metrics->maximum_columns = maximum_columns;
    metrics->source_checksum = miga80_source_view_checksum(source, source_size);
    metrics->framebuffer_checksum = 0U;
    return MIGA80_SOURCE_VIEW_OK;
}

enum Miga80SourceViewStatus miga80_source_view_render_editor(
    uint8_t *pixels, size_t stride, const char *source, size_t source_size,
    size_t cursor, size_t anchor, size_t first_line, size_t first_column,
    const char *title_text, const char *status_text,
    struct Miga80SourceViewMetrics *metrics)
{
    enum Miga80SourceViewStatus status;
    size_t selection_start, selection_end, offset = 0U, line = 0U, row;

    if (pixels == NULL || source == NULL || title_text == NULL ||
        status_text == NULL || metrics == NULL || cursor > source_size ||
        (anchor != (size_t)-1 && anchor > source_size)) {
        return MIGA80_SOURCE_VIEW_INVALID_ARGUMENT;
    }
    if (stride < MIGA80_SOURCE_VIEW_WIDTH) {
        return MIGA80_SOURCE_VIEW_INVALID_STRIDE;
    }
    status = miga80_source_view_measure_editor(source, source_size, metrics);
    if (status != MIGA80_SOURCE_VIEW_OK) { return status; }

    selection_start = anchor == (size_t)-1 || anchor == cursor
                          ? cursor : anchor < cursor ? anchor : cursor;
    selection_end = anchor == (size_t)-1 || anchor == cursor
                        ? cursor : anchor > cursor ? anchor : cursor;
    fill_rows(pixels, stride, 0U, MIGA80_SOURCE_VIEW_HEIGHT,
              VIEW_COLOR_BACKGROUND);
    miga80_source_view_draw_row(pixels, stride, 0U, title_text,
                                VIEW_COLOR_HEADER_TEXT,
                                VIEW_COLOR_HEADER_BACKGROUND);
    miga80_source_view_draw_row(pixels, stride,
                                MIGA80_SOURCE_VIEW_ROWS - 1U, status_text,
                                VIEW_COLOR_STATUS_TEXT,
                                VIEW_COLOR_STATUS_BACKGROUND);

    while (line < first_line && offset < source_size) {
        if (source[offset++] == '\n') { ++line; }
    }
    for (row = 1U; row <= MIGA80_SOURCE_VIEW_SOURCE_ROWS; ++row) {
        size_t column = 0U;
        int ended = 0;

        if (line < first_line) { break; }
        while (!ended) {
            const int at_end = offset == source_size;
            const int at_newline = !at_end && source[offset] == '\n';
            const int selected = offset >= selection_start &&
                                 offset < selection_end;
            size_t width = 1U, cell;

            if (at_end || at_newline) {
                if (column >= first_column &&
                    column < first_column + MIGA80_SOURCE_VIEW_COLUMNS &&
                    (cursor == offset || selected)) {
                    const size_t visible = column - first_column;
                    fill_cell(pixels, stride, visible, row,
                              cursor == offset ? VIEW_COLOR_HEADER_BACKGROUND
                                               : VIEW_COLOR_STATUS_BACKGROUND);
                }
                if (at_end) { ended = 1; }
                else { ++offset; ++line; }
                break;
            }
            if (source[offset] == '\t') {
                width = 4U - (column % 4U);
            }
            for (cell = 0U; cell < width; ++cell) {
                const size_t logical_column = column + cell;
                if (logical_column >= first_column &&
                    logical_column < first_column +
                                         MIGA80_SOURCE_VIEW_COLUMNS) {
                    const size_t visible = logical_column - first_column;
                    const int cursor_cell = cursor == offset && cell == 0U;
                    if (selected || cursor_cell) {
                        fill_cell(pixels, stride, visible, row,
                                  cursor_cell
                                      ? VIEW_COLOR_HEADER_BACKGROUND
                                      : VIEW_COLOR_STATUS_BACKGROUND);
                    }
                    if (cell == 0U && source[offset] != '\t') {
                        draw_character(pixels, stride, visible, row,
                            (unsigned char)source[offset],
                            selected || cursor_cell
                                ? VIEW_COLOR_HEADER_TEXT
                                : VIEW_COLOR_SOURCE_TEXT);
                    }
                }
            }
            column += width;
            ++offset;
            if (column >= first_column + MIGA80_SOURCE_VIEW_COLUMNS &&
                cursor < offset && selection_end <= offset) {
                while (offset < source_size && source[offset] != '\n') {
                    ++offset;
                }
            }
        }
        if (offset == source_size && ended) { break; }
    }
    metrics->framebuffer_checksum = miga80_source_view_checksum(
        pixels, stride * MIGA80_SOURCE_VIEW_HEIGHT);
    return MIGA80_SOURCE_VIEW_OK;
}

enum {
    PLANAR_COLOR_BACKGROUND = 0,
    PLANAR_COLOR_ACCENT = 1,
    PLANAR_COLOR_SOURCE_TEXT = 2,
    PLANAR_COLOR_BRIGHT_TEXT = 3
};

static void planar_fill_text_row(uint8_t *color_bit_0, uint8_t *color_bit_1,
                                 size_t bytes_per_row, size_t row,
                                 uint8_t color)
{
    const size_t first_y = row * MIGA80_FONT4X8_HEIGHT;
    const int bit_0 = (color & 1U) != 0U;
    const int bit_1 = (color & 2U) != 0U;
    size_t y;

    for (y = first_y; y < first_y + MIGA80_FONT4X8_HEIGHT; ++y) {
        (void)memset(color_bit_0 + y * bytes_per_row,
                     bit_0 ? 0xff : 0x00,
                     MIGA80_SOURCE_VIEW_PLANAR_BYTES_PER_ROW);
        (void)memset(color_bit_1 + y * bytes_per_row,
                     bit_1 ? 0xff : 0x00,
                     MIGA80_SOURCE_VIEW_PLANAR_BYTES_PER_ROW);
    }
}

static void planar_write_nibble(uint8_t *plane, size_t bytes_per_row,
                                size_t column, size_t y, uint8_t bits)
{
    const size_t byte = column >> 1U;
    const unsigned int shift = (column & 1U) == 0U ? 4U : 0U;
    const uint8_t mask = (uint8_t)(0x0fU << shift);
    uint8_t *destination = plane + y * bytes_per_row + byte;

    *destination = (uint8_t)((*destination & (uint8_t)~mask) |
                             ((bits & 0x0fU) << shift));
}

static uint8_t planar_read_nibble(const uint8_t *plane, size_t bytes_per_row,
                                  size_t column, size_t y)
{
    const unsigned int shift = (column & 1U) == 0U ? 4U : 0U;

    return (uint8_t)((plane[y * bytes_per_row + (column >> 1U)] >> shift) &
                     0x0fU);
}

static void planar_fill_cell(uint8_t *color_bit_0, uint8_t *color_bit_1,
                             size_t bytes_per_row, size_t column, size_t row,
                             uint8_t color)
{
    const uint8_t bit_0 = (color & 1U) != 0U ? 0x0fU : 0U;
    const uint8_t bit_1 = (color & 2U) != 0U ? 0x0fU : 0U;
    const size_t first_y = row * MIGA80_FONT4X8_HEIGHT;
    size_t glyph_y;

    for (glyph_y = 0U; glyph_y < MIGA80_FONT4X8_HEIGHT; ++glyph_y) {
        planar_write_nibble(color_bit_0, bytes_per_row, column,
                            first_y + glyph_y, bit_0);
        planar_write_nibble(color_bit_1, bytes_per_row, column,
                            first_y + glyph_y, bit_1);
    }
}

static void planar_draw_character(uint8_t *color_bit_0, uint8_t *color_bit_1,
                                  size_t bytes_per_row, size_t column,
                                  size_t row, unsigned char character,
                                  uint8_t color)
{
    const uint8_t *glyph = glyph_for(character);
    const uint8_t bit_0 = (color & 1U) != 0U ? 0x0fU : 0U;
    const uint8_t bit_1 = (color & 2U) != 0U ? 0x0fU : 0U;
    const size_t first_y = row * MIGA80_FONT4X8_HEIGHT;
    size_t glyph_y;

    for (glyph_y = 0U; glyph_y < MIGA80_FONT4X8_HEIGHT; ++glyph_y) {
        const uint8_t set = glyph[glyph_y] & 0x0fU;
        uint8_t current;

        current = planar_read_nibble(color_bit_0, bytes_per_row, column,
                                     first_y + glyph_y);
        planar_write_nibble(color_bit_0, bytes_per_row, column,
                            first_y + glyph_y,
                            (uint8_t)((current & (uint8_t)~set) |
                                      (bit_0 & set)));
        current = planar_read_nibble(color_bit_1, bytes_per_row, column,
                                     first_y + glyph_y);
        planar_write_nibble(color_bit_1, bytes_per_row, column,
                            first_y + glyph_y,
                            (uint8_t)((current & (uint8_t)~set) |
                                      (bit_1 & set)));
    }
}

static void planar_draw_text(uint8_t *color_bit_0, uint8_t *color_bit_1,
                             size_t bytes_per_row, size_t row,
                             const char *text, uint8_t color)
{
    size_t column = 0U;

    while (column < MIGA80_SOURCE_VIEW_COLUMNS && text[column] != '\0') {
        planar_draw_character(color_bit_0, color_bit_1, bytes_per_row,
                              column, row, (unsigned char)text[column], color);
        ++column;
    }
}

static size_t planar_line_offset(const char *source, size_t source_size,
                                 size_t wanted_line, int *found)
{
    size_t offset = 0U, line = 0U;

    while (line < wanted_line && offset < source_size) {
        if (source[offset++] == '\n') { ++line; }
    }
    *found = line == wanted_line;
    return offset;
}

static void planar_render_source_row(
    uint8_t *color_bit_0, uint8_t *color_bit_1, size_t bytes_per_row,
    const char *source, size_t source_size, size_t cursor, size_t anchor,
    size_t offset, int found, size_t first_column, size_t screen_row,
    size_t *next_offset, int *next_found)
{
    const size_t selection_start =
        anchor == (size_t)-1 || anchor == cursor
            ? cursor : anchor < cursor ? anchor : cursor;
    const size_t selection_end =
        anchor == (size_t)-1 || anchor == cursor
            ? cursor : anchor > cursor ? anchor : cursor;
    size_t column = 0U;

    planar_fill_text_row(color_bit_0, color_bit_1, bytes_per_row, screen_row,
                         PLANAR_COLOR_BACKGROUND);
    if (!found) {
        *next_offset = offset;
        *next_found = 0;
        return;
    }
    for (;;) {
        const int at_end = offset == source_size;
        const int at_newline = !at_end && source[offset] == '\n';
        const int selected = offset >= selection_start &&
                             offset < selection_end;
        size_t width = 1U, cell;

        if (at_end || at_newline) {
            if (column >= first_column &&
                column < first_column + MIGA80_SOURCE_VIEW_COLUMNS &&
                (cursor == offset || selected)) {
                planar_fill_cell(color_bit_0, color_bit_1, bytes_per_row,
                                 column - first_column, screen_row,
                                 PLANAR_COLOR_ACCENT);
            }
            break;
        }
        if (source[offset] == '\t') {
            width = 4U - (column % 4U);
        }
        for (cell = 0U; cell < width; ++cell) {
            const size_t logical_column = column + cell;

            if (logical_column >= first_column &&
                logical_column < first_column +
                                     MIGA80_SOURCE_VIEW_COLUMNS) {
                const size_t visible = logical_column - first_column;
                const int cursor_cell = cursor == offset && cell == 0U;

                if (selected || cursor_cell) {
                    planar_fill_cell(color_bit_0, color_bit_1, bytes_per_row,
                                     visible, screen_row,
                                     PLANAR_COLOR_ACCENT);
                }
                if (cell == 0U && source[offset] != '\t') {
                    planar_draw_character(color_bit_0, color_bit_1,
                        bytes_per_row, visible, screen_row,
                        (unsigned char)source[offset],
                        selected || cursor_cell ? PLANAR_COLOR_BRIGHT_TEXT
                                                : PLANAR_COLOR_SOURCE_TEXT);
                }
            }
        }
        column += width;
        ++offset;
        if (column >= first_column + MIGA80_SOURCE_VIEW_COLUMNS &&
            cursor < offset && selection_end <= offset) {
            break;
        }
    }
    while (offset < source_size && source[offset] != '\n') { ++offset; }
    if (offset < source_size) {
        *next_offset = offset + 1U;
        *next_found = 1;
    } else {
        *next_offset = offset;
        *next_found = 0;
    }
}

enum Miga80SourceViewStatus miga80_source_view_render_editor_planar_rows(
    uint8_t *color_bit_0, uint8_t *color_bit_1, size_t bytes_per_row,
    const char *source, size_t source_size, size_t cursor, size_t anchor,
    size_t first_line, size_t first_column, const char *title_text,
    const char *status_text, size_t first_row, size_t row_count)
{
    const size_t last_row = first_row + row_count;
    size_t first_source_row, source_offset = 0U, row;
    int source_found = 0;

    if (color_bit_0 == NULL || color_bit_1 == NULL || source == NULL ||
        title_text == NULL || status_text == NULL || cursor > source_size ||
        (anchor != (size_t)-1 && anchor > source_size) ||
        first_row > MIGA80_SOURCE_VIEW_ROWS ||
        row_count > MIGA80_SOURCE_VIEW_ROWS - first_row) {
        return MIGA80_SOURCE_VIEW_INVALID_ARGUMENT;
    }
    if (bytes_per_row < MIGA80_SOURCE_VIEW_PLANAR_BYTES_PER_ROW) {
        return MIGA80_SOURCE_VIEW_INVALID_STRIDE;
    }
    first_source_row = first_row < 1U ? 1U : first_row;
    if (first_source_row < last_row &&
        first_source_row < MIGA80_SOURCE_VIEW_ROWS - 1U) {
        source_offset = planar_line_offset(source, source_size,
            first_line + first_source_row - 1U, &source_found);
    }
    for (row = first_row; row < last_row; ++row) {
        if (row == 0U) {
            planar_fill_text_row(color_bit_0, color_bit_1, bytes_per_row,
                                 row, PLANAR_COLOR_ACCENT);
            planar_draw_text(color_bit_0, color_bit_1, bytes_per_row, row,
                             title_text, PLANAR_COLOR_BRIGHT_TEXT);
        } else if (row == MIGA80_SOURCE_VIEW_ROWS - 1U) {
            planar_fill_text_row(color_bit_0, color_bit_1, bytes_per_row,
                                 row, PLANAR_COLOR_ACCENT);
            planar_draw_text(color_bit_0, color_bit_1, bytes_per_row, row,
                             status_text, PLANAR_COLOR_BRIGHT_TEXT);
        } else {
            planar_render_source_row(color_bit_0, color_bit_1, bytes_per_row,
                source, source_size, cursor, anchor,
                source_offset, source_found, first_column, row,
                &source_offset, &source_found);
        }
    }
    return MIGA80_SOURCE_VIEW_OK;
}

enum Miga80SourceViewStatus miga80_source_view_render_editor_planar(
    uint8_t *color_bit_0, uint8_t *color_bit_1, size_t bytes_per_row,
    const char *source, size_t source_size, size_t cursor, size_t anchor,
    size_t first_line, size_t first_column, const char *title_text,
    const char *status_text)
{
    return miga80_source_view_render_editor_planar_rows(
        color_bit_0, color_bit_1, bytes_per_row, source, source_size, cursor,
        anchor, first_line, first_column, title_text, status_text, 0U,
        MIGA80_SOURCE_VIEW_ROWS);
}
