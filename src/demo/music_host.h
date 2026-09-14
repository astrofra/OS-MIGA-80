#ifndef MIGA80_DEMO_MUSIC_HOST_H
#define MIGA80_DEMO_MUSIC_HOST_H
#include "compiler/frontend/frontend.h"
#include "demo/supervisor.h"
struct miga80_host_music;
struct miga80_music_stats { ULONG ticks, position, starts, dma_seen; };
/* No music_play in AST => success with *result == NULL, no allocated resources. */
int miga80_host_music_create(const struct miga80_ast_function *ast,
    struct miga80_drawing_context *context, struct miga80_supervisor_events *events,
    struct miga80_host_music **result, char *error, size_t capacity);
void miga80_host_music_destroy(struct miga80_host_music *music,
    struct miga80_music_stats *stats);
const char *miga80_host_music_selftest(void);
void miga80_music_play(struct miga80_host_music *music, uint32_t id);
void miga80_music_stop(struct miga80_host_music *music);
void miga80_music_mute(struct miga80_host_music *music, uint32_t mask);
uint32_t miga80_music_position(struct miga80_host_music *music);
#endif
