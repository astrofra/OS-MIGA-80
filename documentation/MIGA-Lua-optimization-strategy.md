# MIGA Lua Optimization Strategy

**Status:** exact-width integers, signed Q16.16 fixed point including division
and explicit `i32` conversions,
immutable strings/symbols, signed/unsigned integer division with controlled faults, normalized `break`/`continue` loops, cyclic
CFG liveness, branch/loop `phi`, parallel edge copies, `-O1`, spilling, and
frame layout implemented

MIGA Lua is a statically typed, ahead-of-time compiled dialect designed for
native 68EC020/68020 code. Familiar Lua syntax is a usability goal. Dynamic Lua
values, universal tables, metatables, garbage collection, runtime compilation,
and full Lua compatibility are not goals. Unsupported constructs are rejected
instead of silently selecting a dynamic fallback.

## Two performance budgets

The project must optimize both the generated program and the compiler running
on a stock A1200. The typed stack IR and `-O0` stack-heavy assembly renderer are
correctness oracles. They deliberately make evaluation order obvious and are
not a production-quality code-generation strategy.

The shipping compiler remains portable C99 built for the 68020. It processes
one function at a time with bounded reusable arenas, compact integer indexes,
and deterministic passes. It must not require an assembler, linker, garbage
collector, unbounded recursion, or host-sized optimization data structures.
The frontend, value-IR optimizer, and assembly renderer are cross-built with
libnix and executed under `vamos` by `gmake compiler-amiga-test`. That test
requires byte-identical textual assembly from the host and 68020 compiler
builds. Direct native emission on the Amiga remains a separate later gate. The
same target retains the bootstrap executable size in
`build/reports/compiler-amiga-size.txt` so compiler growth remains visible.

## IR memory budget

The lexer already streams over the source and retains only its current token;
it does not materialize a token list. The bootstrap AST and IR arenas instead
favor explicit, easily checked C99 records while the semantics are still
moving. With the current 32-bit enum/index ABI, an AST node occupies 32 bytes,
a typed stack-IR instruction 20 bytes, a stack-IR block 20 bytes, a value-IR
instruction 48 bytes, and a value-IR block 40 bytes. A separate 128-byte
membership bitset table identifies structured loop regions without enlarging
each block. The immutable pool is a 1,284-byte fixed record: 32 eight-byte
entries plus 1,024 decoded payload bytes and two 16-bit counters. It is copied
through the AST, stack IR, and value IR so each layer is independently valid.
At all configured maxima, an `-O1` compilation with a 64 KiB source uses
approximately 104 KiB for the source buffer, main arenas, CFG bitsets, and
optimizer workspace, excluding libc buffers and the compiler's own stack.

This is a measured design budget, not the final representation. Once the
remaining control-flow semantics are stable, the production layout SHOULD use
16-bit value, instruction, block, and symbol indexes with `0xffff` as the invalid value; a
16-bit opcode/type/flag header; aligned fixed records of roughly 8 or 12 bytes;
and a separate compressed source-location table. Liveness and allocation
scratch tables SHOULD share reusable per-function arenas. Blindly packed C
structures and an opaque variable-length token stream are not preferred: the
former risks inefficient unaligned 68020 accesses, while the latter makes
random optimizer access and CFG rewriting more expensive and complex.

## Planned bounded pipeline

```text
typed semantic IR
    -> basic blocks and value/three-address IR
    -> constant folding and propagation
    -> algebraic simplification and copy propagation
    -> dead and unreachable code removal
    -> 68020 instruction selection and strength reduction
    -> CFG liveness and bounded register allocation
    -> stack/frame layout
    -> small final peephole pass
    -> shared low-level m68k instruction model
```

The typed stack IR now has up to 32 explicit basic blocks, bounded successor
slots, typed local load/store operations, comparisons, branches, and backward
edges. `if`/`else` and `while` CFGs rename each local to its current value and
insert typed `phi` values at two-predecessor joins. Lowering normalizes every
cyclic loop around one preheader, header, dedicated latch, and dedicated exit.
Normal and `continue` edges enter a binary merge funnel ending at the latch;
the false header edge and `break` edges enter another funnel ending at the
exit. Funnel construction consumes at most two predecessors per block, so it
does not require a large N-ary `phi` representation. A bounded dominator pass
recognizes the natural loop and verifies its preheader, latch, declared
structured region, and unique exit. Loop-header `phi` values are deliberately created
before their backward operands exist, then completed after the latch is
lowered; trivial self-joins are removed. The dead-value walk is a bounded
worklist rather than a reverse linear scan, so cyclic dependencies and forward
value indexes are valid.

The optimizer folds constants with defined 32-bit wrapping, applies algebraic
identities, and marks dead values and overwritten assignments. A bounded
fixed-point data-flow pass computes `live-in` and `live-out` bitsets for every
block, including cyclic CFGs; `phi` operands are uses on their incoming CFG
edges rather than ordinary uses in the join block. The allocator reconstructs
live register and spill ownership at each block entry, so mutually exclusive
branches may reuse locations. Used `D3-D7` registers are preserved once with
`MOVEM`; expression values are not pushed to `A7`. Constant multiplication by
two or three is strength-reduced when the available registers make that
profitable.

Integer arithmetic preserves its declared `i8`, `u8`, `i16`, `u16`, or `i32`
type. Narrow results are sign- or zero-extended after each wrapping operation,
so later 32-bit register operations see a canonical value. Ordered comparisons
and division select signed or unsigned 68020 operations from that static type.
Signed division truncates toward zero and defines minimum / `-1` as a wrapping
minimum. The optimizer removes `/ 1`, turns signed `/ -1` into wrapping negation,
and folds constant divisions. A division with a dynamic divisor remains a
liveness root even if its result is overwritten because its zero-divisor fault
is observable. A proven nonzero constant divisor needs no guard and a dead such
division may be removed. Replacing other constant divisors, including powers of
two, remains future measured strength-reduction work.

`fix` uses one signed Q16.16 data value. Constant folding multiplies unsigned
magnitudes in C99 and explicitly applies the specified negative-infinity
rounding, avoiding host signed-overflow and signed-shift dependencies. Native
selection uses 68020 `MULS.L` for the exact 64-bit product, followed by
`MOVE.W` and `SWAP` to extract bits 16 through 47. Addition, subtraction,
negation, and signed comparisons reuse the ordinary 32-bit instructions.
Fixed-point division folds the exact magnitude expression
`(|a| * 65536) / |b|`, reapplies the sign for truncation toward zero, and
keeps the low 32 bits. Native selection uses a 32-bit `DIVUL.L` remainder step
followed by one non-overflowing 64/32-bit `DIVU.L`; this defines wrapping even
when the mathematical quotient exceeds 32 bits. `/ 1.0` is removed and
`/ -1.0` becomes wrapping negation.

Explicit `fix(i32)` first biases the input by `0x8000` and performs one
unsigned comparison against `0x10000`, accepting exactly -32768 through 32767.
The successful native path restores the value and uses `SWAP` plus `CLR.W` to
shift it by 16 without a helper. A dynamic checked conversion is a liveness
root even if its result is overwritten; its cold failure path enters ABI fault
2 with the source location. O1 omits the guard when the input is produced
directly by `i32(fix)`, whose range is already proven. `i32(fix)` cannot
overflow: it adds `0xffff` only for a negative raw value, then uses `SWAP` and
`EXT.L`, so negative values truncate toward zero. Both forms fold at compile
time.

The allocator first tries all eight data registers, so ordinary leaf functions
do not lose `D7` merely to reserve a scratch register. If that plan needs a
spill, a second bounded pass allocates values in `D0-D6`, reserves `D7` as the
spill scratch, and reuses four-byte spill slots after their last use. The
backend emits an ABI 0.6 `A6` frame with `LINK`/`UNLK`, addresses slots at
negative `A6` offsets, consumes spilled operands directly as 68020 memory
operands where possible, and preserves `D7` with the other used saved
registers. Frame size is checked before any assembly is emitted.
Only a function containing a live fixed-point multiplication takes a different
bounded route: values are allocated in `D0-D5`, `D7` is the preserved high
product register, and `D6` is retained as a low-product scratch only if that
particular result spills. This keeps the fixed-point requirement out of every
integer-only function. A live fixed-point division instead allocates values in
`D0-D4`, preserves `D6` for the divisor magnitude and `D7` for the high
dividend/remainder, and uses preserved `D5` only when its result spills.

`-O0` gives every source local a physical negative `A6` slot after the
parameter slots. `-O1` needs no local slot when value renaming and liveness keep
the value in registers. Across branches and loops, it colors `phi` storage from
bounded per-value block masks: values with disjoint live regions reuse a
four-byte slot. Incoming `phi` assignments have parallel-copy semantics. The
edge scheduler first emits copies whose destinations are no longer needed as
sources; if the remaining graph is a cycle, it preserves one old destination
in a single reserved four-byte temporary and completes the cycle with `D7` as
the saved scratch register. The temporary is omitted when no edge needs it.
The nested conditional corpus contains six live `phi` values but needs only two
slots; the loop corpus additionally verifies a real exchange cycle.

Nested `break` and `continue` now target the nearest loop. A loop whose body
always transfers to `break` has no actual backedge and is emitted as an acyclic
region rather than retaining an unreachable latch. Declarations or returns
inside control-flow bodies, copy propagation across joins, address-register
allocation, and the shared low-level m68k model also remain pending. Global
value numbering, expensive
interprocedural optimization, speculative dynamic typing, and exhaustive
instruction scheduling remain excluded until measurements justify their
compiler cost.

Statement-only `/=` is implemented as a typed read/divide/write and can never
participate directly in a CFG condition. Statement-only `++`, `--`, `+=`,
`-=`, and `*=` remain recorded language work and will follow the same rule.

Immutable string literals are canonical pool pointers and symbols are interned
IDs, so equality uses one 32-bit comparison and needs neither a helper call nor
a byte loop. Pool addresses are materialized with PC-relative `LEA`. Live
string parameters arrive in `A0/A1` under ABI 0.6 and are copied into the
current uniform data-register allocator; using `A2-A4` for long-lived address
values remains a future measured optimization.

The first 68020-specific choices include keeping scalars in data registers,
eventually keeping long-lived addresses in address registers, folding constant displacements
into effective addresses, preferring compact immediate forms, strength-reducing
constant multiplication when profitable, arranging fall-through branches, and
removing redundant extensions, moves, loads, and stores. Speed and code size
are separate costs because the A1200 instruction cache and memory domain can
make a shorter sequence preferable to one with a nominally lower instruction
count.

## Optimization levels

- `-O0` retains the implemented direct, debuggable lowering and is the semantic
  baseline.
- `-O1` is the implemented default: bounded simplification, dead-value
  removal, instruction selection, CFG-aware liveness/allocation, `phi`-slot
  coloring, and immediate-form selection.
- A later `-O2` may spend more compile time on the host and capable profiles,
  but it must use the same semantics and ABI. It must never be required to run
  a cartridge on a stock A1200.

Every optimization must preserve defined per-width wrapping, canonical
sign/zero extension, evaluation order,
guards, stop checks, and observable runtime calls. `-O0` and `-O1` are compared
against the same typed-IR oracle and executed under Musashi with deterministic
inputs. The same rule will apply to `-O2` when it exists.

## Measurement and acceptance

Musashi validates instruction semantics and ABI behavior; it is not a
cycle-accurate A1200 performance oracle. Compiler regressions will record code
bytes, executed instruction count, maximum generated stack use, calls, and
memory-operation widths. Stable curated kernels may receive reviewed limits.
FS-UAE remains an integration check, while timing decisions require repeated
measurements on a stock physical A1200 under a declared DMA and memory profile.

The first optimization milestone is met. Across seven ordinary source corpora
with six edge inputs each, a signed-division corpus with twelve successful
executions and four controlled faults, an exact-width corpus with twelve
successful executions and two controlled faults, an immutable-value corpus
with four executions, a fixed-point corpus with eight executions, a
fixed-division corpus with 16 successful executions and four controlled
faults, an explicit-conversion corpus with twelve successful executions and
four controlled faults, and a six-input spill fixture, both
optimization levels agree with the typed IR under Musashi. The current
regression measurements are shown as code bytes / executed instructions /
maximum callee stack bytes:

| Corpus | `-O0` | `-O1` |
| --- | ---: | ---: |
| arithmetic and parameter reuse | 116 / 41 / 28 | 28 / 12 / 4 |
| folding, negation, and identities | 132 / 46 / 28 | 28 / 10 / 4 |
| register pressure and repeated products | 120 / 43 / 32 | 40 / 13 / 8 |
| typed locals, reassignment, and dead store | 160 / 53 / 32 | 28 / 12 / 4 |
| six comparisons and nested `if`/`else` | 548 / 120-122 / 32 | 356 / 60-62 / 24 |
| `while`, loop-carried values, nested `if`, copy cycle | 308 / 313 / 48 | 188 / 164 / 28 |
| multiple `break`/`continue` sites and binary funnels | 356 / 69-684 / 32 | 288 / 39-352 / 32 |
| signed `/` and `/=`, including controlled faults | 108 / 16-28 / 28 | 44 / 7-11 / 0 |
| `i8`/`u8`/`i16`/`u16`, signed/unsigned division and wrapping | 472 / 43-110 / 44 | 220 / 15-34 / 20 |
| immutable string/symbol pool, equality, and CFG joins | 196 / 46-47 / 28 | 136 / 23-24 / 16 |
| signed Q16.16 multiplication, comparison, wrapping, and CFG join | 196 / 57-58 / 28 | 104 / 25-26 / 16 |
| signed Q16.16 `/` and `/=`, wrapping quotient, controlled faults | 300 / 16-101 / 28 | 164 / 10-55 / 8-16 |
| explicit `fix(i32)` / `i32(fix)`, including range faults | 208 / 15-50 / 28-32 | 112 / 11-26 / 16 |

The register-pressure corpus forces simultaneous `D3/D4` allocation and
verifies their ABI preservation. A separate deliberately pressure-heavy value
IR fixture forces three reusable spill slots; its 96-byte image executes 33
instructions with 36 callee stack bytes and agrees with the source-level oracle
for six edge inputs. The conversion corpus adds 12 returned values and four
controlled faults after the fixed-division tranche, bringing the current
Musashi compiler total to 168 executions.

The `-Os` 68020/libnix compiler currently has a 70,872-byte linked
text/data/BSS footprint (70,472 text, 280 data, 120 BSS). Its host and Amiga
builds emit byte-identical ordinary, local-heavy, conditional, loop,
loop-control, division, exact-width, immutable-value, fixed-point,
fixed-division, conversion, and synthetic spilling `-O1` assembly. The
conversion tranche adds 3,612 text bytes over the fixed-division compiler and
no linked data/BSS bytes. This growth is recorded explicitly; the generated
fixed-point paths remain inline and call no runtime helper.
Unconditional jumps to the physically next block are elided at both
optimization levels. This
lets the dedicated latch normalize the IR without adding an extra runtime
branch per iteration, and also removes other redundant fall-through jumps. The
conditional corpus reduces code by
35% and executed instructions by about 49-50% at O1. CFG-aware reuse and
`phi` coloring reduce its maximum callee stack from the previous 44 bytes to 24
bytes, below O0's 32 bytes. On the loop corpus, O1 reduces code by 38%, executed
instructions by 48%, and maximum callee stack use from 48 to 28 bytes. These
counts are regression signals, not A1200 cycle claims. In the new loop-control
corpus, O1 saves 19% of code and 44-49% of executed instructions; its 32-byte
stack high-water currently matches O0 because the additional control joins
need edge-transfer slots.

On the division corpus, O1 reduces code from 108 to 44 bytes, the successful
path from 28 to 7 instructions, and generated stack use from 28 bytes to zero.
The fault paths execute 8 or 11 instructions at O1 and preserve the exact
source line and column. These figures measure this corpus, not general division
latency or physical A1200 cycles.

On the immutable-value corpus, whose image sizes include both string
descriptors and payload bytes, O1 reduces 196 bytes to 136, 46-47 executed
instructions to 23-24, and stack high-water from 28 bytes to 16. Runtime
equality remains a register-sized pointer or ID comparison; literal decoding,
deduplication, and symbol interning occur only during compilation.

On the fixed-point corpus, O1 reduces the image from 196 to 104 bytes, the four
paths from 57-58 to 25-26 instructions, and stack high-water from 28 to 16
bytes. These counts include saving/restoring `D7`; they establish a compact
native arithmetic path, not a cycle-accurate claim for physical A1200 memory.

On the fixed-division corpus, O1 reduces the image from 300 to 164 bytes. Its
normal paths fall from 95-101 to 51-55 instructions, while its dynamic-zero
fault paths retain exact source locations. O1 uses at most 16 callee stack
bytes, including preservation of `D6-D7` and two temporary long words during
each division; O0 reaches 28 bytes.

On the explicit-conversion corpus, O1 reduces the image from 208 to 112 bytes.
Its normal paths fall from 40-50 to 22-26 instructions and its range-fault
paths from 15 to 11; both preserve the exact source location. O1 uses 16
callee stack bytes, versus 28-32 at O0.
