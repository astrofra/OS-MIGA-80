#ifndef MIGA80_DEMO_ANIMATION_H
#define MIGA80_DEMO_ANIMATION_H
#include <exec/types.h>
struct Screen;
struct BitMap;
struct miga80_animation;
struct miga80_animation *miga80_animation_create(struct Screen *screen);
ULONG miga80_animation_signals(struct miga80_animation *animation);
struct BitMap *miga80_animation_back(struct miga80_animation *animation);
struct BitMap *miga80_animation_front(struct miga80_animation *animation);
int miga80_animation_request(struct miga80_animation *animation);
int miga80_animation_poll(struct miga80_animation *animation);
ULONG miga80_animation_time(struct miga80_animation *animation);
ULONG miga80_animation_frames(struct miga80_animation *animation);
/* Owner only, with the native worker removed/joined. Restores the editor. */
void miga80_animation_destroy(struct miga80_animation *animation);
#endif
