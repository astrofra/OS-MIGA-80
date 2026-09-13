/* Private STOPTEST instrumentation. All I/O belongs to the supervising task. */
#include <string.h>
#define __USE_NEW_TIMEVAL__
#include <devices/input.h>
#include <devices/inputevent.h>
#include <devices/timer.h>
#include <exec/memory.h>
#include <proto/exec.h>

#include "demo/stop_test.h"

struct miga80_stop_injector {
    struct MsgPort *timer_port;
    struct MsgPort *input_port;
    struct TimeRequest *timer;
    struct IOStdReq *input;
    int timer_open;
    int input_open;
    int pending;
    unsigned int phase;
};

static int send_key(struct miga80_stop_injector *injector, UWORD code,
                     UWORD qualifier)
{
    struct InputEvent event;

    (void)memset(&event, 0, sizeof(event));
    event.ie_Class = IECLASS_RAWKEY;
    event.ie_Code = code;
    event.ie_Qualifier = qualifier;
    injector->input->io_Command = IND_WRITEEVENT;
    injector->input->io_Data = (APTR)&event;
    injector->input->io_Length = sizeof(event);
    return DoIO((struct IORequest *)injector->input) == 0;
}

static void arm_timer(struct miga80_stop_injector *injector, ULONG micros)
{
    injector->timer->tr_node.io_Command = TR_ADDREQUEST;
    injector->timer->tr_time.tv_secs = micros / 1000000U;
    injector->timer->tr_time.tv_micro = micros % 1000000U;
    SendIO((struct IORequest *)injector->timer);
    injector->pending = 1;
}

static int service_timer(void *data, ULONG signals)
{
    struct miga80_stop_injector *injector = data;

    (void)signals;
    if (CheckIO((struct IORequest *)injector->timer) == NULL) {
        return 1;
    }
    injector->pending = 0;
    if (WaitIO((struct IORequest *)injector->timer) != 0) {
        return 0;
    }
    if (injector->phase == 0U) {
        /* F5 during a run must not queue a second compilation. */
        if (!send_key(injector, 0x54U, 0U) ||
            !send_key(injector, 0xd4U, 0U)) {
            return 0;
        }
        injector->phase = 1U;
        arm_timer(injector, 50000U);
        return 1;
    }
    if (injector->phase == 1U) {
        if (!send_key(injector, 0x45U, 0U) ||
            !send_key(injector, 0x45U, IEQUALIFIER_REPEAT)) {
            return 0;
        }
        injector->phase = 2U;
        /* Bound the regression if the input event never reaches the window. */
        arm_timer(injector, 2000000U);
        return 1;
    }
    injector->phase = 3U;
    return 0; /* Watchdog is a test failure, never a successful Escape. */
}

struct miga80_stop_injector *miga80_stop_injector_start(
    struct miga80_supervisor_events *events)
{
    struct miga80_stop_injector *injector =
        AllocMem(sizeof(*injector), MEMF_PUBLIC | MEMF_CLEAR);

    if (injector == NULL) {
        return NULL;
    }
    injector->timer_port = CreateMsgPort();
    injector->input_port = CreateMsgPort();
    if (injector->timer_port == NULL || injector->input_port == NULL) {
        goto fail;
    }
    injector->timer = (struct TimeRequest *)CreateIORequest(
        injector->timer_port, sizeof(*injector->timer));
    injector->input = (struct IOStdReq *)CreateIORequest(
        injector->input_port, sizeof(*injector->input));
    if (injector->timer == NULL || injector->input == NULL ||
        OpenDevice(TIMERNAME, UNIT_VBLANK,
                    (struct IORequest *)injector->timer, 0U) != 0) {
        goto fail;
    }
    injector->timer_open = 1;
    if (OpenDevice("input.device", 0U, (struct IORequest *)injector->input,
                     0U) != 0) {
        goto fail;
    }
    injector->input_open = 1;
    events->signals = 1UL << injector->timer_port->mp_SigBit;
    events->service = service_timer;
    events->data = injector;
    arm_timer(injector, 100000U);
    return injector;

fail:
    (void)miga80_stop_injector_finish(injector, 0);
    return NULL;
}

int miga80_stop_injector_finish(struct miga80_stop_injector *injector,
                               int expect_escape)
{
    int success = !expect_escape || injector->phase == 2U;

    if (injector->pending) {
        if (CheckIO((struct IORequest *)injector->timer) == NULL) {
            AbortIO((struct IORequest *)injector->timer);
        }
        (void)WaitIO((struct IORequest *)injector->timer);
    }
    if (injector->input_open) {
        if (injector->phase >= 2U) {
            /* A held-key repeat after stopping must not close the source. */
            if (!send_key(injector, 0x45U, IEQUALIFIER_REPEAT) ||
                !send_key(injector, 0xc5U, 0U)) {
                success = 0;
            }
        }
        CloseDevice((struct IORequest *)injector->input);
    }
    if (injector->timer_open) {
        CloseDevice((struct IORequest *)injector->timer);
    }
    if (injector->input != NULL) {
        DeleteIORequest((struct IORequest *)injector->input);
    }
    if (injector->timer != NULL) {
        DeleteIORequest((struct IORequest *)injector->timer);
    }
    if (injector->input_port != NULL) {
        DeleteMsgPort(injector->input_port);
    }
    if (injector->timer_port != NULL) {
        DeleteMsgPort(injector->timer_port);
    }
    FreeMem(injector, sizeof(*injector));
    return success;
}
