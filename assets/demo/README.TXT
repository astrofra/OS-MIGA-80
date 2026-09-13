MIGA-80 MANDELBROT VERTICAL SLICE

Boot this disk on a PAL Amiga 1200 with Kickstart 3.0 or 3.1.
MIGA-80 starts automatically and displays the complete default MIGA Lua
source with the project 4x8 bitmap font.

MIGA-80 checks the 68020 CPU requirement before compiling. F5 runs the
compiler on a private 32 KiB stack, independently of the boot Shell stack.
Runtime diagnostics are written to RAM:MIGA80-BOOTED.TXT, so normal use does
not modify this floppy image.

The source view is read-only. Press F5 to parse and lower the displayed source,
optimize its value IR with the bounded O1 register plan, emit native 68020 code
directly into memory, and execute it. The generated program draws the
Mandelbrot image through pset. Press Esc on the result to return to the source,
then Ctrl-Q to close MIGA-80. Errors also return to the source with Esc.
Generated execution uses a separate guarded 4 KiB stack and stops with a
source-located error if its loop budget is exhausted.

During execution, Esc stops the program and returns directly to the source.
Esc never closes the application. Press Ctrl-Q to quit from the source, result,
or error screen. The shortcut follows the AmigaOS keyboard layout. A separate
Exec task makes the stop independent of loop-budget
checks. It relies on working AmigaOS scheduling and interrupts; it cannot
recover arbitrary system corruption. Compilation itself is not interrupted.

Supervision is on by default. For a direct-execution development comparison:
MIGA80:MIGA80 MIGA80:DATA/DEFAULT.LUA RAM:MIGA80-BOOTED.TXT NOSUPERVISOR
The loop budget remains active. SUPERVISOR explicitly enables supervision.

The source is stored in DATA/DEFAULT.LUA. FONT4X8.BIN is the deterministic
runtime export of the freely licensed project font in works/ID.

DRAWING ON BOTH LAYERS

PIXEL is selected at the start of each run. layer(PIXEL) selects the chunky
buffer, displayed in PF1/front after C2P. layer(PLANAR) selects four native
bitplanes, displayed in PF2/back. Both accept pset(x,y,color) and
line(x0,y0,x1,y1,color), with coordinates in 0..255 and colors in 0..15.
Lines are clipped; out-of-range pixels and colors above 15 are ignored.
Zero on PIXEL reveals PLANAR. Zero on PLANAR is the base/backdrop color.

PIXEL lines use CPU Bresenham. PLANAR lines program the blitter registers
directly. The owner takes the blitter once per batch of at most 16 commands,
suspends task switching during that batch, waits for DMA completion, and
returns the resource. Interrupts stay active so Esc can be delivered. PLANAR
pset uses CPU bit writes in the same command order. The worker owns no DMA.

After Ctrl-Q, load the two-layer example from the Shell and press F5:
MIGA80:MIGA80 MIGA80:DATA/LAYERS.LUA RAM:MIGA80-BOOTED.TXT
