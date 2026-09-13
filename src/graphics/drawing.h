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

struct miga80_draw_surface {
    uint32_t layer; /* Offset zero is used by the native pset fast path. */
    uint8_t *pixels;
    uint8_t *planes[4];
    int32_t line_x0, line_y0;
    uint32_t line_color;
    void (*planar_line)(void *owner, const struct miga80_draw_line *line);
    void (*planar_pset)(void *owner, uint32_t x, uint32_t y, uint32_t color);
    void *owner;
};

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
