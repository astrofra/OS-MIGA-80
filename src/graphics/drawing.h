#ifndef MIGA80_GRAPHICS_DRAWING_H
#define MIGA80_GRAPHICS_DRAWING_H

#include <stdint.h>
#include "compiler/abi/runtime.h"

#define MIGA80_DRAW_WIDTH 256U
#define MIGA80_DRAW_HEIGHT 256U
#define MIGA80_DRAW_PLANE_STRIDE 32U
#define MIGA80_DRAW_PLANE_BYTES 8192U

struct miga80_draw_line {
    int32_t x0, y0, x1, y1;
    uint32_t color;
};

struct miga80_draw_triangle {
    int32_t x0, y0, x1, y1, x2, y2;
    uint32_t color;
};

/* Pixel-center coverage, left/top inclusive and right/bottom exclusive. */
struct miga80_draw_spans {
    int16_t left[256], right[256];
    uint16_t top, bottom, min_x, max_x;
};

struct miga80_draw_surface {
    uint32_t layer; /* Offset zero is used by the native pset fast path. */
    uint32_t pixel_written; /* Native PIXEL pset writes offset four. */
    uint8_t *pixels;
    uint8_t *planes[4];
    int32_t line_x0, line_y0;
    uint32_t line_color;
    void (*planar_line)(void *owner, const struct miga80_draw_line *line);
    void (*planar_pset)(void *owner, uint32_t x, uint32_t y, uint32_t color);
    void (*clear)(void *owner, uint32_t color);
    void (*flip)(void *owner);
    uint32_t (*time)(void *owner);
    void *owner;
    int32_t tri_x1, tri_y1;
    void (*planar_tri)(void *owner, const struct miga80_draw_triangle *triangle);
    struct miga80_draw_spans spans; /* Owner storage, never on the worker stack. */
};

int miga80_draw_triangle_spans(struct miga80_draw_spans *spans,
    const struct miga80_draw_triangle *triangle);
void miga80_draw_triangle(struct miga80_draw_surface *surface,
    const struct miga80_draw_triangle *triangle);
void miga80_draw_tri_middle(struct miga80_draw_surface *surface, uint32_t x, uint32_t y);
void miga80_draw_tri_end(struct miga80_draw_surface *surface, uint32_t x, uint32_t y);

void miga80_draw_clear(struct miga80_draw_surface *surface, uint32_t color);
void miga80_draw_flip(struct miga80_draw_surface *surface);
uint32_t miga80_draw_time(struct miga80_draw_surface *surface);
int miga80_draw_clip_line(struct miga80_draw_line *line);
void miga80_draw_select(struct miga80_draw_surface *surface, uint32_t layer);
void miga80_draw_pset(struct miga80_draw_surface *surface,
                      uint32_t x, uint32_t y, uint32_t color);
void miga80_draw_planar_pset(struct miga80_draw_surface *surface,
                             uint32_t x, uint32_t y, uint32_t color);
void miga80_draw_line_start(struct miga80_draw_surface *surface,
                            uint32_t x, uint32_t y, uint32_t color);
void miga80_draw_line_end(struct miga80_draw_surface *surface,
                          uint32_t x, uint32_t y);
/* Endpoints must already have passed miga80_draw_clip_line. */
void miga80_draw_cpu_line(struct miga80_draw_surface *surface,
                          const struct miga80_draw_line *line);
uint8_t miga80_draw_planar_pixel(const struct miga80_draw_surface *surface,
                                unsigned int x, unsigned int y);

#endif
