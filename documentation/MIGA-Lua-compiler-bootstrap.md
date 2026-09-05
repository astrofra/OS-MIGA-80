# MIGA Lua Compiler Bootstrap

**Status:** exact-width integers, signed Q16.16 fixed point including division
and explicit `i32` conversions,
immutable strings/symbols, typed locals, signed/unsigned integer division,
controlled faults, normalized loops,
`break`/`continue`, loop `phi`, `-O1`, and spills implemented

## Scope

The bootstrap compiler proves the complete host development path without
claiming the version 1.0 grammar is frozen. Its accepted source is exactly one
public typed function:

```ebnf
function   = "function", name, "(", [ parameters ], ")", ":", value-type,
             { statement }, return-statement, "end" ;
parameters = parameter, { ",", parameter } ;
parameter  = name, ":", value-type ;
value-type = integer-type | "fix" | "bool" | "string" | "symbol" ;
integer-type = "i8" | "u8" | "i16" | "u16" | "i32" ;
statement  = local-declaration | control-statement ;
control-statement = assignment | if-statement | while-statement
                    | loop-control-statement ;
local-declaration = "local", name, ":", value-type, "=", expression ;
assignment = local-name, ( "=" | "/=" ), expression ;
if-statement = "if", expression, "then", { control-statement },
               "else", { control-statement }, "end" ;
while-statement = "while", expression, "do", { control-statement }, "end" ;
loop-control-statement = "break" | "continue" ;
return-statement = "return", expression ;
expression = sum, { ( "==" | "~=" | "!=" | "<" | "<=" | ">" | ">=" ), sum } ;
sum        = product, { ( "+" | "-" ), product } ;
product    = unary, { ( "*" | "/" ), unary } ;
unary      = "-", unary | primary ;
primary    = integer | fix-literal | "true" | "false" | string-literal
             | "symbol", "(", string-literal, ")"
             | ( "fix" | "i32" ), "(", expression, ")"
             | parameter-name | local-name | "(", expression, ")" ;
string-literal = single-quoted-string | double-quoted-string ;
fix-literal = digits, ".", digit, { digit } ;
```

Whitespace and Lua line comments beginning with `--` are accepted. Decimal
integer literals are limited to `0` through `2147483647`. A decimal point
selects `fix` directly: `1` is `i32`, while `1.0` is Q16.16 `fix`. A fixed
literal requires one through nine fractional digits and is converted with
integer arithmetic to the nearest Q16.16 value, with exact half cases rounded
away from zero after unary negation. Source parsing never uses host floating
point. The positive literal magnitude is limited to the representable signed
maximum; `-32768.0` remains accepted by the typed `--eval` interface, while a
source expression can obtain that wrapping bit pattern through arithmetic.
Short string literals use
single or double quotes and accept `\\`, `\'`, `\"`, `\n`, `\r`, `\t`,
`\0`, and `\xNN`; raw newlines are rejected. A function has at most 16
function-scoped typed locals and 32 statements including nested branches and
the final return.
Declarations require an initializer; a local is visible only after that
initializer, cannot shadow a parameter or another local, and parameters are
immutable. Types are exact. There is no general implicit conversion. The one
implemented exception adapts an `i32` constant expression to `i8`, `u8`,
`i16`, or `u16` when its final value is representable in the destination type;
an out-of-range constant is rejected. Arithmetic, `/`, and ordered comparisons
require two operands of the same numeric type. There is no implicit conversion
between integers and `fix`; the two implemented explicit forms are `fix(i32)`
and `i32(fix)`.
`==`, `~=`, and its exact alias
`!=` require two operands of the same value type; `if` and `while` conditions
require `bool`. `if` currently requires an
explicit `else`. Declarations and returns inside `if` branches or loop bodies
remain rejected until lexical scopes and multiple exit blocks are specified.
`break` and `continue` are valid only within the nearest enclosing `while` and
must terminate their immediate statement list; a following statement remains
valid when it is reached through another branch of an enclosing `if`. The
initial ABI supports at most three scalar parameters in `D0` through `D2` and
two `string` parameters in `A0`/`A1`. A scalar result uses `D0`; a `string`
result uses `A0`.
Integer arithmetic wraps at the declared width. Signed integer `/` truncates toward zero and
minimum-value divided by `-1` wraps to that minimum value; unsigned `/` uses
ordinary unsigned division. A provably constant zero divisor is a compile-time
error. Every other divisor not proven nonzero is checked before `DIVS.L` or
`DIVU.L` and branches to a controlled, source-located runtime fault when zero.
Register, frame, and fault placement follows
[MIGA Lua Native ABI 0.6](./MIGA-Lua-native-ABI-v0.md).

`fix` is signed Q16.16 in one 32-bit scalar. Addition, subtraction, and
negation wrap modulo 2^32. Multiplication forms a signed 64-bit product and
extracts bits 16 through 47, which rounds toward negative infinity before the
32-bit wrapping result. Ordered comparisons are signed comparisons on the raw
Q16.16 value. Fixed-point division computes `(a * 65536) / b`, truncates toward
zero, and keeps the low 32 bits. A constant zero divisor is rejected; a
dynamic zero divisor reaches the same controlled, source-located fault as
integer division. `fix(value)` accepts only `i32`, is exact for values from
-32768 through 32767, and shifts the integer left by 16. An out-of-range
constant is a compile-time error; an out-of-range dynamic input reaches
controlled fault 2 with the conversion's source location. `i32(value)` accepts
only `fix`, truncates toward zero, and cannot overflow because every Q16.16
integral part fits `i32`. Both forms fold when their input is constant.

The version 1 language contract requires an explicit return annotation and
includes `void`, but `void` code generation is not in this bootstrap tranche.
`string` and `symbol` are implemented immutable value types. A string value is
the canonical address of a read-only `{ u32 byte_length; byte payload[]; }`
descriptor emitted in `.text`; it has no trailing-NUL requirement. Equivalent
decoded literals are deduplicated, making equality a pointer comparison.
`symbol("name")` interns a compile-time spelling into an opaque nonzero 32-bit
ID, also compared in constant time. Neither type permits arithmetic, ordering,
or implicit conversion to the other. The current per-function pool is bounded
to 32 deduplicated entries and 1,024 decoded payload bytes. Cartridge-wide
pool merging and ID rewriting remain part of the future multi-function
pack/link step. There are no `byte` or `word` aliases: the source spellings
remain `i8`, `u8`, `i16`, and `u16`.

Calls, conversions involving the narrow integer types, multiple functions,
multiple returns, hexadecimal source literals, and the minimum `i32` literal
spelling are likewise rejected rather than guessed.

Arrays are not part of the bootstrap grammar yet. Their frozen version 1
language contract is nevertheless zero-based: for `array<T, N>`, valid indices
are exactly `0` through `N - 1`, and index `0` denotes the first element.

`x /= value` is implemented as statement-only sugar for `x = x / value`; it
cannot appear in an expression or an `if`/`while` condition. Planned sibling
forms are `x++`, `x--`, `x += value`, `x -= value`, and `x *= value`, under the
same statement-only rule.

## Pipeline

The implementation has four bounded, host-buildable layers:

1. The frontend produces a typed AST with bounded node, statement, local, and
   immutable-pool tables and reports the first error with a one-based line and
   column.
2. Lowering produces a typed stack IR with explicit local loads/stores,
   comparisons, conditional/unconditional terminators, and up to 32 basic
   blocks with two successor slots each. A `while` has the canonical shape
   `preheader -> header -> body -> latch -> header`, plus one dedicated exit.
   Every normal/`continue` path is folded through a bounded binary funnel into
   the single latch; the false condition and every `break` path use another
   binary funnel into the single exit. Thus every block still has at most two
   predecessors and binary `phi` values remain sufficient. A `while` whose
   body has no syntactic path back to the header is represented as acyclic and
   needs no unreachable latch. The host interpreter follows this CFG as the
   semantic oracle and uses unsigned C operations to specify per-width
   wrapping, signed/unsigned comparisons, fixed-point multiplication, and division without C
   signed-overflow behavior.
3. `-O1` renames locals to values throughout the CFG and creates typed `phi`
   values at two-predecessor joins and loop headers. It identifies natural
   loops with bounded dominator analysis, and validates their single
   preheader, dedicated latch, declared loop region, and unique exit. It creates provisional loop
   `phi` operands before the latch has been lowered, then completes their backward
   inputs and removes trivial self-joins. Constant folding, simplification,
   dead-value removal, and `live-in`/`live-out` analysis all accept cyclic value
   flow. A dynamic division or checked `fix(i32)` remains live even when its
   result is overwritten, because its fault is observable; a proven-safe
   folded operation remains removable. `phi` operands are edge-specific uses. Non-overlapping `phi` live
   regions reuse stack slots; edge transfers are scheduled as parallel copies,
   with one bounded temporary slot reserved only when a genuine copy cycle must
   be broken. Calls will need an explicit side-effect rule before value
   renaming crosses them.
4. The development backend renders GNU m68k assembly. `-O0` retains fixed `A6`
   parameter/local slots and expression-stack temporaries as a baseline. The
   default `-O1` keeps current local and expression values in registers and
   preserves any allocated `D3-D7` registers with `MOVEM`. Spilling functions
   use ABI 0.6 `LINK`/`UNLK` frames, negative `A6` offsets, and `D7` as a saved
   scratch register. Both backends omit an unconditional jump when its target
   is the next emitted block, so the dedicated latch does not add a redundant
   branch to the hot loop path. Dynamic divisions add one `TST.L` and one
   normally untaken branch before the integer or fixed-point division
   sequence; cold per-site stubs pass the fault code, line, and column to the
   handler pointer in the `A5` context. Dynamic `fix(i32)` emits an unsigned
   biased range guard before a `SWAP`/`CLR.W` exact conversion unless O1 can
   prove the input came directly from `i32(fix)`. The latter uses a conditional
   `+0xffff`, then `SWAP`/`EXT.L`, to truncate toward zero without a helper call.
   String literals use PC-relative `LEA` and leave no relocation in the flat
   image. O1 currently copies live `A0`/`A1` string inputs into its uniform
   data-register value allocator; dedicated address-register allocation is a
   later measured optimization, not an ABI requirement.
   A live `fix` multiplication reserves `D7` as the high half of the 64-bit
   `MULS.L` result, extracts the Q16.16 result with `MOVE.W` plus `SWAP`, and
   preserves `D7` once in the function prologue/epilogue.
   A live `fix` division reserves `D6` for the divisor magnitude and `D7` for
   the high dividend/remainder, then uses bounded `DIVUL.L` plus 64/32-bit
   `DIVU.L` steps. O1 allocates ordinary values only in `D0-D4` for such a
   function and reserves `D5` only when a fixed quotient spills.

For the current local toolchain, GNU `m68k-amigaos-as` retains a relocatable
Amiga object and `m68k-amigaos-objcopy` extracts the flat image consumed by
Musashi. ELF linking, symbol-manifest loading, the shared low-level instruction
model, and the shipping direct encoder remain later steps. The `-O0`
stack-heavy renderer remains a correctness oracle; see the [MIGA Lua Optimization
Strategy](./MIGA-Lua-optimization-strategy.md).

## Commands

Build the compiler and render assembly:

```sh
gmake miga80c
build/host/miga80c/miga80c tests/compile/arithmetic.lua -S -o /tmp/arithmetic.s
build/host/miga80c/miga80c tests/compile/arithmetic.lua -O0 -S -o /tmp/arithmetic-o0.s
```

Evaluate the same typed IR on the host:

```sh
build/host/miga80c/miga80c tests/compile/arithmetic.lua --eval 7 5 2
```

`--eval` also accepts `true`/`false` for Boolean inputs and raw string or symbol
spellings that already exist in the function's immutable pool. This is an
oracle convenience: embedded NUL bytes and external runtime descriptors are
not representable as command-line arguments.

Run native frontend/IR tests and the complete differential path:

```sh
gmake compiler-abi-test compiler-test compiler-execute-test compiler-spill-test
```

Cross-build this same C99 compiler bootstrap for 68020/libnix and execute its
typed-IR evaluator under `vamos`:

```sh
gmake compiler-amiga-test
```

This proves that the bounded frontend, value optimizer, and assembly renderer
already run as an Amiga program: the target and host builds must render
byte-identical `-O1` assembly. It does not yet prove on-Amiga direct
machine-code emission; the shipping encoder and instruction-cache
synchronization remain later work.

The differential tests preserve `-O0`/`-O1` assembly, relocatable objects, and
flat binaries under the compiler pipeline build directories. Seven ordinary
source corpora—including typed locals, a nested 19-block comparison/branch
fixture, the loop-carried cyclic-copy fixture, and a multi-site `break`/`continue`
fixture—use six edge inputs each at both levels. Those 84 executions must
produce the same `D0` value as the typed-IR interpreter. An eighth signed-
division corpus adds 12 successful results and four controlled zero faults. A
ninth exact-width corpus adds 12 successful `i8`/`u8`/`i16`/`u16` results and
two controlled zero faults, including signed/unsigned comparisons, divisions,
and wrapping normalization. A tenth immutable-value corpus adds four
executions covering both CFG paths at both optimization levels,
decoded-literal deduplication, string/symbol `phi` values, pointer/ID equality,
and PC-relative descriptors. A synthetic value-IR schedule then forces three
spills and adds six more oracle comparisons. An eleventh fixed-point corpus
adds eight executions covering exact multiplication, negative-infinity
rounding, wrapping, both CFG paths, and `D7` preservation, bringing the total
to 132. A twelfth fixed-division corpus adds 16 successful results and four
controlled zero faults, including signed truncation, wrapping quotients,
`/=`, and `D6-D7` preservation, bringing the total to 152. A thirteenth
conversion corpus adds 12 results and four source-located range faults,
covering both inclusive integer bounds and signed truncation, for 168 total
Musashi executions. This is
necessary because the current bounded source subset cannot naturally exceed
all eight data registers. The reports retain image size, executed instruction
count, and maximum callee stack use, while the runner
verifies return or the expected controlled fault, precise fault location, stack
balance on return, callee-saved registers including `A6`, memory guards, and
the instruction budget.
