MIGA-80 LUA GRAPHICS DEMOS - DEVELOPMENT PREVIEW

Boot release/miga80.adf on a PAL Amiga 1200 with 2 MiB Chip RAM and
Kickstart 3.0 or 3.1. No Fast RAM is required. This reference floppy opens
the graphical file selector at SYS:demos. It includes all four Lua demos:

  default.lua       Mandelbrot fractal, chunky pixels.
  layers.lua        Lines and pixels on both playfields.
  cube.lua          Ten-second 3D wireframe cube, direct blitter lines.
  cube-chunky.lua   Same cube, CPU lines and Kalms C2P.

OPENING A DEMO

Click a file once to select it. Double-click, Return, or the OPEN button
loads it into the source view. Loading does not execute the program.
Folders open in the same way. SYS: returns to the boot volume root;
PARENT or Backspace goes up one folder. REFRESH reads the directory again.
PREV PAGE / NEXT PAGE browse long lists; Up/Down selects with the keyboard.
The list shows folders and .lua files, sorted with folders first.
ESC or CANCEL returns to the previous source without changing it.

The source view is read-only. F2 or its top-right OPEN button reopens the
selector. F5 compiles and runs the displayed source. ESC returns from the
result or error screen, and stops a running demo. CTRL-Q closes MIGA-80.
The shortcut follows the AmigaOS keyboard layout. No files are saved or
modified by the selector. Runtime diagnostics go to RAM:MIGA80-BOOTED.TXT;
normal use does not write to this floppy.

This preview displays at most 30 source lines, 64 columns per line, and
4096 bytes. Sources must be printable ASCII with LF newlines. An unreadable,
oversized or unsupported source leaves the previous source intact and shows
an error in the selector. Directories over 4096 entries are rejected without
replacing the previous listing. Long names are shortened on screen only.

RUNTIME

MIGA-80 checks the 68020 requirement before compiling. F5 runs the compiler
on a private 32 KiB stack, emits native code directly, and executes it on a
separate guarded 4 KiB stack. A loop budget catches runaway loops. The Exec
task supervisor also lets ESC stop generated code independently of budget
checks. Compilation itself is not interrupted. Supervision relies on working
OS scheduling and interrupts; it cannot recover arbitrary system corruption.

From the Shell, reopen the selector:
SYS:MIGA80
Or open a particular source:
SYS:MIGA80 SYS:demos/layers.lua RAM:MIGA80-BOOTED.TXT
SUPERVISOR is the default. NOSUPERVISOR is a development-only CLI option
following the report path. C2P=MASK32 and C2P=REFERENCE select comparison
converters; C2P=KALMS explicitly selects the default.

DRAWING AND ANIMATION

layer(PIXEL) selects the chunky PF1/front buffer; layer(PLANAR) selects the
four native PF2/back bitplanes. PIXEL is initially selected. pset(x,y,color)
and line(x0,y0,x1,y1,color) accept coordinates 0..255 and colors 0..15.
Lines are clipped; out-of-range pixels and colors above 15 are ignored.
Zero on PIXEL reveals PLANAR. Zero on PLANAR is the backdrop color.

PIXEL lines use CPU Bresenham. PLANAR lines use direct blitter registers.
The owner takes the blitter for batches of at most 16 commands, suspends
task switching for each batch, waits for completion, and returns it.
Interrupts remain active. PLANAR pset uses CPU bit writes in command order.

sin(fix) and cos(fix) use radians and signed Q16.16. cls(u8) clears the
selected layer. flip() publishes the hidden bitmap and waits until the
previous bitmap is safe to draw into. Clear each PLANAR frame explicitly;
flip does not clear or copy it. time() returns fix seconds since the first
published frame, or zero before it. Double buffering uses another 128 KiB
of Chip RAM and preserves the source screen. ESC works during buffer waits.

Both cubes rotate on X and Y with depth shading for ten seconds. The chunky
variant converts the entire framebuffer to PF1 at each flip. Kalms is the
default C2P, selected on 2026-09-14. Physical A1200 timings remain pending;
FS-UAE comparisons are documented in the source repository.

The older source-view and cube autoboot ADFs under build/distribution are
development/test variants. Their compatibility paths remain DATA/DEFAULT.LUA,
DATA/LAYERS.LUA and DATA/CUBE.LUA. release/miga80.adf is the reference disk.

CREDITS

MIGA-80 and the project 4x8 bitmap font: see LICENSE.TXT.
Kalms C2P by Mikael Kalms, Public Domain, adapted from c2p1x1_4_c5_bm.s.
Original notice: KALMS.TXT. Source: https://github.com/Kalmalyzer/kalms-c2p
The repository preserves the original source in third_party/kalms-c2p.
No Kickstart ROM or Workbench files are included on this disk.
