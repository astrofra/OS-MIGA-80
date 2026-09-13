#ifndef MIGA80_DEMO_SUPERVISOR_H
#define MIGA80_DEMO_SUPERVISOR_H

#include <exec/types.h>
#include "compiler/abi/runtime.h"

struct MsgPort;

#define MIGA80_SUPERVISOR_STACK_BYTES 4096U
#define MIGA80_SUPERVISOR_GUARD_BYTES 256U
#define MIGA80_SUPERVISOR_STACK_TOTAL_BYTES \
    (MIGA80_SUPERVISOR_STACK_BYTES + 2U * MIGA80_SUPERVISOR_GUARD_BYTES)

struct miga80_supervisor_observation {
    int started;
    int finished;
    int interrupted;
    int stack_intact;
};

/* Extra events are serviced by the owner, never by the disposable worker.
 * Used by the regression to inject keyboard events through input.device. */
struct miga80_supervisor_events {
    ULONG signals;
    int (*service)(void *data, ULONG signals); /* false = test/I/O failure */
    void *data;
};

/* Only generated numeric code and resource-free trusted services may run in
 * this task. The owner retains every allocation and removes the worker before
 * freeing any of them. No DOS, allocation, asynchronous I/O, locks or DMA in
 * the worker; extending that contract requires a different stop protocol.
 * runtime_stack_lower through the end of worker_stack must be one contiguous
 * owner-allocated arena. worker_stack reserves STACK_TOTAL_BYTES above the
 * runtime stack's upper guard. The owner checks the runtime guards too. */
int miga80_supervise_generated(
    APTR code, struct miga80_runtime_context *context, struct MsgPort *input,
    APTR runtime_stack_lower, UBYTE *worker_stack,
    int *escape_held, const struct miga80_supervisor_events *events,
    struct miga80_supervisor_observation *observation);

#endif
