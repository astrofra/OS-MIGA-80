#include <string.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <intuition/intuition.h>
#include <proto/exec.h>

#include "demo/supervisor.h"

#define WORKER_STACK_BYTES MIGA80_SUPERVISOR_STACK_BYTES
#define WORKER_GUARD_BYTES MIGA80_SUPERVISOR_GUARD_BYTES
#define ESCAPE_KEY 0x45U

struct supervised_job {
    struct Task task;
    struct Task *owner;
    APTR code;
    struct miga80_runtime_context *context;
    ULONG done_signal;
    volatile int started;
    volatile int finished;
};

/* The hosted shell runs one generated function at a time. */
static struct supervised_job *active_job;
extern ULONG miga80_execute_generated(APTR code, APTR context);

static void worker_entry(void)
{
    struct supervised_job *job = active_job;

    job->started = 1;
    (void)miga80_execute_generated(job->code, (APTR)job->context);

    /* Publish completion and remove ourselves without a scheduling gap.
     * RemTask(NULL) ends this task's Forbid state. No memory is freed here:
     * tc_MemEntry is empty and the owner retains both task and stack. */
    Forbid();
    job->finished = 1;
    Signal(job->owner, job->done_signal);
    RemTask(NULL);
    for (;;) { /* RemTask(NULL) cannot return. */ }
}

static int guards_intact(const UBYTE *stack)
{
    unsigned int index;

    for (index = 0U; index < WORKER_GUARD_BYTES; ++index) {
        if (stack[index] != 0x5aU ||
            stack[WORKER_GUARD_BYTES + WORKER_STACK_BYTES + index] != 0xa5U) {
            return 0;
        }
    }
    return 1;
}

int miga80_supervise_generated(
    APTR code, struct miga80_runtime_context *context, struct MsgPort *input,
    APTR runtime_stack_lower, UBYTE *stack,
    int *escape_held, const struct miga80_supervisor_events *events,
    struct miga80_supervisor_observation *observation)
{
    struct supervised_job *job = NULL;
    BYTE done_bit = -1;
    ULONG input_signal;
    ULONG extra_signals = events != NULL ? events->signals : 0U;
    int installed = 0;
    int stopped = 0;
    int success = 0;

    if (observation == NULL) {
        return 0;
    }
    (void)memset(observation, 0, sizeof(*observation));
    if (active_job != NULL || code == NULL || context == NULL ||
        input == NULL || escape_held == NULL || stack == NULL ||
        runtime_stack_lower == NULL ||
        (events != NULL && events->signals != 0U && events->service == NULL)) {
        return 0;
    }
    job = (struct supervised_job *)AllocMem(sizeof(*job),
                                            MEMF_PUBLIC | MEMF_CLEAR);
    done_bit = AllocSignal(-1);
    if (job == NULL || done_bit < 0) {
        goto cleanup;
    }
    (void)memset(stack, 0x5a, WORKER_GUARD_BYTES);
    (void)memset(stack + WORKER_GUARD_BYTES, 0xcd, WORKER_STACK_BYTES);
    (void)memset(stack + WORKER_GUARD_BYTES + WORKER_STACK_BYTES,
                 0xa5, WORKER_GUARD_BYTES);
    job->owner = FindTask(NULL);
    job->code = code;
    job->context = context;
    job->done_signal = 1UL << done_bit;
    input_signal = 1UL << input->mp_SigBit;
    job->task.tc_Node.ln_Type = NT_TASK;
    job->task.tc_Node.ln_Name = "MIGA-80 generated code";
    job->task.tc_Node.ln_Pri = job->owner->tc_Node.ln_Pri > -128
                                  ? job->owner->tc_Node.ln_Pri - 1 : -128;
    /* Both stacks and their guards occupy one owner-allocated arena, with
     * the generated stack below the worker's C stack. Exec's bounds remain
     * correct across the trampoline's A7 switch and all preemption points. */
    job->task.tc_SPLower = runtime_stack_lower;
    job->task.tc_SPUpper = stack + WORKER_GUARD_BYTES + WORKER_STACK_BYTES;
    job->task.tc_SPReg = job->task.tc_SPUpper;
    job->task.tc_MemEntry.lh_Head =
        (struct Node *)&job->task.tc_MemEntry.lh_Tail;
    job->task.tc_MemEntry.lh_TailPred =
        (struct Node *)&job->task.tc_MemEntry.lh_Head;
    (void)SetSignal(0U, job->done_signal);
    active_job = job;
    if (AddTask(&job->task, (APTR)worker_entry, NULL) == NULL) {
        goto cleanup;
    }
    installed = 1;

    for (;;) {
        struct IntuiMessage *message;

        /* Drain the queue before Wait: a signal is not a message count.
         * Esc wins a simultaneous completion/key event. F5 and unrelated
         * input while a program is running are consumed, never replayed. */
        while ((message = (struct IntuiMessage *)GetMsg(input)) != NULL) {
            const ULONG message_class = message->Class;
            const UWORD key = message->Code;

            ReplyMsg((struct Message *)message);
            if (message_class == IDCMP_RAWKEY) {
                if (key == (ESCAPE_KEY | 0x80U)) {
                    *escape_held = 0;
                } else if (key == ESCAPE_KEY) {
                    *escape_held = 1;
                    stopped = 1;
                }
            }
        }
        if (stopped || job->finished) {
            success = 1;
            break;
        }
        {
            const ULONG signals = Wait(job->done_signal | input_signal |
                                         extra_signals);

            if ((signals & extra_signals) != 0U &&
                !events->service(events->data, signals & extra_signals)) {
                break;
            }
        }
    }

cleanup:
    if (installed) {
        /* Only our resource-free worker is removed from another task. The
         * completion flag and self-removal are atomic relative to this
         * section. Never remove an already self-removed task. */
        Forbid();
        if (!job->finished) {
            RemTask(&job->task);
        }
        observation->started = job->started;
        observation->finished = job->finished;
        observation->interrupted = stopped;
        Permit();
        observation->stack_intact = guards_intact(stack);
        success = success && observation->stack_intact;
        if (stopped) {
            context->fault_code = MIGA80_ABI_FAULT_USER_STOP;
            context->fault_line = 0U;
            context->fault_column = 0U;
        }
    }
    active_job = NULL;
    if (done_bit >= 0) {
        (void)SetSignal(0U, 1UL << done_bit);
        FreeSignal(done_bit);
    }
    if (job != NULL) {
        FreeMem(job, sizeof(*job));
    }
    return success;
}
