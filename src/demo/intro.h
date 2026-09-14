#ifndef MIGA80_DEMO_INTRO_H
#define MIGA80_DEMO_INTRO_H
#include <exec/types.h>
struct Screen;
struct miga80_intro_stats {
    ULONG frames, elapsed_q16, audio_mask, audio_completed, audio_errors;
    ULONG pcm_checksum, dma_seen;
};
enum miga80_intro_result {
    MIGA80_INTRO_FAILED = -1, MIGA80_INTRO_COMPLETE = 0,
    MIGA80_INTRO_SKIPPED = 1, MIGA80_INTRO_QUIT = 2
};
/* poll_input returns 0, SKIPPED, QUIT, or FAILED; runs on the owner task.
 * The caller restores its palette after this function. Original bitmap restored. */
int miga80_intro_run(struct Screen *screen, int (*poll_input)(void *), void *data,
                    struct miga80_intro_stats *stats);
/* Regression probe: reserve every audio channel without stealing or waiting. */
int miga80_intro_audio_available(void);
#endif
