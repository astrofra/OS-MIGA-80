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
