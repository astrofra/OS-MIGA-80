# Boot logo and procedural jingle

Normal MIGA-80 startup presents `works/logo.png`, then opens the source view or
`SYS:demos` selector. The intro is called once in startup, before any source or
selector framebuffer is published. File loading, F2, F5 and ESC returns never
invoke it. The 196 x 43 logo keeps its native pixels and four original
RGB colors. Its top-left position is (30, 106) on the 256 x 256 display, centered
to the nearest pixel on both axes. The image is encoded into the executable at
build time; no PNG decoder or external image read is needed on the Amiga.
Logo export and host previews require Pillow in the build Python environment.

The mouse pointer stays hidden throughout intro preparation, playback and the
transition to the first source or selector frame, including ESC/left-click skips.
A transparent Chip RAM sprite is installed with `SetPointer()`; `ClearPointer()`
restores the default pointer once the interface is displayed. Its storage remains
alive until the window and screen close, including early quit/error paths. See
the [Intuition pointer documentation](https://wiki.amigaos.net/wiki/Intuition_Pointer).
If this small allocation fails, startup skips the intro and opens the interface.

The intro lasts 3.2 seconds from its first displayed frame: horizontal band
shifts, dropped fragments and restrained cyan/magenta offsets resolve into the
clean logo, followed by a fragmented exit. Short color offsets follow the
second and third musical attacks. It uses the existing synchronized screen
buffers and Kalms C2P on a 256 x 64 strip. The rest of the bitmap stays black.
Timing comes from EClock, so rendering load does not stretch the sequence.

The jingle is original integer synthesis, generated on the Amiga. Its separate
C object is explicitly compiled with `-m68000 -msoft-float`. Four oscillators
produce three chords (A major, E major, A major); the dominant voice plays A4,
E5, C#5, following the requested low / high / middle “A–MI–GA” contour. Triangle
waves and a quiet square harmonic provide an arcade sound, with attack and
release envelopes and headroom below 8-bit clipping. There is no recorded
sample, music file, floating-point synthesis or narrator dependency.

The CPU generates 33,076 signed PCM bytes in Chip RAM. Paula plays this buffer
once through a synchronized left/right pair, at PAL period 322 (about 11,015 Hz;
the composition uses a nominal 11,025 Hz). `audio.device` reserves an available
pair at minimum precedence without waiting or stealing. `BeginIO` preserves
its audio flags; one write is queued per channel between CMD_STOP/CMD_START.
The device owns the DMA lifecycle. See the official
[audio device documentation](https://wiki.amigaos.net/wiki/Audio_Device).

ESC or a left click skips the intro; CTRL-Q exits. Playback requests are
aborted/drained before freeing PCM, closing the device and deleting ports.
Screen buffer replies are drained before restoring the original bitmap; the
caller then restores the UI palette. If audio is unavailable the visual intro
continues silently. A graphics allocation failure skips the intro and lets
the file workflow open. Add `NOSPLASH` after the report path (and optional
`BROWSE` mode) to bypass it during development. Regression modes run their
existing tests without the automatic intro.

## Verification

`gmake intro-test` uses ASan/UBSan to check all animation phases, output bounds,
canonical color ranges, the exact centered logo pixels, initial/final black,
audio buffer guards, silence and clipping headroom. Independent DFT checks
find the three melody peaks at 440, 659 and 554 Hz. It exports
`build/reports/intro/boot-intro.gif` and `boot-jingle.wav` from the same portable
core used on target; the WAV uses the nominal sample rate.

`gmake release-intro-fs-uae` uses a copy of the release disk. It checks two full
intro runs, EClock duration, the CPU-generated PCM checksum against the host,
active stereo Paula DMA, completion of both playback requests, repeated
allocation without memory growth, ESC injected through input.device, the quit
cleanup path, reacquisition of all four audio channels, restoration of the
source bitmap/palette, and hosted cleanup. Report:
`build/reports/release-intro-fs-uae.txt`.

`gmake release-fs-uae` continues to verify file loading and execution after the
new code is linked into the reference ADF. Physical Amiga timing and audio
feedback remain pending.
