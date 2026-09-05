#ifndef MIGA80_UI_SOURCE_VIEW_H
#define MIGA80_UI_SOURCE_VIEW_H

#include <stddef.h>
#include <stdint.h>

enum {
    MIGA80_SOURCE_VIEW_WIDTH = 256,
    MIGA80_SOURCE_VIEW_HEIGHT = 256,
    MIGA80_SOURCE_VIEW_COLUMNS = 64,
    MIGA80_SOURCE_VIEW_ROWS = 32,
    MIGA80_SOURCE_VIEW_SOURCE_ROWS = 30
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

uint32_t miga80_source_view_checksum(const void *bytes, size_t size);

#endif
