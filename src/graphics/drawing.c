#include "graphics/drawing.h"
#include <string.h>

static unsigned int outcode(int32_t x, int32_t y)
{
    return (x < 0 ? 1U : x >= (int32_t)MIGA80_DRAW_WIDTH ? 2U : 0U) |
           (y < 0 ? 4U : y >= (int32_t)MIGA80_DRAW_HEIGHT ? 8U : 0U);
}

int miga80_draw_clip_line(struct miga80_draw_line *line)
{
    /* Cohen-Sutherland, integer intersections truncated towards zero.
     * Subtract in 64 bits. Distance to the nearest clipping edge is at most
     * 2^31, so its product with an endpoint delta fits signed 64 bits. */
    unsigned int step;

    if (line->color > 15U) {
        return 0;
    }
    /* Canonical endpoint order makes reversed segments rasterize identically. */
    if (line->y0 > line->y1 || (line->y0 == line->y1 && line->x0 > line->x1)) {
        const int32_t x = line->x0;
        const int32_t y = line->y0;
        line->x0 = line->x1;
        line->y0 = line->y1;
        line->x1 = x;
        line->y1 = y;
    }
    for (step = 0U; step < 8U; ++step) {
        const unsigned int first = outcode(line->x0, line->y0);
        const unsigned int second = outcode(line->x1, line->y1);
        const unsigned int outside = first != 0U ? first : second;
        int64_t x = first != 0U ? line->x0 : line->x1;
        int64_t y = first != 0U ? line->y0 : line->y1;
        const int64_t dx = (int64_t)line->x1 - line->x0;
        const int64_t dy = (int64_t)line->y1 - line->y0;

        if (outside == 0U) {
            return 1;
        }
        if ((first & second) != 0U) {
            return 0;
        }
        if ((outside & 12U) != 0U) {
            const int64_t edge = (outside & 4U) != 0U ? 0 : 255;
            x += dx * (edge - y) / dy;
            y = edge;
        } else {
            const int64_t edge = (outside & 1U) != 0U ? 0 : 255;
            y += dy * (edge - x) / dx;
            x = edge;
        }
        if (first != 0U) {
            line->x0 = (int32_t)x;
            line->y0 = (int32_t)y;
        } else {
            line->x1 = (int32_t)x;
            line->y1 = (int32_t)y;
        }
    }
    return 0; /* Bounded even for malformed/extreme endpoints. */
}

void miga80_draw_select(struct miga80_draw_surface *surface, uint32_t layer)
{
    if (layer == MIGA80_LAYER_PLANAR || layer == MIGA80_LAYER_PIXEL) {
        surface->layer = layer;
    }
}

void miga80_draw_pset(struct miga80_draw_surface *surface,
                      uint32_t x, uint32_t y, uint32_t color)
{
    if (x >= MIGA80_DRAW_WIDTH || y >= MIGA80_DRAW_HEIGHT || color > 15U) {
        return;
    }
    if (surface->layer == MIGA80_LAYER_PIXEL) {
        surface->pixel_written = 1U;
        surface->pixels[y * MIGA80_DRAW_WIDTH + x] = (uint8_t)color;
        return;
    }
    if (surface->planar_pset != 0) {
        surface->planar_pset(surface->owner, x, y, color);
    } else {
        miga80_draw_planar_pset(surface, x, y, color);
    }
}

void miga80_draw_planar_pset(struct miga80_draw_surface *surface,
                             uint32_t x, uint32_t y, uint32_t color)
{
    unsigned int plane;
    unsigned int offset;
    uint8_t mask;

    if (x >= MIGA80_DRAW_WIDTH || y >= MIGA80_DRAW_HEIGHT || color > 15U) {
        return;
    }
    offset = y * MIGA80_DRAW_PLANE_STRIDE + (x >> 3);
    mask = (uint8_t)(0x80U >> (x & 7U));
    for (plane = 0U; plane < 4U; ++plane) {
        uint8_t *byte = &surface->planes[plane][offset];
        *byte = (uint8_t)((*byte & (uint8_t)~mask) |
                         ((color & (1U << plane)) != 0U ? mask : 0U));
    }
}

void miga80_draw_cpu_line(struct miga80_draw_surface *surface,
                          const struct miga80_draw_line *line)
{
    int32_t x = line->x0;
    int32_t y = line->y0;
    const int32_t sx = x < line->x1 ? 1 : -1;
    const int32_t sy = y < line->y1 ? 1 : -1;
    const int32_t dx = x < line->x1 ? line->x1 - x : x - line->x1;
    const int32_t dy = y < line->y1 ? line->y1 - y : y - line->y1;
    int32_t error = dx - dy;

    for (;;) {
        const int32_t twice = 2 * error;
        miga80_draw_pset(surface, (uint32_t)x, (uint32_t)y, line->color);
        if (x == line->x1 && y == line->y1) {
            return;
        }
        if (twice >= -dy) {
            error -= dy;
            x += sx;
        }
        if (twice <= dx) {
            error += dx;
            y += sy;
        }
    }
}

void miga80_draw_line_start(struct miga80_draw_surface *surface,
                            uint32_t x, uint32_t y, uint32_t color)
{
    surface->line_x0 = (int32_t)x;
    surface->line_y0 = (int32_t)y;
    surface->line_color = color;
}

void miga80_draw_line_end(struct miga80_draw_surface *surface,
                          uint32_t x, uint32_t y)
{
    struct miga80_draw_line line = {
        surface->line_x0, surface->line_y0, (int32_t)x, (int32_t)y,
        surface->line_color
    };
    if (!miga80_draw_clip_line(&line)) {
        return;
    }
    if (surface->layer == MIGA80_LAYER_PLANAR && surface->planar_line != 0) {
        surface->planar_line(surface->owner, &line);
    } else {
        miga80_draw_cpu_line(surface, &line);
    }
}

uint8_t miga80_draw_planar_pixel(const struct miga80_draw_surface *surface,
                                unsigned int x, unsigned int y)
{
    unsigned int plane;
    uint8_t color = 0U;
    const unsigned int offset = y * MIGA80_DRAW_PLANE_STRIDE + (x >> 3);
    const uint8_t mask = (uint8_t)(0x80U >> (x & 7U));
    for (plane = 0U; plane < 4U; ++plane) {
        if ((surface->planes[plane][offset] & mask) != 0U) {
            color |= (uint8_t)(1U << plane);
        }
    }
    return color;
}

void miga80_draw_clear(struct miga80_draw_surface *surface, uint32_t color)
{
    unsigned int plane;
    if (color > 15U) { return; }
    if (surface->layer == MIGA80_LAYER_PIXEL) {
        memset(surface->pixels, (int)color, MIGA80_DRAW_WIDTH * MIGA80_DRAW_HEIGHT);
        surface->pixel_written = 1U;
    } else if (surface->clear != NULL) {
        surface->clear(surface->owner, color);
    } else {
        for (plane = 0U; plane < 4U; ++plane) {
            memset(surface->planes[plane], (color & (1U << plane)) != 0U ? 255 : 0,
                   MIGA80_DRAW_PLANE_BYTES);
        }
    }
}

void miga80_draw_flip(struct miga80_draw_surface *surface)
{
    if (surface->flip != NULL) { surface->flip(surface->owner); }
}
uint32_t miga80_draw_time(struct miga80_draw_surface *surface)
{
    return surface->time != NULL ? surface->time(surface->owner) : 0U;
}

void miga80_draw_color_response(struct miga80_draw_surface *surface,
                                uint32_t response)
{
    if (response < MIGA80_RESPONSE_COUNT && surface->color_response != NULL) {
        surface->color_response(surface->owner, response);
    }
}

void miga80_draw_print_start(struct miga80_draw_surface *surface,
                             uint32_t resource, uint32_t x, uint32_t y)
{
    surface->text_resource = resource;
    surface->text_x = (int32_t)x;
    surface->text_y = (int32_t)y;
}

void miga80_draw_print_end(struct miga80_draw_surface *surface,
                           uint32_t color)
{
    const struct miga80_draw_text text = {
        surface->text_resource, surface->text_x, surface->text_y, color,
        surface->layer
    };
    if (color < 16U && surface->text != NULL) {
        surface->text(surface->owner, &text);
    }
}
