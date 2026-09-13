#define __USE_NEW_TIMEVAL__
#include <stdint.h>
#include <string.h>
#include <devices/timer.h>
#include <exec/memory.h>
#include <graphics/view.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/timer.h>
#include "demo/animation.h"

/* Held by the owner for the complete worker lifetime. ReadEClock is a
 * bounded, interrupt-callable read; it starts no I/O and owns no resource. */
struct Device *TimerBase;
struct miga80_animation {
    struct Screen *screen;
    struct ScreenBuffer *buffers[3]; /* original editor + two animation buffers */
    struct MsgPort *replies;
    struct MsgPort *timer_port;
    struct TimeRequest *timer;
    struct EClockVal start;
    ULONG frequency, frames;
    unsigned int front, back, requested;
    int pending, safe, displayed, clock_started;
};

static int request_buffer(struct miga80_animation *a, unsigned int index)
{
    if (a->pending || !ChangeScreenBuffer(a->screen, a->buffers[index])) {
        return 0;
    }
    a->requested = index;
    a->pending = 1;
    a->safe = a->displayed = 0;
    return 1;
}

int miga80_animation_poll(struct miga80_animation *a)
{
    struct Message *message;
    if (!a->pending) { return 1; }
    while ((message = GetMsg(a->replies)) != NULL) {
        struct DBufInfo *db = a->buffers[a->requested]->sb_DBufInfo;
        if (message == &db->dbi_SafeMessage) { a->safe = 1; }
        if (message == &db->dbi_DispMessage) { a->displayed = 1; }
    }
    if (!a->safe || !a->displayed) { return 0; }
    a->front = a->requested;
    a->back = a->front == 1U ? 2U : 1U;
    a->pending = 0;
    if (a->front != 0U) {
        if (!a->clock_started) {
            a->frequency = ReadEClock(&a->start);
            a->clock_started = 1;
        }
        ++a->frames;
    }
    return 1;
}

struct miga80_animation *miga80_animation_create(struct Screen *screen)
{
    struct miga80_animation *a = AllocMem(sizeof(*a), MEMF_PUBLIC | MEMF_CLEAR);
    unsigned int i;
    if (a == NULL) { return NULL; }
    a->screen = screen;
    a->back = 1U;
    a->replies = CreateMsgPort();
    a->timer_port = CreateMsgPort();
    if (a->replies == NULL || a->timer_port == NULL) { goto fail; }
    a->timer = (struct TimeRequest *)CreateIORequest(a->timer_port, sizeof(*a->timer));
    if (a->timer == NULL || OpenDevice(TIMERNAME, UNIT_ECLOCK,
            (struct IORequest *)a->timer, 0U) != 0) { goto fail; }
    TimerBase = a->timer->tr_node.io_Device;
    for (i = 0U; i < 3U; ++i) {
        struct BitMap *bitmap;
        unsigned int plane;
        a->buffers[i] = AllocScreenBuffer(screen, NULL, i == 0U ? SB_SCREEN_BITMAP : 0U);
        if (a->buffers[i] == NULL) { goto fail; }
        bitmap = a->buffers[i]->sb_BitMap;
        if (bitmap->Depth != 8U || bitmap->BytesPerRow != 32U || bitmap->Rows < 256U) {
            goto fail;
        }
        a->buffers[i]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = a->replies;
        a->buffers[i]->sb_DBufInfo->dbi_DispMessage.mn_ReplyPort = a->replies;
        if (i != 0U) {
            for (plane = 0U; plane < 8U; ++plane) {
                memset(bitmap->Planes[plane], 0, 8192U);
            }
        }
    }
    return a;
fail:
    miga80_animation_destroy(a);
    return NULL;
}

ULONG miga80_animation_signals(struct miga80_animation *a) { return 1UL << a->replies->mp_SigBit; }
struct BitMap *miga80_animation_back(struct miga80_animation *a) { return a->buffers[a->back]->sb_BitMap; }
struct BitMap *miga80_animation_front(struct miga80_animation *a) { return a->buffers[a->front]->sb_BitMap; }
int miga80_animation_request(struct miga80_animation *a) { return request_buffer(a, a->back); }
ULONG miga80_animation_frames(struct miga80_animation *a) { return a->frames; }

ULONG miga80_animation_time(struct miga80_animation *a)
{
    struct EClockVal now;
    uint64_t elapsed, seconds;
    if (!a->clock_started || a->frequency == 0U) { return 0U; }
    (void)ReadEClock(&now);
    elapsed = (((uint64_t)now.ev_hi << 32) | now.ev_lo) -
              (((uint64_t)a->start.ev_hi << 32) | a->start.ev_lo);
    seconds = elapsed / a->frequency;
    if (seconds >= 32768U) { return 0x7fffffffUL; }
    return (ULONG)(seconds * 65536U + (elapsed % a->frequency) * 65536U / a->frequency);
}

void miga80_animation_destroy(struct miga80_animation *a)
{
    unsigned int i;
    if (a == NULL) { return; }
    /* Drain BOTH replies before reusing or freeing a DBufInfo. The owner
     * waits here only after joining the worker, with all blits complete. */
    while (a->pending && !miga80_animation_poll(a)) {
        Wait(miga80_animation_signals(a));
    }
    if (a->front != 0U) {
        while (!request_buffer(a, 0U)) { WaitTOF(); }
        while (!miga80_animation_poll(a)) { Wait(miga80_animation_signals(a)); }
    }
    for (i = 0U; i < 3U; ++i) {
        if (a->buffers[i] != NULL) { FreeScreenBuffer(a->screen, a->buffers[i]); }
    }
    if (a->timer != NULL) {
        if (TimerBase != NULL) { CloseDevice((struct IORequest *)a->timer); TimerBase = NULL; }
        DeleteIORequest((struct IORequest *)a->timer);
    }
    if (a->timer_port != NULL) { DeleteMsgPort(a->timer_port); }
    if (a->replies != NULL) { DeleteMsgPort(a->replies); }
    FreeMem(a, sizeof(*a));
}
