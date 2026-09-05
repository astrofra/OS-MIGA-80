# MIGA-80

Fantasy OS for the Amiga 1200

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
its typed-IR evaluator plus `-O1` renderer under `vamos` with:

```sh
gmake compiler-amiga-test
```

The implemented subset accepts one explicitly annotated typed function with up
to three scalar `i8`/`u8`/`i16`/`u16`/`i32`/`fix`/`bool`/`symbol` parameters, two
`string` parameters, and 16 explicitly typed locals,
initialized declarations, reassignment, one final return, arithmetic, all six
comparisons (`!=` aliases `~=`), nested `if`/`then`/`else`/`end`, and nested
`while`/`do`/`end` loops with loop-carried values, `break`, and `continue`.
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
