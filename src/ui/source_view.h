#ifndef MIGA80_UI_SOURCE_VIEW_H
#define MIGA80_UI_SOURCE_VIEW_H

#include <stddef.h>
#include <stdint.h>

enum {
    MIGA80_SOURCE_VIEW_WIDTH = 256,
    MIGA80_SOURCE_VIEW_HEIGHT = 256,
    MIGA80_SOURCE_VIEW_COLUMNS = 64,
    MIGA80_SOURCE_VIEW_ROWS = 32,
    MIGA80_SOURCE_VIEW_SOURCE_ROWS = 30,
    MIGA80_SOURCE_VIEW_CELL_HEIGHT = 8,
    MIGA80_SOURCE_VIEW_PLANAR_BYTES_PER_ROW = 32
};

enum Miga80SourceViewStatus {
    MIGA80_SOURCE_VIEW_OK = 0,
    MIGA80_SOURCE_VIEW_INVALID_ARGUMENT,
    MIGA80_SOURCE_VIEW_INVALID_STRIDE,
    MIGA80_SOURCE_VIEW_TOO_MANY_LINES,
    MIGA80_SOURCE_VIEW_LINE_TOO_LONG,
    MIGA80_SOURCE_VIEW_INVALID_CHARACTER
};

struct Miga80SourceViewMetrics {
    size_t source_bytes;
    size_t source_lines;
    size_t maximum_columns;
    uint32_t source_checksum;
    uint32_t framebuffer_checksum;
};

enum Miga80SourceViewStatus miga80_source_view_render(
    uint8_t *pixels, size_t stride, const char *source, size_t source_size,
    struct Miga80SourceViewMetrics *metrics);
enum Miga80SourceViewStatus miga80_source_view_render_with_status(
    uint8_t *pixels, size_t stride, const char *source, size_t source_size,
    const char *status_text, struct Miga80SourceViewMetrics *metrics);
/* Scrolling editable-document view for the current 256x256 LORES UI. */
enum Miga80SourceViewStatus miga80_source_view_render_editor(
    uint8_t *pixels, size_t stride, const char *source, size_t source_size,
    size_t cursor, size_t anchor, size_t first_line, size_t first_column,
    const char *title_text, const char *status_text,
    struct Miga80SourceViewMetrics *metrics);

/*
 * Direct two-plane renderer for the interactive LORES editor.  The returned
 * two-bit pixels are deliberately abstract: the caller chooses which two
 * physical PF1 planes carry bit 0 and bit 1.  In the current dual-playfield
 * screen they are BPL3 and BPL7, producing palette indices 0, 2, 8 and 10.
 *
 * The rows variant redraws only complete 4x8 text rows.  It neither clears nor
 * reads rows outside [first_row, first_row + row_count), which makes it safe
 * to use after a blitter scroll or for cursor/selection damage.
 */
enum Miga80SourceViewStatus miga80_source_view_render_editor_planar_rows(
    uint8_t *color_bit_0, uint8_t *color_bit_1, size_t bytes_per_row,
    const char *source, size_t source_size, size_t cursor, size_t anchor,
    size_t first_line, size_t first_column, const char *title_text,
    const char *status_text, size_t first_row, size_t row_count);
enum Miga80SourceViewStatus miga80_source_view_render_editor_planar(
    uint8_t *color_bit_0, uint8_t *color_bit_1, size_t bytes_per_row,
    const char *source, size_t source_size, size_t cursor, size_t anchor,
    size_t first_line, size_t first_column, const char *title_text,
    const char *status_text);
enum Miga80SourceViewStatus miga80_source_view_measure_editor(
    const char *source, size_t source_size,
    struct Miga80SourceViewMetrics *metrics);
enum Miga80SourceViewStatus miga80_source_view_draw_status(
    uint8_t *pixels, size_t stride, const char *status_text);

/* One clipped 64-column row, also used by the hosted file selector. */
void miga80_source_view_draw_row(uint8_t *pixels, size_t stride, size_t row,
    const char *text, uint8_t foreground, uint8_t background);

uint32_t miga80_source_view_checksum(const void *bytes, size_t size);

#endif
