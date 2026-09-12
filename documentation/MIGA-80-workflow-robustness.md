# MIGA-80 workflow robustness

Date: 2026-09-12. Physical A1200 feedback is still pending.

Validated locally: `gmake check`, the guarded Musashi suite, ADF `AUTORUN`,
and all 13 `SELFTEST` attempts pass. The ADF regressions used FS-UAE with
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
register set, including D2, on the compiler stack. It records that host stack
pointer in the runtime context before switching stacks. Normal completion
and the non-returning fault handler restore the same saved host state. Faults
can therefore skip generated epilogues, live spill frames, and runtime-call
return addresses. Runtime guards are checked after either exit; the compiler
guards are checked after restoring the original Amiga task stack. Temporary
code, compiler objects, and both dedicated stacks are released on failure too.

The normal UI routes F5, result/error, and Esc through the same state-transition
function as the automated regression. Esc returns to the source from either
result or error, then exits from source. F5 in result/error and key releases
are ignored. Runtime errors include the source line and column in the status
row. The budget is deterministic, not a time limit or responsive stop key:
polling Esc during execution and arbitrary CPU-exception recovery remain open.

## Regression commands

```sh
gmake runtime-guards-test
gmake check
gmake miga80-demo-adf-fs-uae-autorun
gmake miga80-demo-adf-fs-uae-workflow
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

Reports are retained in `build/reports/runtime-guards-host.txt`,
`build/reports/source-view-adf-autorun-fs-uae.txt`, and
`build/reports/source-view-adf-workflow-fs-uae.txt`. The common
`source-view-adf-fs-uae.txt` also holds the latest ADF test result.
Source fixtures and direct/GNU images are retained under
`build/host/runtime-guards`.

This covers the hosted vertical subset. It does not certify physical timing,
exclusive hardware takeover, responsive interruption, broader direct-encoder
instruction coverage, or long-duration resource soaks.
