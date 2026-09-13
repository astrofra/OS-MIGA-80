#include <exec/memory.h>
#include <exec/tasks.h>
#include <graphics/gfx.h>
#include <hardware/blit.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <proto/exec.h>
#include <proto/graphics.h>

#include "demo/drawing_host.h"
#include "demo/animation.h"
#include "graphics/c2p4_reference.h"

#define DRAW_BATCH_SIZE 16U
static volatile struct Custom *const draw_custom =
    (volatile struct Custom *)0xdff000UL;

struct draw_command {
    struct miga80_draw_line line;
    UWORD pixel;
};

struct miga80_host_drawing {
    struct miga80_draw_surface surface;
    struct Task *owner;
    UBYTE *storage;
    BYTE request_bit;
    volatile ULONG pending;
    ULONG count;
    struct draw_command commands[DRAW_BATCH_SIZE];
    ULONG lines, frames, elapsed;
    struct miga80_animation *animation;
};

static void wait_blitter(void)
{
    /* Read before testing BBUSY, including the early Agnus workaround. */
    (void)draw_custom->dmaconr;
    while ((draw_custom->dmaconr & DMAF_BLTDONE) != 0U) { }
    (void)draw_custom->dmaconr;
}

static void draw_hardware_line(struct miga80_host_drawing *drawing,
                               const struct miga80_draw_line *line)
{
    const LONG delta_x = line->x1 - line->x0;
    const LONG dx = delta_x < 0 ? -delta_x : delta_x;
    const LONG dy = line->y1 - line->y0;
    const LONG major = dx >= dy ? dx : dy;
    const LONG minor = dx >= dy ? dy : dx;
    const LONG error = 4 * minor - 2 * major;
    /* Clipping canonicalizes endpoints top-to-bottom: only lower octants.
     * SUD selects an X major axis; AUL/SUL select negative X accordingly. */
    const UWORD octant = dx >= dy
        ? (UWORD)(SUD | (delta_x < 0 ? AUL : 0U))
        : (UWORD)(delta_x < 0 ? SUL : 0U);
    const UWORD control = (UWORD)(LINEMODE | octant |
                                  (error < 0 ? SIGNFLAG : 0U));
    const ULONG offset = (ULONG)line->y0 * MIGA80_DRAW_PLANE_STRIDE +
                         ((ULONG)line->x0 >> 4) * 2U;
    unsigned int plane;

    for (plane = 0U; plane < 4U; ++plane) {
        APTR start = drawing->surface.planes[plane] + offset;
        wait_blitter();
        /* D = A.B + ~A.C: overwrite just the line, including color zero.
         * B is a constant texture, with no B DMA. A holds the line error. */
        draw_custom->bltcon0 = (UWORD)(((ULONG)line->x0 & 15U) << 12) |
                              SRCA | SRCC | DEST | 0xcaU;
        draw_custom->bltcon1 = control;
        draw_custom->bltafwm = 0xffffU;
        draw_custom->bltalwm = 0xffffU;
        draw_custom->bltamod = (UWORD)(4 * (minor - major));
        draw_custom->bltbmod = (UWORD)(4 * minor);
        draw_custom->bltcmod = MIGA80_DRAW_PLANE_STRIDE;
        draw_custom->bltdmod = MIGA80_DRAW_PLANE_STRIDE;
        draw_custom->bltapt = (APTR)error;
        draw_custom->bltcpt = start;
        draw_custom->bltdpt = start;
        draw_custom->bltadat = 0x8000U;
        draw_custom->bltbdat = (line->color & (1U << plane)) != 0U ? 0xffffU : 0U;
        draw_custom->bltsize = (UWORD)(((major + 1) << 6) | 2U);
    }
    ++drawing->lines;
}

static void clear_hardware(struct miga80_host_drawing *drawing, ULONG color)
{
    unsigned int plane;
    for (plane = 0U; plane < 4U; ++plane) {
        wait_blitter();
        draw_custom->bltcon0 = DEST | ((color & (1U << plane)) != 0U ? 0xffU : 0U);
        draw_custom->bltcon1 = 0U;
        draw_custom->bltafwm = draw_custom->bltalwm = 0xffffU;
        draw_custom->bltdmod = 0U;
        draw_custom->bltdpt = drawing->surface.planes[plane];
        draw_custom->bltsize = (256U << 6) | 16U;
    }
}

void miga80_host_drawing_flush(struct miga80_host_drawing *drawing)
{
    ULONG index;
    UWORD saved_dma;

    if (drawing->count == 0U) {
        return;
    }
    /* Ownership may sleep: acquire before Forbid. The worker never owns
     * the hardware. At most 16 clipped commands run with task switching
     * suspended; keyboard/timer interrupts remain enabled for ESC. */
    OwnBlitter();
    WaitBlit();
    Forbid();
    saved_dma = draw_custom->dmaconr;
    draw_custom->dmacon = DMAF_SETCLR | DMAF_BLITTER | DMAF_BLITHOG;
    for (index = 0U; index < drawing->count; ++index) {
        const struct draw_command *command = &drawing->commands[index];
        if (command->pixel == 2U) {
            clear_hardware(drawing, command->line.color);
        } else if (command->pixel != 0U) {
            wait_blitter();
            miga80_draw_planar_pset(&drawing->surface,
                (uint32_t)command->line.x0, (uint32_t)command->line.y0,
                command->line.color);
        } else {
            draw_hardware_line(drawing, &command->line);
        }
    }
    wait_blitter();
    draw_custom->dmacon = (UWORD)((DMAF_BLITTER | DMAF_BLITHOG) & ~saved_dma);
    drawing->count = 0U;
    /* No DMA remains when the OS regains the resource or the task yields. */
    DisownBlitter();
    Permit();
}

static void submit_command(struct miga80_host_drawing *drawing,
                             const struct miga80_draw_line *line, UWORD pixel)
{
    struct draw_command *command = &drawing->commands[drawing->count];
    command->line = *line;
    command->pixel = pixel;
    ++drawing->count;
    if (drawing->count == DRAW_BATCH_SIZE) {
        if (FindTask(NULL) == drawing->owner) { /* NOSUPERVISOR */
            miga80_host_drawing_flush(drawing);
        } else {
            drawing->pending = 1U;
            Signal(drawing->owner, 1UL << drawing->request_bit);
            /* The owner clears pending after draining the whole batch.
             * This worker owns no lock, signal, I/O or DMA. */
            while (drawing->pending != 0U) { }
        }
    }
}

static void submit_line(void *owner, const struct miga80_draw_line *line)
{
    submit_command(owner, line, 0U);
}

static void submit_pixel(void *owner, uint32_t x, uint32_t y, uint32_t color)
{
    const struct miga80_draw_line line = {(int32_t)x, (int32_t)y, 0, 0, color};
    submit_command(owner, &line, 1U);
}

static void set_back_planes(struct miga80_host_drawing *drawing)
{
    struct BitMap *bitmap = miga80_animation_back(drawing->animation);
    unsigned int plane;
    for (plane = 0U; plane < 4U; ++plane) {
        drawing->surface.planes[plane] = bitmap->Planes[plane * 2U + 1U];
    }
}

static int service_drawing(void *data, ULONG signals)
{
    struct miga80_host_drawing *drawing = data;
    (void)signals;
    if (drawing->pending == 1U) {
        miga80_host_drawing_flush(drawing);
        drawing->pending = 0U;
    } else if (drawing->pending == 2U) {
        uint8_t *front[4];
        unsigned int plane;
        struct BitMap *bitmap = miga80_animation_back(drawing->animation);
        miga80_host_drawing_flush(drawing);
        if (drawing->surface.pixel_written) {
            for (plane = 0U; plane < 4U; ++plane) { front[plane] = bitmap->Planes[plane * 2U]; }
            if (miga80_c2p4_reference_byte4(drawing->surface.pixels, 256U, 256U, 256U,
                    front, bitmap->BytesPerRow) != MIGA80_C2P4_OK) { return 0; }
        }
        if (!miga80_animation_request(drawing->animation)) { return 0; }
        drawing->pending = 3U;
    }
    if (drawing->pending == 3U && miga80_animation_poll(drawing->animation)) {
        set_back_planes(drawing);
        drawing->pending = 0U;
    }
    return 1;
}

static void submit_clear(void *owner, uint32_t color)
{
    const struct miga80_draw_line line = {0, 0, 0, 0, color};
    submit_command(owner, &line, 2U);
}

static void submit_flip(void *owner)
{
    struct miga80_host_drawing *drawing = owner;
    if (drawing->animation == NULL) { return; }
    drawing->pending = 2U;
    if (FindTask(NULL) == drawing->owner) {
        if (!service_drawing(drawing, 0U)) { drawing->pending = 0U; return; }
        while (drawing->pending != 0U) {
            Wait(miga80_animation_signals(drawing->animation));
            (void)service_drawing(drawing, 0U);
        }
    } else {
        Signal(drawing->owner, 1UL << drawing->request_bit);
        while (drawing->pending != 0U) { }
    }
}

static uint32_t read_time(void *owner)
{
    struct miga80_host_drawing *drawing = owner;
    return drawing->animation != NULL ? miga80_animation_time(drawing->animation) : 0U;
}

int miga80_host_drawing_animate(struct miga80_host_drawing *drawing,
    struct Screen *screen, struct miga80_supervisor_events *events)
{
    drawing->animation = miga80_animation_create(screen);
    if (drawing->animation == NULL) { return 0; }
    events->signals |= miga80_animation_signals(drawing->animation);
    set_back_planes(drawing);
    return 1;
}

void miga80_host_drawing_finish(struct miga80_host_drawing *drawing)
{
    unsigned int plane;
    if (drawing->animation == NULL) { return; }
    while (!miga80_animation_poll(drawing->animation)) {
        Wait(miga80_animation_signals(drawing->animation));
    }
    drawing->frames = miga80_animation_frames(drawing->animation);
    drawing->elapsed = miga80_animation_time(drawing->animation);
    for (plane = 0U; plane < 4U; ++plane) {
        UBYTE *destination = drawing->storage + plane * MIGA80_DRAW_PLANE_BYTES;
        if (drawing->frames != 0U) {
            CopyMem(miga80_animation_front(drawing->animation)->Planes[plane * 2U + 1U],
                    destination, MIGA80_DRAW_PLANE_BYTES);
        }
        drawing->surface.planes[plane] = destination;
    }
    miga80_animation_destroy(drawing->animation);
    drawing->animation = NULL;
    drawing->pending = 0U;
}
ULONG miga80_host_drawing_frames(struct miga80_host_drawing *drawing) { return drawing->frames; }
ULONG miga80_host_drawing_elapsed(struct miga80_host_drawing *drawing) { return drawing->elapsed; }

struct miga80_host_drawing *miga80_host_drawing_create(
    uint8_t *pixels, struct miga80_drawing_context *context,
    struct miga80_supervisor_events *events)
{
    struct miga80_host_drawing *drawing =
        AllocMem(sizeof(*drawing), MEMF_PUBLIC | MEMF_CLEAR);
    unsigned int plane;

    if (drawing == NULL) {
        return NULL;
    }
    drawing->request_bit = AllocSignal(-1);
    drawing->owner = FindTask(NULL);
    drawing->storage = AllocMem(4U * MIGA80_DRAW_PLANE_BYTES,
                                MEMF_CHIP | MEMF_PUBLIC | MEMF_CLEAR);
    if (drawing->request_bit < 0 || drawing->storage == NULL) {
        miga80_host_drawing_destroy(drawing);
        return NULL;
    }
    drawing->surface.layer = MIGA80_LAYER_PIXEL;
    drawing->surface.pixels = pixels;
    drawing->surface.planar_line = submit_line;
    drawing->surface.planar_pset = submit_pixel;
    drawing->surface.owner = drawing;
    drawing->surface.clear = submit_clear;
    drawing->surface.flip = submit_flip;
    drawing->surface.time = read_time;
    for (plane = 0U; plane < 4U; ++plane) {
        drawing->surface.planes[plane] =
            drawing->storage + plane * MIGA80_DRAW_PLANE_BYTES;
    }
    context->drawing_state = (uint32_t)(uintptr_t)&drawing->surface;
    events->signals = 1UL << drawing->request_bit;
    events->service = service_drawing;
    events->data = drawing;
    return drawing;
}

int miga80_host_drawing_publish(struct miga80_host_drawing *drawing,
                                struct BitMap *destination)
{
    uint8_t *front[4];
    unsigned int plane;

    if (destination == NULL || destination->Depth != 8U) {
        return 0;
    }
    for (plane = 0U; plane < 4U; ++plane) {
        unsigned int row;
        front[plane] = destination->Planes[plane * 2U];
        if (front[plane] == NULL || destination->Planes[plane * 2U + 1U] == NULL) {
            return 0;
        }
        for (row = 0U; row < MIGA80_DRAW_HEIGHT; ++row) {
            CopyMem(drawing->surface.planes[plane] + row * MIGA80_DRAW_PLANE_STRIDE,
                    destination->Planes[plane * 2U + 1U] + row * destination->BytesPerRow,
                    MIGA80_DRAW_PLANE_STRIDE);
        }
    }
    return miga80_c2p4_reference_byte4(drawing->surface.pixels,
        MIGA80_DRAW_WIDTH, MIGA80_DRAW_HEIGHT, MIGA80_DRAW_WIDTH,
        front, destination->BytesPerRow) == MIGA80_C2P4_OK;
}

struct miga80_draw_surface *miga80_host_drawing_surface(
    struct miga80_host_drawing *drawing)
{
    return &drawing->surface;
}

ULONG miga80_host_drawing_lines(struct miga80_host_drawing *drawing)
{
    return drawing->lines;
}

void miga80_host_drawing_destroy(struct miga80_host_drawing *drawing)
{
    WaitBlit();
    miga80_host_drawing_finish(drawing);
    drawing->pending = 0U;
    if (drawing->request_bit >= 0) {
        (void)SetSignal(0U, 1UL << drawing->request_bit);
        FreeSignal(drawing->request_bit);
    }
    if (drawing->storage != NULL) {
        FreeMem(drawing->storage, 4U * MIGA80_DRAW_PLANE_BYTES);
    }
    FreeMem(drawing, sizeof(*drawing));
}
