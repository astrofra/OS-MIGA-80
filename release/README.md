# MIGA-80 reference floppy

[miga80.adf](miga80.adf) is the reference **development preview**, a bootable
880 KiB OFS disk for a PAL Amiga 1200, 2 MiB Chip RAM, Kickstart 3.0/3.1.
It shows the centered logo with a 3.2-second glitch intro and a procedural
“A–MI–GA” jingle, then opens the graphical file selector at `SYS:demos`.
The intro runs once per application launch, before the file workflow appears;
F2, F5 and returns to the source do not replay it.
ESC or a left click skips the intro; CTRL-Q exits.

- `default.lua`: Mandelbrot fractal.
- `layers.lua`: PIXEL and PLANAR drawing.
- `cube.lua`: rotating wireframe cube, hardware blitter, ten seconds.
- `cube-chunky.lua`: the same cube with CPU lines and Kalms C2P.

Click to select; double-click to load. F5 runs the displayed source. ESC stops
execution or returns to the source. F2 or **OPEN** reopens the selector.
CTRL-Q exits. The source view remains read-only. See the disk's `README.TXT`
for navigation, runtime details and current source limits.

The stable filename is intentional: Git records successive reference images.
`miga80.manifest.json` records the image SHA-256 and every packaged payload.
These files belong in version control; `gmake clean` leaves `release/` intact.
No Kickstart ROM, Workbench files, test fixtures or benchmark reports are included.
Normal use writes diagnostics to RAM only.

Rebuild and validate from the repository root:

```sh
gmake release
gmake release-intro-fs-uae
gmake release-fs-uae
```

The builder packages every `assets/demo/*.lua`, checks the OFS structure and
compares extracted payloads byte for byte. The emulator test uses a separate
copy with temporary fixtures; it never writes to this reference image.
The current validation runs on FS-UAE PAL A1200 / 2 MiB Chip / no Fast RAM.
Physical Amiga feedback is still pending. This preview is not the completed
version 1 described by the roadmap.
