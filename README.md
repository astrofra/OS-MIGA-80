# MIGA-80

Fantasy OS for the Amiga 1200

The reference distribution is the versioned `release/miga80-<version>.adf`, a
bootable development preview containing all six Lua demos in `SYS:demos`.
Startup shows the centered project logo with a brief glitch effect and an
original “A–MI–GA” jingle synthesized on the 68000 and played through Paula.
ESC skips the intro. See [boot intro and tests](documentation/MIGA-80-boot-intro.md).
Its graphical selector supports mouse selection, double-click loading, folder
navigation and pagination. Ctrl+O / **OPEN** reopens it, F5 runs the source, ESC
stops or returns, and CTRL-Q exits. The source view is now an editable LORES
viewport: type to edit; use the arrows (with Shift for selection),
Ctrl+C/Ctrl+X/Ctrl+V for its internal clipboard, Ctrl+S to save, and
Shift+Ctrl+S for Save As. Documents are limited to 16 KiB; long lines and files
scroll rather than being rejected. The planned HIRES editor is not implemented
yet. Build with `gmake release`; validate with
`gmake release-fs-uae`. The ADF and payload manifest are kept in `release/`
for version control. See [distribution instructions](release/README.md).
The [solid cube demos](documentation/MIGA-80-solid-cube.md) add flat face lighting,
direct blitter area fill and a chunky ASM span filler. Both now play the supplied
ProTracker module through Paula and CIA interrupts; see the [Lua music API](documentation/MIGA-80-mod-playback.md).

The first hosted AmigaOS bootstrap can be built and tested from macOS with:

```sh
gmake check
```

This runs the native compiler/typed-IR tests, generated 68020 code under
Musashi, the legacy eight-plane and new four-plane chunky-to-planar golden
vectors, three-layer graphics oracle, inverse AGA decoder differential, and
graphics-report schema tests. It also compiles and inspects the Hunk
executables, runs the hosted bootstrap through `vamos` and FS-UAE, then
executes the 256 × 256 AGA screen regression under FS-UAE.

Native drawing now targets both playfields: `layer(PIXEL)` selects PF1/front
with CPU Bresenham and C2P; `layer(PLANAR)` selects PF2/back with direct blitter
line mode. The ADF includes `DATA/LAYERS.LUA`. See
[drawing primitives and tests](documentation/MIGA-80-drawing-primitives.md).

The Lua wireframe cube adds fixed-point `sin`/`cos`, `cls`, `flip` and `time`,
with direct blitter drawing, synchronized double buffering and a ten-second
animation. Build its autoboot disk with `gmake miga80-cube-adf`; see
[cube animation and runtime](documentation/MIGA-80-cube-animation.md).

The chunky cube uses the Kalms 68020 C2P adaptation by default, selected on
2026-09-14. Our mask32 assembly and scalar reference remain selectable for comparison. `gmake miga80-cube-c2p-compare` runs
the three backends on the same ADF; see the [comparison and measurements](documentation/MIGA-80-c2p-runtime-comparison.md).
`gmake miga80-cube-chunky-kalms-adf` builds the Kalms autoboot variant.

Run only the portable three-layer graphics oracle natively with:

```sh
gmake graphics-reference-test
```

Build the pinned Musashi core and run the first host-side 68EC020 execution
harness with:

```sh
gmake miga68k-test
```

The first invocation fetches the pinned Musashi source archive and verifies its
SHA-256 checksum. The current Phase 0 harness executes a reviewed `mul_add`
assembly fixture through GNU `as`/`objcopy`, checks its 32-bit result, stack
balance, callee-saved registers, memory guards, and instruction limit, and
retains a short disassembly trace on failure. It is the foundation for the Lua
compiler path exercised below.

Build the initial typed MIGA Lua compiler, verify native ABI 0.6,
and validate generated 68020 code against its typed-IR oracle with:

```sh
gmake compiler-abi-test compiler-test compiler-execute-test compiler-spill-test
```

Cross-build the same portable C99 compiler bootstrap for 68020/libnix and run
its typed-IR evaluator, `-O1` renderer, and direct O1 encoder under `vamos`
with:

```sh
gmake compiler-amiga-test
```

The implemented subset accepts one explicitly annotated typed function with up
to three scalar `i8`/`u8`/`i16`/`u16`/`i32`/`fix`/`bool`/`symbol` parameters, two
`string` parameters, and 24 explicitly typed locals,
initialized declarations, reassignment, one final return, arithmetic, all six
comparisons (`!=` aliases `~=`), nested `if`/`then`/`else`/`end`, and nested
`while`/`do`/`end` loops with loop-carried values, `break`, and `continue`.
It also accepts an explicit `void` result and the statement-only
`pset(i32, i32, u8)`, `layer(PLANAR/PIXEL)`, and
`line(i32, i32, i32, i32, u8)`, `tri(x0,y0,x1,y1,x2,y2,color)`, `cls(u8)` and `flip()` runtime intrinsics.
`sin(fix)`, `cos(fix)` and `time()` return `fix` values.
Signed integer `/` truncates toward zero, unsigned integer `/` uses `DIVU.L`, and
statement-only `/=` follows the target numeric type; all use controlled
division-by-zero faults. Narrow arithmetic wraps at its declared width and is
kept in canonical sign- or zero-extended 32-bit ABI values. Signed Q16.16
`fix` values support decimal literals, `+`, `-`, unary negation, multiplication,
division, comparisons, explicit checked `fix(i32)`, and truncating `i32(fix)`
with bit-exact host/68020 semantics. The typed IR carries
up to 32 basic blocks.
Multiple loop-control sites are folded through bounded binary funnels into a
canonical latch and exit. Assembly generation defaults to the value-IR `-O1` backend;
`-O0` keeps the stack baseline for comparison. `-O1` removes dead assignments,
creates typed branch and loop join values, computes cyclic CFG-aware liveness,
coalesces compatible `phi` slots, schedules parallel edge copies, and uses
bounded ABI frames when transfers or register pressure require them. See the [compiler
bootstrap](documentation/MIGA-Lua-compiler-bootstrap.md)
for its exact grammar, the [native ABI
0.6](documentation/MIGA-Lua-native-ABI-v0.md) for the frozen register, stack,
immutable-value, and fault core, and the [optimization
strategy](documentation/MIGA-Lua-optimization-strategy.md) for the bounded
on-Amiga compiler plan.

The version 1 language contract is deliberately strict: parameter, return
(including `void`), and local types are explicit; runtime implicit conversions,
polymorphic/union types, and multiple returns are excluded from version 1. The
bootstrap only adapts an `i32` constant expression implicitly when it provably
fits its narrow destination. Integer/fixed conversions use explicit syntax;
an out-of-range dynamic `fix(i32)` reaches controlled fault 2. Future fixed
arrays are zero-based, with valid indices `0`
through `N - 1`.
There are no `byte`/`word` aliases. Immutable short `string` literals use
single or double quotes and a bounded deduplicated descriptor pool;
`symbol("name")` creates an opaque interned scalar ID. Equality is constant
time for both, no implicit conversion exists between them, string values use
the ABI address class, and emitted pool references are PC-relative.

Run the inverse dual-playfield decoder and compositor → C2P → decoder differential with:

```sh
gmake aga-reference-test
```

Validate the common Phase 0 graphics benchmark report contract with:

```sh
gmake graphics-report-test
```

Run only the historical two-layer/eight-plane C99 converter tests natively on macOS with:

```sh
gmake c2p-test
```

Run the historical four-layout C2P measurement protocol under FS-UAE with:

```sh
gmake c2p-benchmark-fs-uae
```

Its scalar-C timings validate the benchmark infrastructure; the runtime layout remains open until optimized candidates are measured on a real stock A1200.

Run the three-profile, single-layer C2P4 correctness suite natively with:

```sh
gmake c2p4-test
```

Build and execute the C99 pair-LUT, 68020 pair-LUT/mask32, and staged blitter-publication matrix under FS-UAE with:

```sh
gmake c2p4-benchmark-fs-uae
```

This validates all 24 optimized-path cases, conservative traffic accounting, and exact canonical output. FS-UAE timings do not select a runtime layout or backend; an exclusive-runtime harness, physical Chip-RAM calibration, genuine CPU/blitter conversion, and stock-A1200 measurements remain open.

Run the hosted 256×256 AGA dual-playfield smoke test separately with:

```sh
gmake aga-screen-smoke
```

Build and boot-test the first complete MIGA-80 vertical slice with:

```sh
gmake source-view-test miga80-demo-adf-inspect
gmake miga80-demo-adf-fs-uae
gmake compiler-encoder-musashi-test
gmake compiler-call-test runtime-guards-test
gmake miga80-demo-adf-fs-uae-autorun
gmake miga80-demo-adf-fs-uae-workflow
gmake miga80-demo-adf-fs-uae-stop
gmake miga80-demo-adf-fs-uae-direct
```

This produces `build/distribution/miga80-source-view.adf`. The standalone OFS
disk launches MIGA-80, loads `DATA/DEFAULT.LUA`, and displays its complete
30-line Mandelbrot source with the project 4×8 bitmap font. `F5` parses and
lowers that source on the Amiga, builds the value IR, applies the bounded O1
register plan, and directly emits a guarded 464-byte 68020 function in
RAM, synchronizes the instruction cache, and executes it through the private
runtime ABI. The generated function performs 20,480 `pset` calls and publishes
the resulting 160×128 Mandelbrot viewport. The exact ADF boot and an automated
on-target `AUTORUN` variant are checked under FS-UAE; the latter must match the
typed-IR and Musashi framebuffer checksum `c4604fc7`.

The F5 path checks the 68020 requirement, allocates a guarded private 32 KiB
compiler stack, and enters it with Exec `StackSwap`. This is independent of
the boot Shell and of the stack assigned to a manual CLI launch. Generated
code uses a separate guarded 4 KiB stack and a budget of 1,000,000 backward
transfers. Budget exhaustion and controlled faults restore the host stack and
return to the error/source workflow. Generated code runs in a separate Exec
task by default: `Esc` stops execution and returns directly to the source,
including when the code cannot reach a budget check. `Esc` never closes the
application; use `Ctrl-Q` from the source, result, or error screen to quit.
The shortcut follows the active AmigaOS keyboard layout. The SELFTEST
regression exercises repeated success and error cycles; STOPTEST injects real
keyboard events to stop guarded/unguarded loops and a stalled service. See
[workflow robustness](documentation/MIGA-80-workflow-robustness.md).
Interactive diagnostics are written to `RAM:MIGA80-BOOTED.TXT`, keeping the
distribution ADF unchanged and preventing emulator save-disk overlays from
outliving the disk layout they were created for.

For development comparisons, a manual Shell launch can disable supervision:
`MIGA80:MIGA80 MIGA80:DATA/DEFAULT.LUA RAM:MIGA80-BOOTED.TXT NOSUPERVISOR`.
The backward-transfer budget remains active. `SUPERVISOR` explicitly enables
the default again; with an automated mode, put the option after that mode.
The supervisor relies on working OS scheduling and interrupts. It cannot
recover arbitrary memory corruption or replace the future exclusive-mode
stop protocol. Physical A1200 validation is still pending.

The unguarded 404-byte O1 baseline is byte-identical to its GNU-assembly
oracle. Under Musashi it executes 7,466,958 instructions, versus 17,314,258 for the original
744-byte O0 direct image; these are deterministic regression counts, not
cycle-accurate A1200 timings. A focused `pset` test also proves that values live
across a call are moved out of caller-saved `D0-D2`. See the
[Mandelbrot vertical-slice plan](documentation/MIGA-80-mandelbrot-vertical-slice.md).

The target is currently locked to the `libnix` Kickstart 2+ startup/runtime with
`-mcrt=nix20`. Reproduce the `newlib`/`libnix`/`clib2` allocation and filesystem
comparison with:

```sh
gmake runtime-compare
```

Launch it interactively under the configured Workbench 3.0 FS-UAE profile with:

```sh
gmake run
```

See the [macOS development toolchain guide](documentation/macos-development-toolchain.md) for local ROM/HDF configuration and validated versions, the [three-layer graphics reference](documentation/graphics-reference-compositor.md) for canonical composition semantics, the [AGA reference decoder](documentation/aga-reference-decoder.md) for inverse playfield validation, the [graphics benchmark report format](documentation/graphics-benchmark-report-format.md) for comparable Phase 0 evidence, the [four-plane C2P benchmark](documentation/c2p4-benchmark.md) for the current viewport converters, the [exclusive graphics benchmark plan](documentation/graphics-exclusive-benchmark.md) for authoritative hardware timing and takeover diagnostics, the [legacy C2P reference note](documentation/c2p-reference.md) and [layout benchmark](documentation/c2p-layout-benchmark.md) for the historical two-layer experiments, and the [AGA screen smoke-test note](documentation/aga-screen-smoke.md) for the provisional playfield mapping and current validation boundary.
