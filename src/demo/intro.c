#include "demo/intro.h"
#include <string.h>
#include <devices/audio.h>
#include <exec/memory.h>
#include <exec/errors.h>
#include <proto/alib.h>
#include <graphics/gfx.h>
#include <graphics/view.h>
#include <hardware/custom.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include "audio/boot_jingle.h"
#include "demo/animation.h"
#include "demo/intro_effect.h"
#include "graphics/c2p4_reference.h"
#include "ui/source_view.h"

struct boot_audio {
    struct MsgPort *control_port, *write_port;
    struct IOAudio *control, *write[2];
    int8_t *pcm;
    ULONG mask;
    int opened, submitted[2];
};

static void audio_close(struct boot_audio *audio, struct miga80_intro_stats *stats)
{
    unsigned int i, pending = 0U;
    for (i = 0U; i < 2U; ++i) {
        if (audio->submitted[i]) {
            ++pending;
            (void)AbortIO((struct IORequest *)audio->write[i]);
        }
    }
    while (pending != 0U) {
        struct Message *reply;
        WaitPort(audio->write_port);
        while ((reply = GetMsg(audio->write_port)) != NULL) {
            for (i = 0U; i < 2U; ++i) {
                if (audio->submitted[i] && reply == &audio->write[i]->ioa_Request.io_Message) {
                    const BYTE error = audio->write[i]->ioa_Request.io_Error;
                    audio->submitted[i] = 0;
                    --pending;
                    if (error == 0) { ++stats->audio_completed; }
                    else if (error != IOERR_ABORTED) { ++stats->audio_errors; }
                }
            }
        }
    }
    if (audio->opened) {
        /* Close with the complete original mask, not one playback channel. */
        audio->control->ioa_Request.io_Unit = (struct Unit *)audio->mask;
        CloseDevice((struct IORequest *)audio->control);
    }
    for (i = 0U; i < 2U; ++i) {
        if (audio->write[i] != NULL) { DeleteIORequest((struct IORequest *)audio->write[i]); }
    }
    if (audio->control != NULL) { DeleteIORequest((struct IORequest *)audio->control); }
    if (audio->write_port != NULL) { DeleteMsgPort(audio->write_port); }
    if (audio->control_port != NULL) { DeleteMsgPort(audio->control_port); }
    if (audio->pcm != NULL) { FreeMem(audio->pcm, MIGA80_BOOT_JINGLE_SAMPLES); }
    (void)memset(audio, 0, sizeof(*audio));
}

static int audio_control(struct boot_audio *audio, UWORD command)
{
    struct IOAudio *io = audio->control;
    io->ioa_Request.io_Command = command;
    io->ioa_Request.io_Flags = IOF_QUICK;
    BeginIO((struct IORequest *)io);
    if ((io->ioa_Request.io_Flags & IOF_QUICK) == 0U) {
        WaitPort(audio->control_port);
        (void)GetMsg(audio->control_port);
    }
    return io->ioa_Request.io_Error == 0;
}

static int audio_prepare(struct boot_audio *audio, struct miga80_intro_stats *stats)
{
    static UBYTE pairs[] = {3U, 5U, 10U, 12U};
    unsigned int bit, voice = 0U;
    audio->control_port = CreateMsgPort();
    audio->write_port = CreateMsgPort();
    if (audio->control_port == NULL || audio->write_port == NULL) { goto fail; }
    audio->control = (struct IOAudio *)CreateIORequest(audio->control_port, sizeof(struct IOAudio));
    if (audio->control == NULL) { goto fail; }
    audio->control->ioa_Request.io_Message.mn_Node.ln_Pri = ADALLOC_MINPREC;
    audio->control->ioa_Request.io_Flags = ADIOF_NOWAIT;
    audio->control->ioa_Data = pairs;
    audio->control->ioa_Length = sizeof(pairs);
    if (OpenDevice(AUDIONAME, 0U, (struct IORequest *)audio->control, 0U) != 0) { goto fail; }
    audio->opened = 1;
    audio->mask = (ULONG)audio->control->ioa_Request.io_Unit;
    audio->pcm = AllocMem(MIGA80_BOOT_JINGLE_SAMPLES, MEMF_CHIP);
    if (audio->pcm == NULL) { goto fail; }
    miga80_boot_jingle_generate(audio->pcm);
    stats->pcm_checksum = miga80_source_view_checksum(audio->pcm, MIGA80_BOOT_JINGLE_SAMPLES);
    for (bit = 0U; bit < 4U; ++bit) {
        struct IOAudio *io;
        if ((audio->mask & (1UL << bit)) == 0U) { continue; }
        if (voice >= 2U) { goto fail; }
        io = (struct IOAudio *)CreateIORequest(audio->write_port, sizeof(*io));
        audio->write[voice++] = io;
        if (io == NULL) { goto fail; }
        io->ioa_Request.io_Device = audio->control->ioa_Request.io_Device;
        io->ioa_Request.io_Unit = (struct Unit *)(1UL << bit);
        io->ioa_Request.io_Command = CMD_WRITE;
        io->ioa_Request.io_Flags = ADIOF_PERVOL;
        io->ioa_AllocKey = audio->control->ioa_AllocKey;
        io->ioa_Data = (UBYTE *)audio->pcm;
        io->ioa_Length = MIGA80_BOOT_JINGLE_SAMPLES;
        io->ioa_Period = MIGA80_BOOT_JINGLE_PERIOD;
        io->ioa_Volume = 48U;
        io->ioa_Cycles = 1U;
    }
    if (voice != 2U) { goto fail; }
    stats->audio_mask = audio->mask;
    return 1;
fail:
    audio_close(audio, stats);
    return 0; /* The visual intro still runs if audio is unavailable. */
}

static int audio_start(struct boot_audio *audio)
{
    unsigned int i;
    if (!audio_control(audio, CMD_STOP)) { return 0; }
    for (i = 0U; i < 2U; ++i) {
        audio->submitted[i] = 1;
        /* BeginIO preserves ADIOF_PERVOL; SendIO/DoIO clear audio flags. */
        BeginIO((struct IORequest *)audio->write[i]);
    }
    return audio_control(audio, CMD_START);
}

int miga80_intro_audio_available(void)
{
    struct boot_audio audio = {0};
    struct miga80_intro_stats stats = {0};
    UBYTE all = 15U;
    int available = 0;
    audio.control_port = CreateMsgPort();
    if (audio.control_port == NULL) { return 0; }
    audio.control = (struct IOAudio *)CreateIORequest(audio.control_port, sizeof(struct IOAudio));
    if (audio.control != NULL) {
        audio.control->ioa_Request.io_Message.mn_Node.ln_Pri = ADALLOC_MINPREC;
        audio.control->ioa_Request.io_Flags = ADIOF_NOWAIT;
        audio.control->ioa_Data = &all;
        audio.control->ioa_Length = 1U;
        if (OpenDevice(AUDIONAME, 0U, (struct IORequest *)audio.control, 0U) == 0) {
            audio.opened = 1;
            audio.mask = (ULONG)audio.control->ioa_Request.io_Unit;
            available = audio.mask == 15U;
        }
    }
    audio_close(&audio, &stats);
    return available;
}

int miga80_intro_run(struct Screen *screen, int (*poll_input)(void *), void *data,
                    struct miga80_intro_stats *stats)
{
    struct miga80_animation *animation = NULL;
    struct boot_audio audio = {0};
    uint8_t *strip = NULL;
    ULONG palette[50];
    unsigned int color, last_tick = ~0U;
    int result = MIGA80_INTRO_FAILED, pending = 0, sound_ready, sound_started = 0;
    (void)memset(stats, 0, sizeof(*stats));
    palette[0] = 16UL << 16;
    for (color = 0U; color < 16U; ++color) {
        const uint32_t rgb = miga80_intro_rgb(color);
        palette[1U + color * 3U] = ((rgb >> 16) & 255U) * 0x01010101UL;
        palette[2U + color * 3U] = ((rgb >> 8) & 255U) * 0x01010101UL;
        palette[3U + color * 3U] = (rgb & 255U) * 0x01010101UL;
    }
    palette[49] = 0U;
    LoadRGB32(&screen->ViewPort, palette);
    animation = miga80_animation_create(screen);
    strip = AllocMem(MIGA80_INTRO_STRIP_BYTES, MEMF_PUBLIC);
    if (animation == NULL || strip == NULL) { goto done; }
    sound_ready = audio_prepare(&audio, stats);
    for (;;) {
        ULONG elapsed;
        unsigned int tick;
        result = poll_input != NULL ? poll_input(data) : 0;
        if (result != 0) { break; }
        if (pending && miga80_animation_poll(animation)) { pending = 0; }
        if (!sound_started && miga80_animation_frames(animation) != 0U) {
            sound_started = 1;
            if (sound_ready && !audio_start(&audio)) { ++stats->audio_errors; }
        }
        if (sound_ready && sound_started) {
            volatile struct Custom *custom = (volatile struct Custom *)0xdff000UL;
            if ((custom->dmaconr & audio.mask) == audio.mask) { stats->dma_seen = 1U; }
        }
        elapsed = miga80_animation_time(animation);
        stats->elapsed_q16 = elapsed;
        tick = (unsigned int)((elapsed * 50U) >> 16);
        if (tick >= MIGA80_INTRO_TICKS) { result = MIGA80_INTRO_COMPLETE; break; }
        if (!pending && tick != last_tick) {
            struct BitMap *bitmap = miga80_animation_back(animation);
            uint8_t *planes[4];
            unsigned int plane;
            miga80_intro_render(strip, tick);
            for (plane = 0U; plane < 4U; ++plane) {
                planes[plane] = bitmap->Planes[plane * 2U] +
                    MIGA80_INTRO_STRIP_Y * bitmap->BytesPerRow;
            }
            if (miga80_c2p4_kalms_color4(strip, MIGA80_INTRO_WIDTH,
                    MIGA80_INTRO_STRIP_HEIGHT, MIGA80_INTRO_WIDTH, planes,
                    bitmap->BytesPerRow) != MIGA80_C2P4_OK ||
                !miga80_animation_request(animation)) {
                result = MIGA80_INTRO_FAILED;
                break;
            }
            pending = 1;
            last_tick = tick;
        }
        WaitTOF();
    }
    stats->frames = miga80_animation_frames(animation);
done:
    audio_close(&audio, stats);
    miga80_animation_destroy(animation);
    if (strip != NULL) { FreeMem(strip, MIGA80_INTRO_STRIP_BYTES); }
    return result;
}
