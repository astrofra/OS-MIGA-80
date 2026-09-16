# Drawing primitives: PIXEL and PLANAR

Checkpoint: 2026-09-13. Native Lua drawing now addresses both playfields.
The default Mandelbrot remains the boot example; `DATA/LAYERS.LUA` is also
included on the source-view ADF. Physical A1200 validation remains pending.

```lua
function main(): void
  layer(PLANAR)
  line(0, 0, 255, 255, 3)
  pset(32, 40, 7)
  layer(PIXEL)
  line(0, 255, 255, 0, 12)
  pset(32, 40, 0)
end
```

Each run starts on `PIXEL`, preserving existing `pset` programs. `layer` accepts
only `PLANAR` or `PIXEL`. Coordinates are signed `i32`; colors are canonical
`u8`, with values above 15 ignored. A pixel outside 0..255 is ignored. Lines
include both endpoints, are canonicalized top-to-bottom (left-to-right for
horizontal lines), then clipped to the 256 x 256 surface. Integer intersections
truncate toward zero. Reversing endpoints therefore gives the same pixels.
Clipping uses bounded Cohen-Sutherland iterations and 64-bit intermediate
arithmetic, including for extreme signed coordinates.

| Layer | Storage and primitive | AGA destination |
|---|---|---|
| PIXEL | 65,536 byte pixels; assembly `pset`, CPU Bresenham `line` | PF1/front, bitmap planes 0/2/4/6 after four-plane C2P |
| PLANAR | Four 8,192-byte Chip-RAM bitplanes; CPU bit writes for `pset`, direct blitter line mode for `line` | PF2/back, bitmap planes 1/3/5/7 |

PIXEL color zero reveals PLANAR. PLANAR zero selects the base/backdrop color.
Both drawing paths replace existing color bits, including clearing bits when
writing a lower color or zero. The completion status occupies the bottom eight
PIXEL rows. Publication happens on successful completion; this slice has no
frame loop, dirty tracking, configurable viewport, or object API yet.

## Hosted AGA palette

Both playfields use the Workbench Sunset palette: PF1 selects registers 0–15,
PF2 selects 16–31. The hosted screen adds a four-instruction user Copper list
at viewport line zero: WAIT, BPLCON3=`1040`, BPLCON4=`0011`, END. This sets
PF2OF=4 (offset 16) and BPLAM=0 (no palette XOR), after the system's palette
loads. Intuition merges the list and releases it on CloseScreen, following the
[RKM user Copper list contract](https://www.theflatnet.de/pub/cbm/amiga/AmigaDevDocs/lib_27.html).

This corrects the Kickstart 3.0 / FS-UAE configuration which reported the
requested ColorMap bases but emitted BPLCON3=`1c40` and BPLCON4=`1011`.
The bitmap geometry and ColorMap roundtrip were correct while visible PF2
colors came from the wrong bank. The corrected blue/orange line fixture was
also inspected visually in FS-UAE; physical A1200 confirmation remains open.

## Direct hardware and supervision

PLANAR commands enter an owner-allocated queue of at most 16 entries. Pixels
share the queue with lines, so an erase or overwrite cannot overtake a pending
DMA operation. Full batches wake the supervising task. A partial final batch
is flushed after successful native completion and discarded on stop/fault.

The owner calls `OwnBlitter` and `WaitBlit` once at batch entry, then `Forbid`.
All line setup and completion polling use `$DFF000` directly. There are no
`Draw`, `Move`, `SetAPen`, or other graphics-library drawing calls in that path.
The owner uses blitter line mode with the A error accumulator, a constant B
texture, C/D pointers to each plane, and minterm `$CA` to replace the selected
bits. `BLTSIZE` starts each plane operation last. The algorithm follows the
[Commodore Hardware Reference Manual, blitter line mode](https://www.theflatnet.de/pub/cbm/amiga/AmigaDevDocs/hard_6.html#6-8).

DMA and blitter priority are enabled for the batch; only the bits changed by
this batch are restored afterward. Every DMA write finishes before
`DisownBlitter` and `Permit`. Each clipped line touches at most 256 pixels
per plane. The animation extension also queues full-plane clears (8,192
bytes per plane), within the same limit of 16 commands per batch. This is a bounded suspension of OS task switching. Interrupts and
the display remain active, allowing keyboard/timer delivery; it does not yet
implement a whole-program exclusive takeover with a private input interrupt.

The generated worker holds no blitter ownership, allocation, pending I/O, or
hardware register access. ESC can remove it during computation or while it
waits for the owner. If input arrives during a batch, the supervisor handles
it after that bounded batch returns. Queue buffers and signals are released
only after the worker has gone. Ctrl-Q still exits source/result/error; ESC
stops or returns to source. No physical latency/performance figure is claimed.

## Compiler contract and verification

`line(i32,i32,i32,i32,u8)` evaluates all five arguments in source order. Typed
IR retains one line call; value IR lowers it to two ordered effects:
`line_start(x0,y0,color)` and `line_end(x1,y1)`. Both obey the existing maximum
three-register native call ABI. Values needed after either call survive
`D0-D2/A0-A1` clobbers. The optional drawing context is 112 bytes, retaining all
36 guarded-profile bytes at their existing offsets. See the
[native ABI](MIGA-Lua-native-ABI-v0.md) for the added service entries.

The guarded stack bound adds 1,024 bytes for the trusted C drawing path when a
program contains a live non-pset runtime call. Mandelbrot keeps its 96-byte bound,
464-byte guarded code image, and `c4604fc7` pixel checksum.

```sh
gmake drawing-test
gmake check
gmake miga80-demo-adf-fs-uae-graphics
gmake miga80-demo-adf-fs-uae-stop
```

The direct-execution comparison uses the same expected graphics report:

```sh
MIGA80_FS_UAE_TIMEOUT_SECONDS=180 scripts/test-miga80-demo-adf-fs-uae.sh \
  build/distribution/miga80-source-view.adf \
  tests/smoke/source-view-adf/graphics-expected.txt GRAPHICSTEST_DIRECT
```

The sanitized host suite checks all octants/ties against an independent
nearest-pixel oracle, 20,000 extreme clipping inputs, pixel replacement,
argument rejection, and malformed IR. Musashi executes O0, O1, and guarded O1
from both native and GNU emission with deliberately clobbered caller registers.
O1 encodings are also byte-identical; O0's textual emitter omits fallthrough
jumps that the direct baseline retains, so their comparison is semantic.

The AGA `GRAPHICSTEST` runs the Lua fixture three times through F5, compares
PIXEL checksum `18d74cc6` and PLANAR checksum `022d41fe` with the host oracle,
checks 19 accepted hardware lines per run, and reads all 65,536 pixels from the
eight-plane destination each time. It checks source return, Ctrl-Q, signal
release, warmed free memory, and hosted cleanup. On source, result, and source
return, it also inspects the installed non-interlaced Copper list, bounded by
its own allocation while task switching is suspended: eight-plane dual mode,
PF1 priority, PF2 offset, palette XOR, and both RGB nibbles of all 32 colors.
These checks are specific to the current exclusive screen without raster
effects; a future scanline palette needs checks at each visible interval.
`STOPTEST` additionally stops
an unguarded infinite PLANAR-line loop with real input.device ESC delivery and
requires that hardware lines have actually executed before interruption.

Reports: `build/reports/drawing-host.txt`,
`build/reports/source-view-adf-graphics-fs-uae.txt`, and
`build/reports/source-view-adf-stop-fs-uae.txt`.

The animation extension adds `cls`, `flip`, `time`, `sin` and `cos`, with two
complete AGA buffers. See [the Lua cube](MIGA-80-cube-animation.md) for publication
and buffer-content semantics.

Filled triangles, their bounded coordinate domain and exact shared-edge rule
are documented with the [solid cube samples](MIGA-80-solid-cube.md). The graphics
regression now also covers clipping, degenerate triangles, one-pixel spans and
color-zero erasure on the real blitter and chunky ASM paths.
