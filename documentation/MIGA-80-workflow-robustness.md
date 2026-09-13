# MIGA-80 workflow robustness

Date: 2026-09-12. Physical A1200 feedback is still pending.

Validated locally: `gmake check`, the guarded Musashi suite, supervised ADF
`AUTORUN`, all 13 `SELFTEST` attempts, ten `STOPTEST` stops, and the direct
`NOSUPERVISOR` comparison pass. The ADF regressions used FS-UAE with
Kickstart 3.0 (39.106), 2 MiB Chip RAM, and no Fast RAM. The final workflow
report confirms source recovery, stable warmed free memory, and hosted cleanup.

## Shipping execution contract

F5 accepts a parameterless `void` function and emits guarded O1 code. Each
transfer to an equal or earlier block in emission order checks the unsigned
counter at `12(A5)`. Zero raises controlled fault 3 before any decrement;
otherwise the transfer consumes one unit. Every generated control-flow cycle
contains such an edge, including empty loops and `continue` funnels. A budget
of N permits exactly N backward transfers. Straight-line code can still run
with a zero budget. The fault carries the source location of the transfer.

The default budget is 1,000,000 transfers. Mandelbrot consumes 160,288 and
leaves 839,712 (139,680 inner iterations + 20,480 columns + 128 rows, also
checked with an independent scalar Q16.16 source simulation). Its guarded image is 464 bytes (`72eef2e6`), compared with the
404-byte unguarded O1 baseline. Both produce framebuffer checksum `c4604fc7`.
The guarded direct and GNU assembly encoders must produce identical bytes.
The original unguarded O0/O1 suites remain semantic and optimization baselines.

Compilation uses the existing guarded 32 KiB Exec `StackSwap` stack. Generated
execution uses a separate 4 KiB stack with 256-byte guards on both sides.
Before entry, the encoder's conservative stack bound must fit that stack
(96 bytes for the default program). The bound covers the accepted numeric and
trusted `pset` subset; future user calls require extending it.

The trampoline in `src/demo/runtime_guarded.S` saves the full Amiga C preserved
register set, including D2, on its caller's stack (the supervised worker stack
by default, or the compiler stack with `NOSUPERVISOR`). It records that stack
pointer in the runtime context before switching stacks. Normal completion
and the non-returning fault handler restore the same saved host state. Faults
can therefore skip generated epilogues, live spill frames, and runtime-call
return addresses. Runtime guards are checked after either exit; the compiler
guards are checked after restoring the original Amiga task stack. Temporary
code, compiler objects, and all dedicated stacks are released on failure too.

The normal UI routes F5, result/error, Esc, and Ctrl-Q through the same
state-transition function as the automated regression. Esc returns to the
source from either result or error and does nothing in source. Ctrl-Q closes
the application from source, result, or error. F5 in result/error and key
releases are ignored. Runtime errors include the source line and column in the
status row. During supervised execution, Esc instead stops the task and returns
directly to the source with `STOPPED - F5 RUN - CTRL-Q EXIT`. Esc never exits
the application. Held Esc repeats are ignored until key-up. F5 and Ctrl-Q
pressed while running are consumed rather than replayed after stopping.

Ctrl-Q is translated with the active AmigaOS keymap through
[MapRawKey](https://developer.amigaos3.net/autodocs/keymap.library/MapRawKey.html),
so it follows the logical letter Q on QWERTY or AZERTY. It requires the Control
qualifier and a new key-down event: plain Q, Ctrl-A, key-up, repeats, and Alt or
Amiga combinations do not quit. The SELFTEST checks Esc in source and these
basic qualifier/release cases using the boot ADF's default US keymap.

## Hosted development supervisor

Supervision is enabled by default. The UI/compilation task retains every
allocation and runs native code in a disposable Exec task at one priority level
below its own (clamped at -128). The owner waits on its Intuition keyboard port
and a private completion signal. Esc wakes the owner through normal OS input
and scheduling, so it can remove a worker stuck in an empty native loop or
inside a trusted service that never returns. No compiler polling or budget
exhaustion is required. Compilation itself is not cancellable.

The worker has a guarded 4 KiB C stack above the guarded 4 KiB generated stack
in one allocation. Exec's stack bounds cover both stacks throughout the
trampoline's stack switch. The worker performs numeric code and the bounded
memory-only `pset` service. It must never own allocations, DOS activity, locks,
pending I/O, DMA, or other resources requiring cleanup. Display publication,
memory allocation, and device I/O stay in the owner. This narrow contract
is what allows forced task
removal; adding resource-owning services requires a different stop protocol.

On normal return or controlled fault, the worker enters `Forbid`, publishes
completion, signals the owner, and removes itself. The owner tests completion
and removes an unfinished worker inside its own `Forbid` section, preventing a
second removal if completion races with Esc. It frees the code, task storage,
signals, and stacks only after the worker has gone. Stack guards and the parent
compiler-stack restoration are checked on the stopped path too. An observed
Esc wins a simultaneous completion/key event. These choices follow the
documented [AddTask](https://developer.amigaos3.net/autodocs/exec.library/AddTask.html),
[RemTask](https://developer.amigaos3.net/autodocs/exec.library/RemTask.html), and
[Wait](https://developer.amigaos3.net/autodocs/exec.library/Wait.html) contracts;
`RemTask` explicitly warns against removing arbitrary resource-owning tasks.

The owner records user stop as context code 4 with source line/column zero,
not as a generated fault-handler call. The interactive diagnostic report uses
`miga80_source_view_report=3`, `native_execution=stopped`, and `result=stopped`
after restoration. Stopping does not claim a source location or a completed
framebuffer. Successful reports identify `supervision=exec-task` or `disabled`.

To compare the former direct path, launch from Shell with:

```text
MIGA80:MIGA80 MIGA80:DATA/DEFAULT.LUA RAM:MIGA80-BOOTED.TXT NOSUPERVISOR
```

`SUPERVISOR` explicitly enables the default. Optional flags follow the source
and report paths, and follow `AUTORUN`, `SELFTEST`, or `STOPTEST` when using an
automated mode. The last supervision flag wins. `NOSUPERVISOR` retains the
backward-transfer budget but cannot process Esc until generated code exits;
`STOPTEST` rejects this option.

This is an OS task supervisor, not the 68020 supervisor CPU mode. It relies on
working interrupts and task scheduling, and cannot guarantee recovery after
arbitrary CPU exceptions, memory corruption, `Disable`, or `Forbid` in defective
native code. The source-language subset exposes none of those operations.
The future exclusive runtime, which prevents task rescheduling, still needs its
own input interrupt and stop protocol. No physical latency guarantee is claimed.

## Regression commands

```sh
gmake runtime-guards-test
gmake check
gmake miga80-demo-adf-fs-uae-autorun
gmake miga80-demo-adf-fs-uae-workflow
gmake miga80-demo-adf-fs-uae-stop
gmake miga80-demo-adf-fs-uae-direct
```

The host test compares both encoders and executes the shipping assembler
trampoline and real `pset` service under Musashi. It covers the exact budget
boundary, zero and insufficient budgets, an empty `continue` loop, an empty
loop with source locations above 127, a forced
fault with an unbalanced stack frame and damaged preserved registers, and the
complete Mandelbrot result. Tests check the returned fault/location/counter,
framebuffer, Amiga C preserved registers, restored host stack, and stack guards.

The FS-UAE `SELFTEST` mode runs one warmup followed by three rounds of a syntax
error, an infinite-loop budget fault, a forced runtime-service fault, and a
successful recompiled Mandelbrot: 13 compile/run attempts. Every result or
error returns through the shared Esc handler to a source framebuffer verified
by checksum and AGA pixel readback. It checks free public memory against the
warmed baseline after each round. The final workflow report is written only
after the hosted window, screen, buffers, and libraries have been released.

The forced-fault service is private test instrumentation selected only by
`SELFTEST`; it introduces no source-language API or precompiled fractal.
Intermediate test cases do not write reports, so disk/RAM-file allocation
does not contaminate the repeated-run memory comparison. The FS-UAE harness
uses a disposable ADF copy, while normal interactive diagnostics remain in RAM.

`STOPTEST` runs one guarded-loop warmup, then three rounds of a guarded infinite
loop, the same source recompiled without budget instrumentation, and a `pset`
service replaced by a native self-branch: ten stopped runs. A timer serviced
by the owner sends F5 and then Esc through `input.device` and Intuition, including
held-key repeats and release. A separate watchdog turns missing Esc delivery
into a failure. Only real Esc delivery counts as a successful stop; the test
checks a started, unfinished, removed worker and a non-exhausted budget. The
unguarded loop must leave that budget unchanged. Source checksum/AGA readback,
exact compile count, signal allocation, guards, warmed free memory, and a final
Mandelbrot run verify recovery. The warmup exercises the interactive stopped
report after compiler-stack restoration. Final success is written after hosted
cleanup. Phase markers are written outside measured repeated rounds for timeout
diagnosis.

The injection follows
[IND_WRITEEVENT](https://developer.amigaos3.net/autodocs/input.device/IND_WRITEEVENT.html)
one event at a time, because that command ignores `ie_NextEvent`. Pending timer
I/O is aborted if necessary and always joined with `WaitIO` before its request
or port is freed, as required by
[AbortIO](https://developer.amigaos3.net/autodocs/exec.library/AbortIO.html).
The private unguarded and stalled-service fixtures introduce no language API.
The direct comparison launches `AUTORUN NOSUPERVISOR` and requires the same
generated image, framebuffer, budget remainder, and restored compiler stack as
supervised `AUTORUN`.

Reports are retained in `build/reports/runtime-guards-host.txt`,
`build/reports/source-view-adf-autorun-fs-uae.txt`, and
`build/reports/source-view-adf-workflow-fs-uae.txt`,
`build/reports/source-view-adf-stop-fs-uae.txt`, and
`build/reports/source-view-adf-direct-fs-uae.txt`. The common
`source-view-adf-fs-uae.txt` also holds the latest ADF test result.
Source fixtures and direct/GNU images are retained under
`build/host/runtime-guards`.

This covers the hosted vertical subset. It does not certify physical timing,
exclusive hardware takeover or its stop protocol, broader direct-encoder
instruction coverage, or long-duration resource soaks.
