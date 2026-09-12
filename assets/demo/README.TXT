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
then Esc again to close MIGA-80. Errors also return to the source with Esc.
Generated execution uses a separate guarded 4 KiB stack and stops with a
source-located error if its loop budget is exhausted.

The source is stored in DATA/DEFAULT.LUA. FONT4X8.BIN is the deterministic
runtime export of the freely licensed project font in works/ID.
