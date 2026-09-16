# File selector and reference preview

The reference disk is `release/miga80-<version>.adf`, a bootable 880 KiB OFS preview.
`gmake release` packages all `assets/demo/*.lua` into `SYS:demos`; the current
four are Mandelbrot, both-playfield drawing, the blitter cube and the chunky
cube. `release/` is intentionally tracked, and `clean` does not remove it.
The previous source-view/cube disks in `build/distribution/` remain test and
autoboot variants. Adding a new demo asset includes it on the next release
build without changing a hardcoded GUI catalog.

The boot script uses `SYS:` and opens the selector immediately. A single click
selects, a double-click loads, and Return / OPEN also loads. Loading only
changes the read-only source view; F5 remains the explicit compile/run action.
Ctrl+O and the source header's OPEN button reopen the last directory. ESC / CANCEL
preserves the previous source, ESC also returns from results and stops native
execution, and CTRL-Q exits. The browser supports folder navigation, SYS:,
parent, refresh, twelve entries per page, mouse page buttons and Up/Down.
Names are sorted case-insensitively, directories before `.lua` files.

## Implementation

`src/demo/file_picker.c` reads the directory using `Lock`, `Examine` and
`ExNext`, with a correctly aligned `AllocDosObject(DOS_FIB)` allocation.
It distinguishes end-of-directory from an I/O error and releases locks/FIBs
on all exits. A failed scan preserves the previous path and complete listing.
The entry array grows as needed, with an explicit 4096-entry ceiling for the
2 MiB target. Long filenames remain intact internally; only their display
is shortened. Paths are bounded at 511 bytes plus the terminator.
See the [ExNext autodoc](https://d0.se/autodocs/dos.library/ExNext).

The native IDCMP loop subscribes to raw keys and mouse buttons. It copies
coordinates and timestamps before replying to each message. Double-clicks
must address the same entry, with timing checked by Intuition's
[DoubleClick](https://d0.se/autodocs/intuition.library/DoubleClick), respecting
the OS preference. Navigation, keyboard actions and clicks outside the list
reset the pending double-click. There is no additional ASL or Workbench
library payload on the disk.

Loading uses a separate bounded staging buffer. The source and its compiler
metrics change together only after the entire read and source-view validation
succeed. Missing/unreadable files, more than 4096 bytes, more than 30 lines,
more than 64 columns, or non-printable ASCII (apart from LF) are rejected with
an in-browser message. These are existing source-view limits, not a claim of
full Lua text-editor support. Empty directories remain navigable. Booting the
browser does not depend on `default.lua` being present or valid.

Interactive UI redraws use the Kalms colour4 converter to PF1 and clear PF2.
They retain the workflow's framebuffer representation for readback checks.
The historical source-view golden path and explicit runtime C2P selectors
remain available. The supervisor and animation buffer ownership are unchanged.

## Packaging and validation

`build-miga80-release.py` builds a temporary image, installs the boot block,
validates OFS with `xdfscan`, then extracts and compares every payload byte for
byte before replacing the reference. The matching versioned manifest records image and
payload SHA-256 values, source paths, sizes, and filesystem listing. The disk
contains project files, the MIT license and the upstream Kalms notice. It
contains no Kickstart ROM, Workbench files, negative test fixtures or reports.
Normal execution writes diagnostics to RAM only.

`gmake release-fs-uae` boots a separate copy on PAL A1200 / 2 MiB Chip / no Fast
RAM. Temporary fixtures exercise filtering, nested/empty folders, multiple
pages, missing files, oversized sources and malformed source text. The target
regression uses the same input dispatcher as real IDCMP, including real DOS
scanning and Intuition double-click timing. It checks all six demo loads,
single-click isolation, different-row click isolation, Ctrl+O and mouse reopening,
F5 execution of the loaded layers demo, ESC return, cancellation, CTRL-Q,
readback, repeated scans without memory growth, and hosted cleanup.
The report is `build/reports/release-browser-fs-uae.txt`.
The general `gmake check` suite also passes. A separate normal-boot check
opened `cube.lua` from the selector using keyboard input, ran it to completion
with F5, then returned to the source with ESC and to the selector with Ctrl+O.
Mouse selection/double-click are exercised by the shared-dispatch target
regression.

Physical Amiga feedback remains pending. This is a development preview and
an initial file-opening workflow, not completion of the roadmap's editor or
version 1 shell.

The release now also includes `cube-solid.lua` and `cube-solid-chunky.lua`;
see the [solid cube checkpoint](MIGA-80-solid-cube.md).
