#ifndef MIGA80_DEMO_DRAWING_HOST_H
#define MIGA80_DEMO_DRAWING_HOST_H

#include "demo/supervisor.h"
#include "graphics/drawing.h"

struct BitMap;
struct Screen;
struct miga80_host_drawing;
struct miga80_host_drawing *miga80_host_drawing_create(
    uint8_t *pixels, struct miga80_drawing_context *context,
    struct miga80_supervisor_events *events);
int miga80_host_drawing_animate(struct miga80_host_drawing *drawing,
    struct Screen *screen, struct miga80_supervisor_events *events);
void miga80_host_drawing_finish(struct miga80_host_drawing *drawing);
ULONG miga80_host_drawing_frames(struct miga80_host_drawing *drawing);
ULONG miga80_host_drawing_elapsed(struct miga80_host_drawing *drawing);
/* Owner only, after successful worker completion; discard on stop/fault. */
void miga80_host_drawing_flush(struct miga80_host_drawing *drawing);
int miga80_host_drawing_publish(struct miga80_host_drawing *drawing,
                                struct BitMap *destination);
struct miga80_draw_surface *miga80_host_drawing_surface(
    struct miga80_host_drawing *drawing);
ULONG miga80_host_drawing_lines(struct miga80_host_drawing *drawing);
/* Owner only, after removing/joining the generated worker. */
void miga80_host_drawing_destroy(struct miga80_host_drawing *drawing);

#endif
