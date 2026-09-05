# MIGA-80 Local 68020 Tooling

## Fast compiler development without launching UAE

**Status:** runner, exact-width typed compiler, signed/unsigned division,
cyclic CFG/loop `phi` value-IR `-O1`, and spills implemented

**Primary target:** stock Amiga 1200, 68EC020 at approximately 14 MHz  
**Host platforms:** macOS, Linux, and Windows  
**Scope:** local testing of the MIGA-80 Lua compiler and its generated 68020 code

---

## 1. Objective

The MIGA-80 Lua compiler should be testable as an ordinary host-side program. Most compiler work must not require booting an Amiga, preparing an ADF or HDF image, launching UAE, or interacting with Workbench.

The intended development loop is:

```text
MIGA-80 Lua source
      |
      v
compiler frontend and optimizer
      |
      v
68020 assembly or object code
      |
      v
local assembler/linker
      |
      v
raw 68k executable image
      |
      v
embedded 68EC020 emulator
      |
      v
register, memory and trace assertions
```

This local runner validates the compiler. UAE and real hardware validate the complete MIGA-80 machine.

---

## 2. Recommended tool stack

| Component | Recommended tool | Role |
|---|---|---|
| CPU execution | **Musashi** | Embedded 68EC020 execution core for fast unit tests |
| Secondary CPU oracle | **Moira** | Independent cross-checks and more timing-oriented experiments |
| Assembler | **vasm** or GNU `m68k-elf-as` | Converts generated assembly into relocatable code |
| Linker | GNU `m68k-elf-ld` or `vlink` | Places code, constants and test data at deterministic addresses |
| Binary extraction | `m68k-elf-objcopy` | Produces a flat binary for the runner |
| Inspection | `m68k-elf-objdump`, Musashi disassembler | Disassembly, symbols and failure traces |
| Test framework | Small native C/C++ runner | Loads programs, invokes functions and checks results |
| Build/test driver | CMake + CTest, or the existing project build system | Reproducible local and CI execution |

### Primary recommendation: Musashi

[Musashi](https://github.com/kstenerud/Musashi) is a portable Motorola 680x0 emulator written in C. It supports both the 68EC020 and the full 68020, exposes register access, provides an instruction hook, and includes a disassembler. Its host integration consists mainly of supplying memory read/write callbacks.

These properties make it a good fit for a compiler test harness:

- small conceptual integration surface;
- direct control over RAM, stack, PC and registers;
- deterministic execution with no operating system;
- instruction-level tracing on failed tests;
- usable on macOS, Linux and Windows;
- permissive license.

The runner should select `M68K_CPU_TYPE_68EC020`, matching the CPU class used by a stock Amiga 1200 more closely than a full 68020 configuration.

### Secondary recommendation: Moira

[Moira](https://dirkwhoffmann.github.io/Moira/) supports the 68000, 68010, 68EC020 and 68020. It is written in C++20 and was designed for accurate Amiga CPU timing and memory-access sequencing.

Moira is attractive for:

- differential tests against an implementation independent from Musashi;
- investigating instruction timing;
- future integration into a more complete virtual MIGA-80 machine;
- catching emulator-specific behaviour in edge cases.

For the first compiler runner, however, Musashi is the simpler baseline. Supporting two CPU engines immediately would add engineering work before the compiler has enough behaviour to justify it.

### Why not QEMU or MAME first?

- **QEMU m68k** is useful for complete m68k systems and operating-system binaries, but it is less convenient for invoking a tiny generated function and inspecting its registers immediately afterwards.
- **MAME's 68k implementation** is mature, but extracting and maintaining it as a narrow compiler-test dependency is needlessly heavy.
- **UAE** remains essential, but it solves the integration problem rather than the compiler unit-testing problem.

---

## 3. Three validation levels

The tooling should deliberately separate correctness, approximate CPU cost, and actual Amiga behaviour.

| Level | Environment | What it validates | What it does not validate |
|---|---|---|---|
| 1 | Native compiler tests | Lexer, parser, types, IR and diagnostics | Generated machine-code behaviour |
| 2 | Embedded 68EC020 core | ABI, arithmetic, branches, memory, calls and code generation | AGA, Copper, Blitter, DMA contention and exact A1200 performance |
| 3 | UAE and real A1200 | Complete runtime, hardware interaction, frame deadlines and compatibility | Fast isolated compiler diagnosis |

The important rule is:

> Passing the local 68EC020 runner means that generated code is functionally correct under the defined MIGA-80 execution model. It does not prove that a cartridge meets its frame budget on an Amiga 1200.

CPU-core cycle counts are still useful as a stable regression metric. They must not be presented as exact wall-clock predictions for code that accesses Chip RAM or runs concurrently with display, Copper, audio or Blitter DMA.

---

## 4. Minimal virtual machine used by compiler tests

The first runner does not need to emulate an Amiga. It only needs a deliberately small execution environment.

### Suggested memory map

| Address range | Purpose |
|---|---|
| `0x000000-0x0003FF` | Exception vectors and runner-owned control data |
| `0x001000-0x00FFFF` | Generated code and read-only constants |
| `0x010000-0x07FFFF` | Globals, heap and test fixtures |
| `0x080000-0x08FFFF` | Test stack |
| `0x0F0000-0x0F00FF` | Synthetic host-call and test-control ports |

These addresses are only a proposal for the local harness. They are not part of the cartridge ABI unless the project later chooses to formalize them.

The runner should emulate a 24-bit address space and reject accesses outside its declared memory map. This catches invalid pointers much earlier than a permissive flat host allocation would.

### Endianness and alignment

The 68k is big-endian. The host runner must therefore implement explicit 8-, 16- and 32-bit memory callbacks rather than exposing host pointers directly.

Tests should cover:

- big-endian word and longword layout;
- signed and unsigned extension;
- aligned stack and data accesses;
- the exact behaviour chosen for odd-address word and longword accesses;
- 24-bit address truncation or rejection, according to the MIGA-80 contract.

Even if generated code is never supposed to perform an unaligned access, detecting it is valuable because it usually identifies a compiler or ABI bug.

---

## 5. Freeze a small compiler ABI early

The local runner becomes much simpler once the compiler has a stable calling
convention. The frozen bootstrap convention is shown below. It is versioned in
[MIGA Lua Native ABI 0.6](./MIGA-Lua-native-ABI-v0.md); extensions still need
measurement and an explicit compatible revision or version bump.

| Resource | Proposed use |
|---|---|
| `D0` | Primary scalar return value |
| `D0-D2` | Integer or fixed-point arguments and caller-saved temporaries |
| `A0-A1` | Pointer arguments and caller-saved temporaries |
| `D3-D7` | Callee-saved values |
| `A2-A4` | Callee-saved address values |
| `A5` | Callee-saved immutable runtime-context pointer |
| `A6` | Optional frame pointer; omit in leaf functions when possible |
| `A7` | Stack pointer |

ABI 0.6 includes signed Q16.16 `fix`, explicit `i32`/`fix` conversions,
immutable `string` descriptors, and interned
`symbol` IDs to the canonical 32-bit `bool`/integer register and slot contract. It retains
per-width wrapping, four-byte stack alignment, register-only bootstrap
arguments, frames of at most 32,768 bytes, and the first runtime-context entry
for a non-returning controlled fault handler. The following extensions remain
later decisions:

- array descriptor layout and mutable storage;
- multiple return values, if supported;
- additional error and trap conventions;
- the rest of the runtime-context layout and jump table.

The runner should treat ABI preservation as testable behaviour. It can initialize every callee-saved register with a recognizable pattern before invoking generated code and verify the pattern after return.

---

## 6. Executable test protocol

Each compiled test should expose one or more entry points. The host obtains their addresses from a generated symbol manifest or from the linked ELF symbol table.

For a simple test:

```lua
function mul_add(a, b)
    return a * 3 + b
end
```

the runner performs the following operation:

```text
1. Clear and initialize virtual RAM.
2. Load the linked image.
3. Select the 68EC020 core.
4. Set D0 = a and D1 = b.
5. Set A7 to the top of the test stack.
6. Push a runner-controlled return sentinel.
7. Set PC to mul_add.
8. Execute until the return sentinel, a trap, or a cycle limit.
9. Assert that D0 contains the expected value.
10. Check stack balance, preserved registers and memory guards.
```

### Safe termination

Do not merely execute an arbitrary fixed number of cycles and assume the function has returned. Use one of these explicit mechanisms:

1. push an unmapped sentinel return address and stop when the PC reaches it;
2. reserve a `TRAP` instruction as a runner exit operation;
3. reserve a memory-mapped control write as `TEST_EXIT`.

Always retain a cycle/instruction ceiling as protection against infinite loops.

### Host calls

Runtime services such as `sin()`, sprite submission or debug output need not be implemented in 68k assembly during early compiler work. They may be represented by reserved traps or writes to a synthetic I/O page.

The runner can then:

- validate the arguments according to the ABI;
- record the call;
- return a deterministic result;
- reject calls unavailable in the tested language profile.

This creates a mock MIGA-80 runtime without pretending to emulate AGA hardware.

---

## 7. Test categories

### Frontend and language semantics

- lexical and syntax errors;
- static typing and conversions;
- constant folding;
- scope and shadowing;
- definite return and unreachable-code diagnostics;
- explicitly unsupported Lua features.

### Integer and fixed-point code generation

- boundary values: `0`, `1`, `-1`, minimum and maximum;
- add, subtract, multiply, divide and modulo;
- overflow policy;
- shifts and bitwise operations;
- comparison and Boolean normalization;
- fixed-point rounding, saturation or wrapping rules.

### Control flow

- conditional branches;
- short-circuit operators;
- loops with zero, one and many iterations;
- nested control flow;
- forward and backward branch ranges;
- `break`, `continue` or their chosen equivalents.

### Functions and ABI

- leaf and non-leaf functions;
- recursion, if allowed;
- arguments in registers and on the stack;
- saved-register preservation;
- stack balance;
- calls across separate compilation units;
- runtime-service calls.

### Memory and data structures

- globals and locals;
- arrays and tables in the restricted MIGA-80 model;
- bounds checks, if enabled;
- strings and constant pools;
- pointer arithmetic generated internally;
- guard regions around buffers.

### Negative runtime tests

- invalid memory access;
- stack overflow;
- execution beyond the code segment;
- division by zero;
- illegal or forbidden instructions;
- failure to return before the instruction budget expires.

---

## 8. Golden tests, semantic tests and differential tests

Three complementary styles should be used.

### Semantic tests

Compile a MIGA-80 Lua function, execute it, and compare its result with the language specification or a small host reference implementation. These tests should form the majority of the suite because they tolerate harmless changes in instruction selection.

### Golden-code tests

For a small number of important patterns, compare the emitted assembly or normalized disassembly with a reviewed reference. This is useful for:

- function prologues and epilogues;
- branch lowering;
- fixed-point multiply/divide sequences;
- array addressing;
- known optimization opportunities.

Golden-code tests should remain selective. Testing every function byte-for-byte would make normal optimizer work unnecessarily painful.

### Differential tests

Generate many bounded programs or inputs and compare:

```text
reference evaluator result == emulated 68EC020 result
```

Later, a smaller corpus can run under both Musashi and Moira:

```text
Musashi final state == Moira final state
```

This is particularly useful for condition codes, signed arithmetic, shifts, division and unusual addressing modes.

---

## 9. Diagnostics and trace format

A failed test should produce enough information to diagnose the compiler without opening a full debugger.

Recommended failure report:

```text
TEST: arithmetic/mul_add_negative
RESULT: expected D0 = -11, got 0x00000005
PC: 0x00102A
SR: 0x2000
INSTRUCTIONS: 17
ESTIMATED CORE CYCLES: 94

D0=00000005 D1=FFFFFFFC D2=00000003 D3=D3D3D3D3
A0=00000000 A1=00000000 A6=0008FFF0 A7=0008FFEC

001020  MOVE.L  D0,D2
001022  ADD.L   D0,D0
001024  ADD.L   D2,D0
001026  ADD.L   D1,D0
001028  RTS
```

To avoid enormous logs, keep a circular trace of the last 32 or 64 instructions and print it only when a test fails. Optionally add a command-line flag for a full trace.

Useful runner options:

```text
--cpu 68ec020
--trace
--trace-on-failure
--max-instructions N
--poison-memory BYTE
--seed N
--dump-registers
--dump-memory START:SIZE
```

---

## 10. Assembly, linking and file formats

Two workable approaches exist.

### Option A: generate assembly first

```text
MIGA-80 Lua -> textual 68k assembly -> assembler -> ELF -> flat binary
```

Advantages:

- readable compiler output;
- easy comparison with hand-written code;
- mature relocation and symbol handling;
- rapid early development.

Disadvantages:

- an external assembler is part of every test invocation;
- syntax becomes coupled to vasm or GNU `as`;
- source-location mapping requires extra metadata.

This is the recommended first implementation.

### Option B: emit machine code directly

```text
MIGA-80 Lua -> internal encoder -> raw code or ELF
```

Advantages:

- faster and self-contained compilation;
- exact control over instruction encoding;
- no assembler dependency in the final SDK.

Disadvantages:

- the project must implement and test encodings and relocations;
- failures are harder to inspect early in development;
- this creates substantial work unrelated to language semantics.

Direct encoding should be considered only after the backend and ABI are stable.

### Suggested first pipeline

```sh
miga80c test.lua -S -o test.s
m68k-elf-as -m68020 test.s -o test.o
m68k-elf-ld -T tests/runner.ld test.o runtime-stubs.o -o test.elf
m68k-elf-objcopy -O binary test.elf test.bin
miga68k-test --image test.bin --symbols test.sym --entry test_main
```

Exact command-line switches must be verified against the selected assembler. The important architectural choice is to retain ELF and symbols for inspection while giving the CPU runner a simple flat image.

GNU Binutils provides the assembler, linker, `objcopy`, `objdump`, `readelf`, `nm` and related binary tools. On macOS, Homebrew currently provides an `m68k-elf-binutils` formula, making this route particularly convenient.

vasm is also a strong choice when Motorola/Devpac-style assembly syntax is preferred. It supports M68k and can emit linkable objects or absolute code. Avoid making the compiler backend depend permanently on a proprietary or host-specific assembler dialect.

---

## 11. Performance regression testing

The local runner can collect:

- generated code size;
- executed instruction count;
- CPU-core cycle estimate;
- maximum stack depth;
- number of runtime calls;
- number and width of memory operations.

Store these as benchmark metadata rather than absolute pass/fail expectations for every test. Add explicit ceilings only to a small number of important kernels:

- fixed-point matrix/vector operations;
- triangle setup and inner loops;
- C2P preparation stages;
- sprite/object list traversal;
- tilemap update and clipping;
- Lua function-call overhead.

A useful regression record is:

```json
{
  "test": "math/mat4_mul_vec3",
  "code_bytes": 184,
  "instructions": 96,
  "core_cycles": 612,
  "max_stack_bytes": 32
}
```

The runner should flag meaningful relative regressions while recognizing that the final hardware benchmark remains authoritative.

---

## 12. Local and CI workflow

### Developer loop

```text
edit compiler
    |
    v
run one semantic test (< 1 s target)
    |
    v
run compiler suite
    |
    v
inspect trace only on failure
```

Recommended commands:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R fixed_point_mul --output-on-failure
```

### Continuous integration

Every commit should run:

1. native frontend/IR unit tests;
2. generated-code tests under Musashi;
3. malformed-code and timeout tests;
4. deterministic differential tests with recorded seeds;
5. selected code-size and cycle regression checks.

UAE integration tests may run less frequently because they require more setup and are slower. Real A1200 benchmarks should be treated as release or milestone validation.

---

## 13. Proposed repository layout

```text
compiler/
  frontend/
  ir/
  backend_m68k/

runtime/
  abi/
  stubs/
  amiga/

tools/
  miga68k-test/
    memory.c
    runner.c
    trace.c
    traps.c
  musashi/

tests/
  compile/
  diagnostics/
  execute/
  differential/
  performance/
  fixtures/
  runner.ld

integration/
  uae/
  a1200/
```

Third-party CPU sources can be included as a pinned Git submodule, a package fetched by the build, or a vendored dependency with its license. Reproducible version pinning is more important than the exact dependency mechanism.

---

## 14. Implementation roadmap

### Phase 0 — prove the runner

**Implemented:** the pinned Musashi revision selects 68EC020 mode, uses a
bounds-checked big-endian 24-bit memory map, executes the reviewed `mul_add`
source after GNU `as`/`objcopy` conversion to a flat image, stops at a return
sentinel, limits instructions, verifies ABI/stack guards, and retains a
circular disassembly trace for failures. Run it with `gmake miga68k-test`.

- integrate Musashi with a flat byte-array memory model;
- configure the 68EC020 CPU type;
- load a hand-written function;
- set registers and stack;
- stop cleanly after `RTS`;
- assert a return value;
- print a short disassembly trace on failure.

**Exit condition (met):** a native executable runs a hand-written `mul_add` 68k function and verifies its result without UAE.

### Phase 1 — connect the compiler

**Initial connection implemented:** `miga80c` parses one explicitly annotated
`i8`/`u8`/`i16`/`u16`/`i32`/`fix`/`bool`/`string`/`symbol` function with typed local declarations,
assignments, signed/unsigned comparisons, integer division, Q16.16
multiplication/division, explicit `fix(i32)`/`i32(fix)`, statement-only `/=`,
nested `if`/`else`, and nested `while`,
lowers it to typed stack IR and value IR, renders GNU
m68k assembly at `-O0` or `-O1`, and provides a host CFG evaluator. The IR has
up to 32 blocks with bounded successors. Each loop is normalized around one
preheader, header, dedicated latch, and dedicated exit. Multiple `continue`
and `break` sites are folded through binary merge funnels; O1 verifies that form
and inserts typed branch and loop join values. The optimizer solves bounded per-block liveness, treats
`phi` inputs as edge uses, reuses registers across exclusive branches,
coalesces compatible `phi` slots, and schedules parallel edge copies with a
bounded cycle-breaking temporary. The ordinary test path assembles both levels
for seven corpora and checks six inputs per corpus against Musashi. A
signed-division corpus adds twelve normal executions and four controlled
faults. An exact-width corpus adds twelve normal executions and two controlled
faults. Fixed-point multiplication, division, immutable-value, and explicit
conversion corpora cover their dedicated semantics and faults. A synthetic
value-IR fixture forces three reusable
spill slots in an `A6` frame and checks six more inputs, saved registers, stack
balance, and maximum stack use. Host and 68020 test programs must render
ordinary, local-heavy, conditional, loop, loop-control, division, exact-width,
immutable-value, fixed-point, fixed-division, conversion, and spilling
assembly byte-identically.
The current GNU toolchain
retains an Amiga relocatable object; ELF linking, symbol manifests, and broader
language semantics remain pending. See
[MIGA Lua Compiler Bootstrap](./MIGA-Lua-compiler-bootstrap.md).

- emit assembly for integer constants, arithmetic and return;
- assemble and link automatically;
- obtain entry-point symbols;
- execute generated functions from the test suite;
- preserve deterministic test artefacts on failure.

**Exit condition (met for the bootstrap subset):** `miga80c` compiles a Lua function and a local test invokes it with several inputs.

### Phase 2 — freeze the ABI

**Tranche 1 implemented and extended:** ABI 0.1 froze scalar register arguments and return,
caller/callee-saved sets, `A5` context ownership, `A6` frames, Boolean values,
and four-byte stack alignment. ABI 0.2 assigns runtime-context offset zero to a
non-returning controlled fault handler and passes fault code/source location in
`D0-D2`. ABI 0.3 defined canonical register/slot values and wrapping semantics
for `i8`, `u8`, `i16`, and `u16`. ABI 0.4 added canonical immutable-string
pointers in the address class and interned symbol IDs in the scalar class.
ABI 0.5 added signed Q16.16 values in the scalar class. ABI 0.6 defines
explicit `fix(i32)`/`i32(fix)` semantics and controlled range-fault code 2. The
backend consumes the machine-readable contract and the Musashi runner derives
its saved-register checks from it, including a deliberate clobber negative
control. See
[MIGA Lua Native ABI 0.6](./MIGA-Lua-native-ABI-v0.md).

- define register roles; **implemented for ABI 0.6**
- add calls and source locals; **typed source locals, assignments, and
  compiler-generated spill frames implemented; calls pending**
- implement saved-register and stack guard checks; **implemented for the current
  entry path; nested calls remain pending**
- define runtime traps; **division by zero is controlled fault 1 and numeric
  conversion out of range is controlled fault 2; other traps pending**
- publish the ABI as a versioned document; **implemented for ABI 0.6**

**Exit condition:** separately compiled generated functions and runtime stubs can call one another reliably.

### Phase 3 — expand semantics

- comparisons, `bool`, and nested `if`/`else`; **implemented**
- `while`, nested loops, loop-carried value joins, canonical single-latch /
  single-exit loop CFGs, `break`, and `continue`; **implemented without
  declarations or returns inside loop bodies**
- signed `i32` `/`, statement-only `/=`, and controlled zero-divisor faults;
  **implemented with constant rejection and source-located runtime faults**
- `i8`, `u8`, `i16`, and `u16` arithmetic, comparisons, division, constant-fit
  conversion, and canonical ABI values; **implemented at `-O0` and `-O1`**
- signed Q16.16 literals, addition/subtraction/negation, multiplication, and
  comparisons; **implemented with bit-exact host/68020 rounding**
- signed Q16.16 division and statement-only `/=`; **implemented with
  truncation toward zero, wrapping quotients, and controlled zero faults**
- explicit `fix(i32)` and `i32(fix)`; **implemented with constant folding,
  checked integer range, truncation toward zero, and controlled fault 2**
- globals and arrays;
- immutable string pool, `string` descriptors, and interned `symbol` IDs;
  **implemented for the bounded single-function compiler and ABI 0.6;
  cartridge-wide multi-function pool merging remains pending**
- bounds and error behaviour;
- randomized differential tests.

### Phase 4 — performance tracking

**Initial signals implemented:** the generic runner reports image bytes,
executed instructions, and maximum callee stack bytes. The differential suite
locks reviewed `-O0`/`-O1` figures for seven ordinary source corpora, including
nested conditional control flow, cyclic loop transfers, and multi-site loop-control
funnels, plus signed division, exact-width integers, immutable values,
fixed-point arithmetic, fixed division, explicit conversions, and a forced
spill fixture: 168
Musashi executions in
total. These are optimizer regressions only, not
cycle or wall-time claims. In the conditional corpus,
CFG-aware allocation reduces the `-O1` result to 356 code bytes, 60-62 executed
instructions, and 24 maximum callee stack bytes; six live `phi` values share
two slots. In the loop corpus, O1 reduces 308 code bytes, 313 instructions, and
48 stack bytes to 188, 164, and 28 respectively. Both renderers omit jumps to
the next emitted block, so the dedicated latch adds no redundant hot-path
branch. The loop-control corpus reduces 356 code bytes and 69-684 executed
instructions at O0 to 288 bytes and 39-352 instructions at O1; both levels use
32 maximum callee stack bytes.

The division image falls from 108 bytes, 28 normal-path instructions, and 28
stack bytes at O0 to 44 bytes, 7 instructions, and no generated stack use at
O1. Its four dynamic zero-divisor cases reach the ABI fault handler with the
expected source location.

The exact-width image falls from 472 bytes at O0 to 220 bytes at O1. Across its
normal paths O1 executes 32-34 instructions instead of 107-110 and uses 20
bytes of callee stack instead of 44; its unsigned zero-divisor path retains its
source location.

The fixed-point image falls from 196 bytes, 57-58 instructions, and 28 stack
bytes at O0 to 104 bytes, 25-26 instructions, and 16 stack bytes at O1. This
includes preserving `D7` around the native 64-bit product. The fixed-division
image falls from 300 bytes and 95-101 normal-path instructions to 164 bytes
and 51-55 instructions; O1 uses at most 16 stack bytes and preserves `D6-D7`.
The conversion image falls from 208 bytes and 40-50 normal-path instructions
to 112 bytes and 22-26 instructions; checked-fault paths fall from 15 to 11
instructions and retain their source location. The `-Os` 68020/libnix compiler
is 70,872 linked bytes (70,472 text, 280 data, 120 BSS), an increase of 3,612
text bytes over the fixed-division tranche.

- record code size, instruction counts, and stack use; **implemented for the bootstrap**
- add core-cycle estimates;
- build a benchmark corpus from representative cartridges;
- add selected regression thresholds;
- compare the same kernels on UAE and a real A1200.

### Phase 5 — independent validation

- add a Moira runner or adapter;
- execute a curated edge-case corpus under both engines;
- investigate divergent final states;
- retain the hardware test suite as the ultimate compatibility check.

---

## 15. What should remain outside the local CPU runner

Do not gradually turn this tool into another Amiga emulator. The following belong in the UAE/hardware integration layer:

- Copper list execution;
- bitplane fetch and display timing;
- Blitter operations and nasty mode;
- sprite DMA and multiplexing;
- audio DMA;
- vertical blank and raster interrupts;
- Chip RAM contention;
- C2P interaction with display DMA;
- exact 25/50 Hz frame-budget validation.

The local runner may mock the APIs that control these systems, but it should test only the values and command sequences emitted by generated code.

---

## 16. Recommended decision

Build a small **Musashi-based `miga68k-test` executable** before implementing a substantial part of the Lua compiler backend.

The first version should contain only:

- one flat, bounds-checked big-endian memory array;
- a 68EC020 Musashi configuration;
- a stable test entry ABI;
- a return sentinel and instruction limit;
- register and memory assertions;
- a last-instructions trace using the Musashi disassembler;
- integration with the ordinary native test command.

Use textual assembly plus GNU Binutils or vasm at first. Keep ELF for symbols and debugging, then extract a flat binary for execution. Add Moira only when differential validation or more precise CPU timing has concrete value.

This gives MIGA-80 two deliberately different feedback loops:

```text
FAST LOOP
MIGA-80 Lua -> compiler -> Musashi -> assertions
Purpose: correctness, diagnostics, regression tests

HARDWARE LOOP
MIGA-80 Lua -> compiler -> MIGA-80 runtime -> UAE/A1200
Purpose: chipset integration, compatibility and real performance
```

The result is a conventional compiler-development experience on a modern machine while preserving the A1200 as the authoritative target.

---

## References

- [Musashi — Motorola 680x0 emulator](https://github.com/kstenerud/Musashi)
- [Moira — Motorola 68000/68010/68EC020/68020 emulator](https://dirkwhoffmann.github.io/Moira/)
- [vasm — portable and retargetable assembler](https://sun.hasenbraten.de/vasm/)
- [GNU Binutils](https://sourceware.org/binutils/)
- [Homebrew `m68k-elf-binutils`](https://formulae.brew.sh/formula/m68k-elf-binutils)
