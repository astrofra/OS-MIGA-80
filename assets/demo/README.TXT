MIGA-80 LUA GRAPHICS DEMOS - DEVELOPMENT PREVIEW

Boot release/miga80-<version>.adf on a PAL Amiga 1200 with 2 MiB Chip RAM and
Kickstart 3.0 or 3.1. No Fast RAM is required. This reference floppy opens
the graphical file selector at SYS:demos after a 3.2-second logo intro.
The centered logo appears and disappears through glitch bands, accompanied
by three "A-MI-GA" chords (low, high, middle). The 68000 synthesizes the sound
at startup; Paula plays it in stereo. No recorded music file is needed.
The intro runs once per launch, before the selector or source view appears.
Opening another file, F5 replay and ESC return never restart it.
ESC or a left click skips the intro; CTRL-Q exits. NOSPLASH is available as
a CLI option after the report path and optional BROWSE mode.

The disk includes all six Lua demos:

  default.lua       Mandelbrot fractal, chunky pixels.
  layers.lua        Lines and pixels on both playfields.
  cube.lua          Ten-second 3D wireframe cube, direct blitter lines.
  cube-chunky.lua   Same cube, CPU lines and Kalms C2P.
  cube-solid.lua    Ten-second solid cube with flat lighting, blitter.
  cube-solid-chunky.lua  Same solid cube, ASM spans and Kalms C2P.

Both solid cubes play mods/93_10_12_A_SYNTH_1.mod via Paula and CIA timers.
The music stops when the demo ends or ESC is pressed.

OPENING A DEMO

Click a file once to select it. Double-click, Return, or the OPEN button
loads it into the source view. Loading does not execute the program.
Folders open in the same way. SYS: returns to the boot volume root;
PARENT or Backspace goes up one folder. REFRESH reads the directory again.
PREV PAGE / NEXT PAGE browse long lists; Up/Down selects with the keyboard.
The list shows folders and .lua files, sorted with folders first.
ESC or CANCEL returns to the previous source without changing it.

The source view is editable on the existing 256x256 LORES screen. Type text or
Return to insert, use Backspace/Delete and the arrow keys, and hold either
Shift key with an arrow to select. CTRL-C, CTRL-X and CTRL-V use an internal
MIGA-80 clipboard. Typed Tab inserts spaces to the next four-column stop;
loaded tab bytes are preserved and displayed at the same stops. The current
line and column appear in the footer. Long lines and files scroll.

CTRL-O or the top OPEN button reopens the selector. CTRL-S saves to the current
file; SHIFT-CTRL-S opens Save As. Save As accepts a filename of 1 to 30
characters and lets you navigate to another directory or disk. Existing files
require confirmation. Modified documents offer Save / Discard / Cancel before
Open or CTRL-Q. Failed or cancelled file operations preserve the live source.
The boot floppy may be write-protected or full, so save to RAM: for temporary
work or insert a writable project disk for persistent work.

F5 compiles and runs the live in-memory source without saving it first. ESC
returns from the result or error screen, and stops a running demo. CTRL-Q
closes MIGA-80. Text and shortcuts follow the AmigaOS keyboard layout.
Runtime diagnostics go to RAM:MIGA80-BOOTED.TXT.

Documents are limited to 16 KiB and use printable ASCII, tabs and LF newlines;
CRLF files are normalized to LF when loaded. An unreadable, oversized or
unsupported source leaves the previous source intact and shows an error in the
selector. Directories over 4096 entries are rejected without replacing the
previous listing. Long names are shortened on screen only. HIRES editing is
planned but is not part of this release.

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

tri(x0,y0,x1,y1,x2,y2,color) fills a triangle on the selected layer.
Coordinates are i32 values in -32768..32767, clipped to the screen; invalid
coordinates/colors and degenerate triangles draw nothing. Shared edges use
pixel-center coverage with a top-left rule, avoiding cracks or double fill.
PLANAR triangles use a Chip RAM mask, direct blitter area fill and four-plane
color writes. PIXEL triangles use an integer edge walker and ASM span fill.
The runtime preallocates an 8 KiB mask and about 1 KiB of edge scratch.

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

The wireframe cubes use depth shading; the solid cubes use face normals for
flat lighting and hidden-face rejection. All rotate on X and Y for ten
seconds. Four trig values are calculated once per frame. Each chunky
variant converts the entire framebuffer to PF1 at each flip. Kalms is the
default C2P, selected on 2026-09-14. Physical A1200 timings remain pending;
FS-UAE comparisons are documented in the source repository.

The older source-view and cube autoboot ADFs under build/distribution are
development/test variants. Their compatibility paths remain DATA/DEFAULT.LUA,
DATA/LAYERS.LUA and DATA/CUBE.LUA. The versioned ADF in release/ is the reference disk.

MOD MUSIC

music_play("SYS:mods/93_10_12_A_SYNTH_1.mod") starts or restarts the song.
Paths must be string literals; files are preloaded before native execution.
music_stop() stops it. music_position() returns the next row as order*64+row,
zero-based, or -1 when stopped. music_mute(u8) uses bits 0..3 to mute channels;
0 restores them, 15 mutes all without stopping the song's clock.

Standard four-channel, 31-sample MODs (M.K., M!K!, 4CHN), at most 256 KiB each.
The combined original/replay buffers are limited to 512 KiB. Invalid files,
out-of-range loops and effect 8 panning are rejected before playback.
The runtime reserves audio.device and CIA-B resources, then drives Paula
registers directly in the interrupt replayer. A busy resource gives an error.
No audio processing depends on the frame rate. End, ESC and faults stop DMA
and release all music resources before returning to the editor.

CREDITS

MIGA-80 and the project 4x8 bitmap font: see LICENSE.TXT.
Kalms C2P by Mikael Kalms, Public Domain, adapted from c2p1x1_4_c5_bm.s.
Original notice: KALMS.TXT. Source: https://github.com/Kalmalyzer/kalms-c2p
The repository preserves the original source in third_party/kalms-c2p.
No Kickstart ROM or Workbench files are included on this disk.

ptplayer 6.4 by Frank Wille (Public Domain): PTPLAYER.TXT.
Source: https://aminet.net/package/mus/play/ptplayer
Module a_synth_1: internal credit "by erk 12/10/93".
