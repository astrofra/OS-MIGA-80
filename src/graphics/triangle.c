#include "graphics/drawing.h"
#include <string.h>

/* Exact rational DDA: only edge setup divides; each row uses additions.
 * ceil(x(y + 1/2) - 1/2) gives shared edges identical pixel-center coverage. */
static void edge(struct miga80_draw_spans *spans,
    int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    int32_t dx, dy, first, last, denominator, x, remainder, step, extra, y;
    int64_t numerator, quotient;
    if (y0 > y1) {
        int32_t t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
    }
    if (y0 == y1 || y1 <= 0 || y0 >= 256) { return; }
    dx = x1 - x0; dy = y1 - y0;
    first = y0 < 0 ? 0 : y0; last = y1 > 256 ? 256 : y1;
    denominator = 2 * dy;
    numerator = (int64_t)dx * (2 * (first - y0) + 1) - dy;
    quotient = numerator / denominator;
    remainder = (int32_t)(numerator % denominator);
    if (remainder < 0) { --quotient; remainder += denominator; }
    x = x0 + (int32_t)quotient;
    step = dx / dy; extra = 2 * (dx % dy);
    if (extra < 0) { --step; extra += denominator; }
    for (y = first; y < last; ++y) {
        int32_t boundary = x + (remainder != 0);
        if (boundary < 0) { boundary = 0; }
        if (boundary > 256) { boundary = 256; }
        if (boundary < spans->left[y]) { spans->left[y] = (int16_t)boundary; }
        if (boundary > spans->right[y]) { spans->right[y] = (int16_t)boundary; }
        x += step; remainder += extra;
        if (remainder >= denominator) { remainder -= denominator; ++x; }
    }
}

int miga80_draw_triangle_spans(struct miga80_draw_spans *spans,
    const struct miga80_draw_triangle *t)
{
    unsigned int y;
    /* A bounded signed-16 coordinate domain keeps all setup arithmetic safe.
     * Coordinates outside this domain and invalid colors are rejected. */
    if (t->color > 15U || t->x0 < -32768 || t->x0 > 32767 ||
        t->y0 < -32768 || t->y0 > 32767 || t->x1 < -32768 || t->x1 > 32767 ||
        t->y1 < -32768 || t->y1 > 32767 || t->x2 < -32768 || t->x2 > 32767 ||
        t->y2 < -32768 || t->y2 > 32767) { return 0; }
    spans->top = 256U; spans->bottom = 0U;
    spans->min_x = 256U; spans->max_x = 0U;
    for (y = 0U; y < 256U; ++y) { spans->left[y] = 256; spans->right[y] = 0; }
    edge(spans, t->x0, t->y0, t->x1, t->y1);
    edge(spans, t->x1, t->y1, t->x2, t->y2);
    edge(spans, t->x2, t->y2, t->x0, t->y0);
    for (y = 0U; y < 256U; ++y) {
        if (spans->left[y] < spans->right[y]) {
            if (spans->top == 256U) { spans->top = (uint16_t)y; }
            spans->bottom = (uint16_t)(y + 1U);
            if (spans->left[y] < spans->min_x) { spans->min_x = (uint16_t)spans->left[y]; }
            if (spans->right[y] > spans->max_x) { spans->max_x = (uint16_t)spans->right[y]; }
        } else { spans->left[y] = spans->right[y] = 0; }
    }
    return spans->top < spans->bottom;
}

#ifdef __m68k__
extern void miga80_draw_chunky_spans_m68k(uint8_t *pixels,
    const int16_t *left, const int16_t *right, uint32_t rows, uint32_t color);
#endif

void miga80_draw_triangle(struct miga80_draw_surface *surface,
    const struct miga80_draw_triangle *triangle)
{
    struct miga80_draw_spans *spans = &surface->spans;
    unsigned int y;
    if (surface->layer == MIGA80_LAYER_PLANAR && surface->planar_tri != NULL) {
        surface->planar_tri(surface->owner, triangle);
        return;
    }
    if (!miga80_draw_triangle_spans(spans, triangle)) { return; }
    if (surface->layer == MIGA80_LAYER_PIXEL) {
        surface->pixel_written = 1U;
#ifdef __m68k__
        miga80_draw_chunky_spans_m68k(surface->pixels + spans->top * 256U,
            spans->left + spans->top, spans->right + spans->top,
            spans->bottom - spans->top, triangle->color);
#else
        for (y = spans->top; y < spans->bottom; ++y) {
            memset(surface->pixels + y * 256U + spans->left[y],
                (int)triangle->color, (size_t)(spans->right[y] - spans->left[y]));
        }
#endif
    } else {
        for (y = spans->top; y < spans->bottom; ++y) {
            unsigned int x;
            for (x = (unsigned int)spans->left[y]; x < (unsigned int)spans->right[y]; ++x) {
                miga80_draw_planar_pset(surface, x, y, triangle->color);
            }
        }
    }
}

void miga80_draw_tri_middle(struct miga80_draw_surface *surface, uint32_t x, uint32_t y)
{
    surface->tri_x1 = (int32_t)x; surface->tri_y1 = (int32_t)y;
}

void miga80_draw_tri_end(struct miga80_draw_surface *surface, uint32_t x, uint32_t y)
{
    const struct miga80_draw_triangle triangle = {
        surface->line_x0, surface->line_y0, surface->tri_x1, surface->tri_y1,
        (int32_t)x, (int32_t)y, surface->line_color
    };
    miga80_draw_triangle(surface, &triangle);
}
