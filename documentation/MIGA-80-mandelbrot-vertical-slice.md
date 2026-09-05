# MIGA-80 Mandelbrot Vertical Slice

**Status:** First complete F5 path implemented and passing under Musashi and
FS-UAE; O1 direct emission and full runtime guards remain in progress

**Date:** 2026-09-05

**Target:** Stock PAL Amiga 1200, 68EC020, AGA, 2 MiB Chip RAM, no Fast
RAM or FPU

## 1. Purpose

This vertical slice must prove the shortest honest path through the MIGA-80
product:

```text
bootable ADF
    -> hosted MIGA-80 shell
    -> read-only source view
    -> on-Amiga MIGA Lua compilation
    -> native 68020 code in RAM
    -> guarded execution
    -> PIXEL playfield output
```

The default source program draws a Mandelbrot set. This is deliberately more
than a static graphics demo: it exercises typed locals, nested loops,
conditionals, fixed-point arithmetic, a runtime graphics call, native code
emission, generated-code entry and return, and the existing AGA display path.

The result is one self-starting `.adf` image. A user boots it, sees the complete
default source, presses one key, and sees the image produced by the code that
was compiled on that Amiga run.

## 2. Demonstration Contract

The production demo follows these visible states:

| State | Visible behavior | Transition |
| --- | --- | --- |
| Boot | The ADF boots through Kickstart and AmigaDOS and starts `MIGA80` automatically. | Successful preflight opens the workspace. |
| Source | A 256 x 256 MIGA-80 screen shows the complete default Mandelbrot source. | `F5` starts compilation. |
| Compile | The status row reports lexing, lowering, optimization, encoding, and readiness without opening a CLI. | Success enters execution; failure enters the error state. |
| Run | Generated 68020 code fills the configured `PIXEL` viewport and the result is published to the AGA screen. | Normal return leaves the result visible. |
| Result | The Mandelbrot image and a compact completion status remain visible. | `Esc` returns to the source view. |
| Error | A bounded diagnostic shows the first source line, column, and message. | `Esc` returns to the source view. |

In the source state, `Esc` exits MIGA-80 and restores the previous AmigaOS
display. The initial source view has no editing, cursor, selection, save,
horizontal scrolling, or vertical scrolling.

The first ADF is an AmigaOS-hosted MIGA-80 environment. It is not a bare-metal
operating system and does not claim to prove the final exclusive Copper,
interrupt, input, audio, or restoration path. Compilation and generated-code
execution are nevertheless real and occur on the target.

## 3. Non-Negotiable Proof Rules

The demo is valid only if all of the following are true:

1. The displayed source is the source passed to the compiler.
2. The native function executed after `F5` is emitted during that run.
3. The ADF contains no precompiled Mandelbrot machine-code substitute.
4. The target does not invoke or carry a GNU assembler, linker, or object-file
   converter.
5. The direct encoder and the development assembly route agree under Musashi
   for the default source.
6. The typed-IR evaluator, Musashi execution, and Amiga execution produce the
   same canonical pixel-buffer checksum.
7. A compiler diagnostic or generated-code fault returns to a controlled
   MIGA-80 state rather than crashing or silently returning to AmigaDOS.

A precompiled reference image or pixel checksum may exist only as a test
oracle. It may never be used as the displayed runtime result.

## 4. Scope and Deliberate Limits

### 4.1 Included

- a bootable OFS ADF with an automatic startup sequence;
- the hosted 256 x 256 PAL AGA screen;
- a compact read-only source viewer using a 4 x 8 bitmap font;
- the default Mandelbrot source as a real file on the ADF;
- explicit `void` function return support;
- statement calls to a statically known runtime intrinsic;
- the initial `pset` graphics intrinsic;
- the minimum direct 68020 encoder required by the program;
- branch fixups and PC-relative immutable-data fixups used by emitted code;
- executable-memory allocation, cache synchronization, and a guarded entry
  trampoline;
- controlled compile, encoding, budget, and runtime error paths;
- a 160 x 128 byte-per-pixel chunky source converted by the existing C2P4
  path;
- host, Musashi, `vamos` where applicable, and FS-UAE regression coverage;
- one distributable `.adf` plus size, content, and checksum manifests.

### 4.2 Excluded

- source editing, scrolling, project loading, or saving;
- syntax highlighting beyond a possible later color-only presentation pass;
- user-selectable files, viewport sizes, palettes, or compiler options;
- multiple source functions and ordinary user-function calls;
- arrays, records, dictionaries, dynamic allocation, or general libraries;
- progressive display while the Mandelbrot calculation is running;
- final exclusive-hardware takeover, private Copper lists, audio, sprites, and
  joystick support;
- a claim about stock-A1200 frame rate derived from Musashi or FS-UAE timing;
- freezing the complete MIGA-80 version 1 default palette or font repertoire.

The first executable checkpoint uses the bounded stack-oriented `-O0` IR as
its direct-encoder source. This keeps the first native proof small and makes it
directly comparable with the existing assembly oracle. It is not the final
performance configuration: moving direct emission to the call-aware `-O1`
value-IR register plan is the next compiler tranche.

## 5. Existing Foundations and Missing Links

| Area | Existing foundation | Work required by this slice |
| --- | --- | --- |
| ADF | A reproducible OFS image builder and FS-UAE boot test exist for the physical graphics benchmark. | Generalize the packaging inputs and create a MIGA-80 startup image. |
| Display | A hosted 256 x 256 x 8 AGA dual-playfield screen passes under FS-UAE. | Turn the smoke-test path into reusable shell/display code with event handling. |
| Pixel conversion | Correct C and 68020 C2P4 paths exist for 160 x 128, 192 x 160, and 256 x 256. | Select the 160 x 128 byte-per-pixel path and publish the generated viewport safely in hosted mode. |
| Compiler | The portable compiler implements typed locals, CFG, `if`, normalized `while`, `break`, fixed Q16.16 arithmetic, `-O1`, and spills. | Add `void`, intrinsic calls, call-aware liveness, and the syntax needed by the default source. |
| Assembly backend | GNU m68k assembly rendering is validated by the typed-IR oracle and Musashi. | Introduce a shared low-level instruction form and direct binary encoding. |
| Native execution | Musashi checks generated functions, ABI preservation, stack bounds, and controlled numeric faults. | Add runtime-service mocks, executable target allocation, cache synchronization, and the host/target trampoline. |
| UI | The specification defines a hosted editor shell. | Add the first screen states, 4 x 8 text rasterizer, status row, and `F5`/`Esc` routing. |

## 6. Read-Only Source View

A 4 x 8 cell gives exactly 64 columns by 32 rows on the 256 x 256 logical
display. The first layout budget is:

| Rows | Use |
| ---: | --- |
| 1 | title and current source name |
| 30 | complete default source |
| 1 | command, compilation, result, or compact error status |

The source must therefore remain at most 30 physical lines and at most 64
characters per line. If line numbers are shown, their width is part of the
64-column limit. Tabs are rejected or expanded deterministically before
display; the shipped source uses spaces only.

The viewer and compiler consume the same bounded byte buffer loaded from the
ADF. The UI does not maintain a separately reformatted source copy. This avoids
the possibility of compiling text different from the text presented to the
user.

The source UI is composited in playfield 1 (the odd AGA bitplanes). Its
four-bit colour number therefore addresses palette entries 0 through 15
directly; colour zero reveals the playfield-2/backdrop colour. This explicit
placement avoids depending on the configurable playfield-2 colour-bank offset
for the editor and leaves the same foreground playfield ready for generated
`pset` output.

An optional semicolon is accepted as a Lua-compatible statement separator.
This small grammar addition allows two short declarations or assignments on
one physical line without weakening the explicit-type rules.

## 7. Bitmap Font Asset Contract

The initial font is the original freely licensed project asset supplied as
`works/ID/font-4x8.png`, with cell order in `works/ID/font-4x8.txt`. Miniwi was
used only as visual inspiration and no Miniwi glyph data was imported or
converted.

The source sheet is exactly 104 x 32 pixels: 26 columns by four rows of fixed
4 x 8 cells. The first cell is the editor cursor rather than ASCII space.
Later space entries are empty separator cells; the converter selects one as
the canonical ASCII space. The sheet defines the punctuation, digits, Latin
uppercase, and Latin lowercase glyphs needed by the bundled source. Double
quote, apostrophe, and backtick are not yet drawn and deterministically use
the question-mark fallback.

The generated runtime representation is intentionally simple:

```c
uint8_t glyph_rows[95][8];
```

`glyph_rows[codepoint - 0x20][0]` is the top row. Bits 3 through 0 represent
pixels from left to right; bits 7 through 4 must be zero. Unsupported input is
rendered as `?`. The table occupies 760 bytes before any alignment. The
standard-library-only `scripts/generate-font-4x8.py` converter validates PNG
CRC and layout, reverses PNG scanline filters, checks glyph cells, and emits
both the C header and a 780-byte `M8F4` binary asset. The cursor occupies its
own eight-byte entry and is not mapped to a character.

The first UI and bundled source are ASCII-only. Latin-1 coverage can be added
later without changing the renderer.

## 8. Default Mandelbrot Source

The intended source is compact enough to fit the viewer and avoids fixed-point
division in the hot loops. The exact spelling remains a compiler regression
fixture:

```lua
function main(): void
  local x: i32 = 48; local y: i32 = 64
  local i: u8 = 0
  local cr: fix = -2.0; local ci: fix = -1.2
  local zr: fix = 0.0; local zi: fix = 0.0
  local next_zr: fix = 0.0
  while y < 192 do
    x = 48
    cr = -2.0
    while x < 208 do
      zr = 0.0; zi = 0.0
      i = 0
      while i < 15 do
        if zr * zr + zi * zi > 4.0 then
          break
        else
          next_zr = zr * zr - zi * zi + cr
          zi = 2.0 * zr * zi + ci
          zr = next_zr
          i = i + 1
        end
      end
      pset(x, y, i)
      cr = cr + 0.01875
      x = x + 1
    end
    ci = ci + 0.01875
    y = y + 1
  end
end
```

The active viewport covers logical screen coordinates `(48, 64)` through
`(207, 191)`, inclusive. Its 160 x 128 samples cover approximately
`[-2.0, 1.0)` on the real axis and `[-1.2, 1.2)` on the imaginary axis.
`0.01875` is rounded once to the language's defined signed Q16.16 value.

The fixed iteration ceiling is part of this demonstration profile, not a
language or graphics limit. The script uses repeated multiplication instead
of keeping separate square values because the smaller source fits the
no-scroll UI; generated-code measurements will determine whether a slightly
longer, lower-multiplication version is preferable.

## 9. Initial Graphics Intrinsic

The first source-visible runtime service is:

```lua
pset(x: i32, y: i32, color: u8): void
```

Semantics:

- `x` and `y` are coordinates in the full 256 x 256 logical display, with the
  origin at the top left;
- drawing outside the 256 x 256 logical `PIXEL` playfield is clipped and has
  no effect; the bundled script deliberately confines itself to the central
  160 x 128 demonstration viewport;
- color `0` is transparent and colors `1` through `15` select the opaque
  `PIXEL` palette;
- a color above `15` is rejected by the bootstrap service and has no effect;
  routing this case through the controlled fault mechanism remains follow-up
  runtime work;
- a successful call updates the byte-per-pixel chunky source and marks the
  viewport dirty;
- `pset` does not run C2P and does not publish planes on every call.

After the generated `main` function returns, the trusted runtime converts the
dirty viewport once and publishes the result. Thus the default run performs
20,480 cheap chunky writes and one viewport conversion, rather than 20,480
planar conversions.

The signature uses exactly the three scalar argument registers already
reserved by the native ABI: `x` in `D0`, `y` in `D1`, and canonical `u8`
`color` in `D2`. The service may clobber `D0-D2`, `A0-A1`, and condition codes.
The compiler must treat the call as observable and preserve every live value
across it according to the ABI.

## 10. Palette Candidate

The demo introduces a candidate default `PIXEL` palette named **Workbench
Sunset**. It is an artistic reference to the blue, orange, white, and black
identity associated with early Amiga Workbench; it is not a claim of exact
historical Workbench color values.

The logical colors remain 12-bit `0xRGB`. The neutral preview expands each
nibble by replication to `0xRRGGBB`:

| PIXEL index | Role | RGB12 | Neutral RGB24 |
| ---: | --- | ---: | ---: |
| 0 | Transparent; reveals the deep-blue PLANAR base | -- | -- |
| 1 | Midnight blue | `0x013` | `#001133` |
| 2 | Deep blue | `0x025` | `#002255` |
| 3 | Cobalt | `0x047` | `#004477` |
| 4 | Ocean blue | `0x069` | `#006699` |
| 5 | Bright blue | `0x08b` | `#0088bb` |
| 6 | Cyan blue | `0x0ad` | `#00aadd` |
| 7 | Pale cyan | `0x4ce` | `#44ccee` |
| 8 | Ice blue | `0x9ef` | `#99eeff` |
| 9 | White | `0xfff` | `#ffffff` |
| 10 | Warm white | `0xfdb` | `#ffddbb` |
| 11 | Light orange | `0xfb7` | `#ffbb77` |
| 12 | Amiga orange | `0xf94` | `#ff9944` |
| 13 | Burnt orange | `0xd61` | `#dd6611` |
| 14 | Dark umber | `0x832` | `#883322` |
| 15 | Interior/deep blue | `0x001` | `#000011` |

The visible PLANAR base behind the viewport is also `0x001`. Consequently,
both a transparent early-escape pixel and an iteration-limit pixel appear
dark, while boundary bands move through blue, white, and orange. The palette
must be reviewed from emulator screenshots and, before being frozen, on a real
PAL display. This slice does not freeze the other PLANAR colors or the final
color-response profile.

## 11. Compiler and Direct-Encoder Work

### 11.1 Frontend and typed IR

The frontend work is deliberately narrow:

1. accept `void` only as an explicit function return type;
2. accept a `void` return by reaching the end of the function, with no value;
3. accept optional semicolons between statements;
4. resolve `pset` as a statically known intrinsic with the exact signature
   above;
5. accept the intrinsic call only as a statement;
6. retain call-site line and column data;
7. represent the call as an observable IR operation that cannot be removed or
   reordered across other observable operations.

The initial source still contains one public function. General functions,
overloading, dynamic dispatch, and first-class function values remain out of
scope.

### 11.2 Call-aware optimization

Value liveness must model the intrinsic's caller-saved clobbers. Values live
across `pset` must reside in callee-saved registers or spill slots, or be moved
there before the call. `phi` coalescing and dead-value removal may not erase,
duplicate, merge, or reorder calls.

The default program is a useful pressure case because loop coordinates and
fixed-point state cross nested control-flow edges, while only the values needed
by the following pixel remain live across `pset`.

### 11.3 Runtime context extension

ABI 0.6 reserves `A5` for an immutable runtime-context pointer and defines the
non-returning fault-handler pointer at offset zero. Runtime services require a
versioned ABI extension. The exact ABI revision is frozen in the ABI document
before implementation, with at least:

- the existing fault handler at offset `0`;
- one immutable `pset` service entry;
- access to the active runtime graphics state without exposing it to source
  code;
- documented service clobbers and stack alignment;
- a context size/version validation rule.

The service entry is a private-ABI shim, not an Amiga C ABI call made directly
by generated code.

### 11.4 Shared low-level instruction form

The GNU assembly renderer and direct encoder must consume the same bounded
low-level instruction sequence. The initial form needs only the instruction,
addressing-mode, label, branch, literal-pool, prologue/epilogue, fixed-point,
fault, and intrinsic-call variants reachable from the default source and its
negative tests.

It must not encode by parsing the generated GNU assembly text. Assembly text
is one renderer; machine code is another renderer from the shared form.

### 11.5 Direct image

The direct encoder performs two bounded passes:

1. select instruction sizes and assign offsets to labels, code, and immutable
   data;
2. emit big-endian words/long words and resolve checked displacements.

Every write checks capacity. Unsupported instructions, invalid register
classes, odd targets, overflowing displacements, unresolved labels, and image
size overflow are compile errors. The produced image has an explicit entry
offset, byte length, maximum stack requirement, runtime-ABI version, and source
map/fault metadata.

The target copies or emits the checked image into executable memory, performs
the required Exec cache synchronization for the exact range, and enters it
only through the runtime trampoline.

## 12. Guarded Execution

The first target trampoline must:

- save the Amiga C environment it will restore;
- install the dedicated generated-code stack and its guard regions;
- load the validated runtime context into `A5`;
- enter the generated function with an ABI-correct return address;
- distinguish normal return from a non-returning controlled fault;
- restore the host stack and preserved state on every supported exit;
- validate stack guards after execution;
- reject an image whose declared stack or ABI requirements do not fit.

The shipped source is read-only and bounded, but generated loops must still
have an execution budget. The first implementation may use a deterministic
back-edge budget rather than final interactive stop polling. Budget exhaustion
uses the same controlled restoration path as other runtime faults. Responsive
`Esc` interruption during a running generated function belongs to the later
hosted/exclusive input-safe-point work; `Esc` is required in the result and
error states for this slice.

## 13. Build and ADF Layout

Implemented build targets are:

```sh
gmake source-view-test
gmake compiler-encoder-test
gmake compiler-encoder-musashi-test
gmake miga80-demo-inspect
gmake miga80-demo-adf
gmake miga80-demo-adf-inspect
gmake miga80-demo-adf-fs-uae
gmake miga80-demo-adf-fs-uae-autorun
```

The image contains:

```text
S/Startup-Sequence
MIGA80
DATA/DEFAULT.LUA
DATA/FONT4X8.BIN
README.TXT
LICENSE.TXT
```

The font binary is generated from its checked-in source and license. The ADF
manifest records raw size, filesystem listing, and executable, source, font,
and complete-ADF checksums. The payload must remain within the existing
800 KiB planning budget.

The application waits for `F5` or `Esc` after drawing the source. `F5` presents
parse, typed-IR lowering, and direct-encoding states, executes the generated
function, and leaves its Mandelbrot output visible. `Esc` returns to the source
from result/error and exits from the source state. An `AUTORUN` command-line
argument exists only to let the FS-UAE harness exercise the same path without
synthesizing host keyboard input.

### 13.1 Implemented source-view checkpoint

The 2026-09-05 Kickstart 3.0 FS-UAE run passes with:

- a 9,436-byte Hunk executable with 6,568 text bytes, 304 data bytes, and
  4,924 BSS bytes;
- a 643-byte source file occupying exactly 30 rows with a 44-column maximum;
- source FNV-1a checksum `6600f4de`;
- canonical source-view framebuffer checksum `f05779cc`;
- verified AGA dual-playfield palette bases, RGB round-trip, C2P output, and
  bitmap pixel readback;
- 39 occupied OFS blocks, reported as 19 KiB including filesystem overhead.

The raw DD ADF remains exactly 901,120 bytes regardless of used blocks. Its
SHA-256 is recorded in the adjacent generated manifest and may change when
timestamps or any packaged input change.

### 13.2 Implemented native-execution checkpoint

The 2026-09-05 automated A1200 FS-UAE run now also proves:

- successful on-target parsing of 73 AST nodes and 28 statements;
- lowering to 112 typed-IR instructions in 17 basic blocks;
- direct emission of 744 bytes of position-independent 68020 code, checksum
  `69f4c1eb`, without an assembler or linker on the ADF;
- instruction-cache synchronization followed by execution through `A5` and
  the private `pset` service;
- a canonical result checksum of `c4604fc7`, identical to the typed-IR
  evaluator, the direct image under Musashi, and the GNU-assembly oracle under
  Musashi;
- 20,480 observable `pset` calls in both Musashi paths;
- a 53,360-byte Hunk executable with 45,652 text bytes, 548 data bytes, and
  5,000 BSS bytes;
- 130 occupied OFS blocks, reported as 65 KiB including filesystem overhead.

The first integration run also exposed an ABI-bridge defect: generated code
correctly treated `D2` as caller-saved under the MIGA-80 ABI, while the Amiga C
caller kept the framebuffer pointer in its callee-saved `D2`. The trampoline
now preserves `D2` when crossing from the Amiga C ABI into generated code and
restores it before returning to C. The target checksum regression covers this
boundary.

## 14. Validation Matrix

### 14.1 Native host tests

- font conversion, dimensions, bit order, character coverage, and a raster
  golden;
- exact source-view canonical pixel output;
- successful parsing/lowering of the shipped source;
- negative `void`, semicolon, intrinsic signature, and call-context tests;
- typed-IR evaluation with a mocked `pset` and a stable pixel checksum;
- direct-encoder byte tests for every introduced instruction and fixup kind;
- capacity, range, alignment, and unresolved-label failures.

### 14.2 Musashi differential

- assemble the shared low-level program through the GNU development route;
- encode it through the shipping direct route;
- execute both with identical runtime-context and `pset` mocks;
- compare final pixel buffers, normal/fault exit, preserved registers, stack
  balance, stack guards, instruction budget, and observable call trace;
- retain disassembly and a short instruction trace when either path differs.

Musashi validates behavior and instruction encoding. It is not a cycle-accurate
performance oracle for a stock A1200.

### 14.3 Amiga integration

- cross-build the complete C99 shell/compiler for `68020` with no FPU;
- inspect the Hunk, map, undefined symbols, and linked size;
- exercise source load and direct encoding under `vamos` where OS behavior is
  sufficient;
- boot a copy of the actual ADF under the pinned FS-UAE A1200 profile;
- verify the compiler/execution report and final canonical checksum;
- verify that returning to the source view and exiting restore the hosted
  display without an incremental memory leak.

### 14.4 Physical follow-up

The same ADF is run on a stock PAL A1200. The report records compile time,
execution time, C2P/publication time, total and largest free Chip RAM before and
after, generated image bytes, maximum stack use, and final checksum. These
measurements establish the first real performance signal; emulator timings do
not substitute for them.

## 15. Acceptance Criteria

The vertical slice is complete when:

1. the distributable ADF boots without a Workbench disk and launches MIGA-80;
2. the complete 30-line default source is legible on one 256 x 256 screen;
3. `F5` compiles the visible ADF source on the target without external tools;
4. the direct image passes validation, cache synchronization, and guarded
   entry;
5. generated code invokes `pset` and returns normally;
6. the 160 x 128 Mandelbrot viewport is visible with the documented palette;
7. the target pixel checksum matches the typed-IR and Musashi oracle;
8. a deliberately invalid source produces a bounded line/column diagnostic;
9. a forced runtime fault and budget exhaustion restore the shell;
10. `Esc` returns from result/error to source and exits cleanly from source;
11. repeated compile/run/return cycles show no incremental memory or resource
    leak under FS-UAE;
12. the image and its manifest remain reproducible and within the floppy
    budget.

This checkpoint proves the complete product loop at minimum scale. It does not
replace the later editor, exclusive-runtime, graphics-performance, and
physical-hardware release gates.

## 16. Implementation Order

1. **Completed 2026-09-05 -- visible shell:** reusable hosted screen, input
   state, checked font importer, source raster, palette, default source file,
   boot report, and bootable ADF.
2. **Partly completed -- callable compiler:** `void`, semicolons, typed `pset`,
   stack IR, runtime-context service, and Musashi mock are implemented;
   call-aware value-IR liveness remains.
3. **Partly completed -- direct code:** the checked O0 encoder, target
   executable allocation, cache synchronization, and C/ABI trampoline are
   implemented; the shared O1 instruction form and complete guards remain.
4. **Partly completed -- integrated Mandelbrot:** byte-per-pixel rendering,
   one-shot C2P/publication, checksums, and source/result states are
   implemented; budgets, forced-fault coverage, and repeated-state automation
   remain.
5. **Release proof:** automated FS-UAE boot, reproducible ADF/manifest, then
   timing and visual review on a stock PAL A1200.

Each step must leave the ordinary `gmake check` suite passing. New generated
code first lands behind host and Musashi differentials before it is allowed to
run through the target trampoline.

## 17. Related Documents

- [MIGA-80 Specification and Roadmap](./MIGA-80-specification-and-roadmap.md)
- [MIGA Lua Compiler Bootstrap](./MIGA-Lua-compiler-bootstrap.md)
- [MIGA Lua Native ABI 0](./MIGA-Lua-native-ABI-v0.md)
- [MIGA Lua Optimization Strategy](./MIGA-Lua-optimization-strategy.md)
- [Hosted AGA Screen Smoke Test](./aga-screen-smoke.md)
- [Four-Plane C2P Reference and Benchmark](./c2p4-benchmark.md)
- [Three-Layer Graphics Reference Compositor](./graphics-reference-compositor.md)
