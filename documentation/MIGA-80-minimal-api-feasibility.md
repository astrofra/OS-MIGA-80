# MIGA-80 Minimal Runtime API Feasibility Study

- **Status:** design proposal, not an implemented or frozen API
- **Date:** 2026-09-16
- **Scope:** indexed graphics, off-screen drawing and MOD playback; sprites, tile
  maps, sound effects and input are intentionally outside this study

This document distinguishes the API that exists in the repository from the API
proposed here. Names and contracts under "Proposed" are recommendations, not
claims about the current compiler or runtime.

## Executive summary

A useful minimal API is feasible without imitating PICO-8 or TIC-80's memory maps.
Most of its rendering core already exists: MIGA-80 can select either playfield,
clear it, plot pixels, draw clipped lines and filled triangles, double-buffer the
complete display, and play validated four-channel MOD files.

The recommended first public slice is:

- retain `cls`, `pset`, `line`, `tri`, `flip`, `time`, and the two logical screen
  layers;
- add `pget`, filled and outline rectangles, atomic palette updates, and a
  declarative color-response selection;
- add `music_pause`, `music_resume`, and `music_volume` to the existing music
  API;
- follow with opaque, preallocated indexed surfaces and `blit`/`blit_key`;
- do not expose real addresses, Chip RAM, bitplanes, `malloc`, `peek`, `poke`, or
  a general-purpose runtime allocator.

The important architectural choice is to model off-screen images as typed
**surface handles**, not raw memory blocks. A surface may internally be packed
4-bit, byte-per-pixel, planar, cached in more than one representation, or moved
between Fast and Chip RAM without changing cartridge code. Allocation should be
resolved and budgeted before native execution; there should be no per-frame
allocation or garbage collection in the first version.

This gives MIGA-80 the small, immediate API that makes fantasy consoles pleasant
to use while preserving the Amiga-specific advantages of native planar drawing,
the blitter, dual playfields and Paula audio.

This is a complete first **graphics and music slice**, not a complete console API.
Input, text, persistent storage, sprites, tile maps and sound effects still need
separate designs before the whole cartridge API can be called complete.

## Existing baseline

The current source API and runtime already provide more than a blank starting
point.

| Area | Current behavior | Remaining API work |
| --- | --- | --- |
| Target selection | `layer(PLANAR)` and `layer(PIXEL)` | Generalize carefully if scratch surfaces are added |
| Clear | `cls(color)` on the selected layer | Already complete for the minimum |
| Pixel write | `pset(x, y, color)` | Already complete |
| Pixel read | Internal planar reader exists; no Lua call | Add `pget` and synchronization semantics |
| Lines | Clipped, endpoint-inclusive `line` on both layers | Already complete |
| Polygons | Filled, clipped `tri` on both layers | Keep triangle as the minimal polygon primitive |
| Presentation | Two complete AGA buffers and `flip()` | Already complete; keep front/back buffers private |
| Palette | Fixed 16-color banks, 31 opaque colors in dual-playfield mode | Add logical palette operations, color-response selection and frame-boundary publication |
| Off-screen images | No public scratch surface or block copy | Add handles and blitting in a second slice |
| Music | `music_play`, `music_stop`, `music_mute`, `music_position` | Add pause/resume and master volume |

The exact current drawing behavior is documented in
[Drawing primitives](MIGA-80-drawing-primitives.md),
[Cube animation](MIGA-80-cube-animation.md), and
[Solid cubes](MIGA-80-solid-cube.md). The existing MOD contract is in
[MOD playback](MIGA-80-mod-playback.md).

The current display is 256 x 256 with two four-bit logical playfields. `PLANAR`
is the opaque base. `PIXEL` is the overlay and index 0 reveals `PLANAR`. Together
they provide 31 opaque palette entries. The implementation currently keeps a
64 KiB byte-per-pixel `PIXEL` source and two complete eight-plane AGA display
buffers. These facts make additional full-screen scratch buffers possible in
principle, but not free on a stock 2 MiB machine.

Current integration and performance results are emulator results. Physical A1200
validation is still pending, so this study does not promise a frame rate for new
readback or blit paths.

## Lessons from PICO-8, TIC-80 and Lua

### What is worth borrowing

PICO-8's graphics API separates simple drawing operations from its low-level
memory functions. It has `cls`, `pset`, `pget`, lines, rectangles, circles,
sprites, maps, clipping and camera state. Its `pal` operation distinguishes draw
remapping from display remapping; it remaps fixed color indices rather than
installing arbitrary RGB values. Its screen and graphics data live in a fixed
virtual address space. `memcpy` copies overlapping regions of that virtual RAM.
See the official [PICO-8 manual](https://www.lexaloffle.com/dl/docs/pico-8_manual.html).

TIC-80 makes the read/write symmetry even more compact: `pix(x,y,color)` writes
and `pix(x,y)` reads. It exposes drawing primitives, `memcpy`, two composited VRAM
banks, and a separate 16-entry RGB24 palette in each bank. Its overlay bank has
a selectable transparent index. See the official TIC-80 wiki pages for
[`pix`](https://github.com/nesbox/TIC-80/wiki/pix),
[`vbank`](https://github.com/nesbox/TIC-80/wiki/vbank),
[`memcpy`](https://github.com/nesbox/TIC-80/wiki/memcpy), and the
[RAM/VRAM layout](https://github.com/nesbox/TIC-80/wiki/ram).

The useful common ideas are:

- indexed colors remain data; changing the display palette recolors pixels that
  have already been drawn;
- reading a pixel is as well-defined as writing one;
- clear, rectangle and block copy are first-class operations rather than Lua
  loops;
- clipping and target selection are stateful, keeping common calls short;
- presentation and resource storage are separate concerns.

### What should not be copied

PICO-8 and TIC-80 can safely publish numeric addresses because each defines a
small, fixed virtual machine memory map. MIGA-80 has materially different memory
classes and hardware constraints:

- only Chip RAM is visible to the blitter, bitplane DMA and Paula;
- Fast RAM, when present, is preferable for some CPU-only data;
- planar, packed-4 and byte-per-pixel representations have different costs;
- display buffers have alignment, ownership and safe-publication requirements;
- the runtime must still fit and behave predictably with exactly 2 MiB Chip RAM.

Consequently, a TIC-80-style `memcpy(address,address,length)` would either expose
real Amiga internals or force MIGA-80 to freeze an artificial memory map too
early. Neither is needed to obtain useful block drawing.

Full Lua can represent host-owned memory with garbage-collected userdata and can
route all allocation through a host allocator. The official
[Lua 5.4 manual](https://www.lua.org/manual/5.4/manual.html) describes both
facilities. MIGA Lua is currently a statically typed native subset with scalar,
fixed-point, immutable string and symbol values; it has neither general tables
nor userdata nor a garbage collector. Adding standard-Lua allocation semantics
would therefore be a language/runtime project, not a small graphics API change.

### Do PICO-8 or TIC-80 allocate drawable off-screen buffers?

Not in the general sense of `surface_new(width, height)`. Their low-level
techniques are useful precedents, but they depend on fixed fantasy-machine
layouts:

| System | What cartridge code can do | What it does not provide |
| --- | --- | --- |
| PICO-8 | Use a fixed 64 KiB base-RAM map, copy overlapping byte ranges with `memcpy`, and remap the 8 KiB screen or graphics area to a small set of fixed banks | Arbitrary-sized drawable-surface allocation |
| TIC-80 | Select either of two fixed 16 KiB VRAM banks, access a defined RAM/VRAM map, and copy byte ranges with `memcpy` | A general graphics allocator or an unbounded set of render targets |
| Standard Lua | Allocate managed tables, strings and userdata through the Lua runtime and its host allocator | A portable pixel format or graphics target merely from having allocated bytes |

PICO-8's manual explicitly separates its 64 KiB base RAM, 32 KiB cart ROM and
2 MiB Lua RAM, and documents only fixed choices for remapping the graphics and
screen areas. TIC-80 similarly states that its first memory region is reserved
system I/O and maps one of two VRAM banks into it. In both systems, ordinary Lua
allocation and drawable video storage are different concepts.

Therefore MIGA-80 should borrow the *workflow*--draw somewhere, then copy it--but
not the address model. An opaque surface allocation can provide that workflow
while retaining the width, height, stride, format, memory class and lifetime
metadata required by the Amiga backend.

## Design principles

1. **Expose logical resources, never machine addresses.** Cartridge code should
   see layers, surfaces, palette indices and RGB12 values.
2. **Keep the common calls small.** Stateful target selection is appropriate for
   a compact drawing API.
3. **Make synchronization visible in the contract.** A planar `pget` may be much
   more expensive than a chunky `pget`, even when both return the same result.
4. **Commit visible state at `flip`.** Pixel, palette and layer changes should
   become visible together, with no partial palette frame.
5. **Allocate before execution.** Resource failure should be reported during
   preflight with requested bytes, available bytes and the largest free block.
6. **Keep representation private.** A future packed-4 conversion, planar cache or
   Fast-RAM optimization must not require source changes.
7. **Specify fallback results, not just fast paths.** Optimized and portable paths
   must produce the same indexed image.
8. **Do not allocate in a frame.** The first API should have no per-frame heap
   activity, garbage-collection pause or surface fragmentation failure.

## Proposed minimum graphics API

The signatures below use the current explicit MIGA Lua style. Optional arguments
and overloads are deliberately avoided.

### Core target and drawing calls

```lua
target(PLANAR)
cls(0)
pset(12, 20, 3)
local c:u8 = pget(12, 20)
line(0, 0, 255, 255, 8)
rect(16, 16, 64, 32, 5)
rectb(16, 16, 64, 32, 15)
tri(32, 32, 224, 64, 128, 224, 8)
flip()
```

`target` is the recommended long-term name because it can later accept a scratch
surface as well as `PLANAR` and `PIXEL`. Until surface handles exist,
`layer(PLANAR|PIXEL)` can remain the implemented spelling. If compatibility is
already important, `layer` can remain as a permanent two-layer convenience
alias rather than forcing an immediate rename.

Recommended contracts:

- coordinates are signed `i32`, origin at the top left;
- colors are indices 0..15; out-of-range colors perform no drawing;
- `cls(color)` clears the complete current target. If clipping is added later,
  `cls` also resets the clip rectangle; use `rect` for a partial clear;
- all primitives clip and never wrap;
- `rect(x,y,w,h,color)` uses a top-left origin and positive width/height, matching
  the eventual blit rectangle; zero width or height is a no-op;
- `rectb` draws the pixels inside the same half-open rectangle as `rect`;
- `tri` remains the minimum filled-polygon operation. A general polygon API is
  unnecessary until vertex arrays exist;
- `flip` publishes both screen layers together. Scratch surfaces are not
  displayed by `flip`.

Filled and outline rectangles are recommended even though they were not in the
initial list. They are common for UI, bars, tile preparation and dirty-region
repair, and both CPU spans and the blitter can implement them much more cheaply
than repeated Lua lines.

Circles, ellipses, arbitrary polygons, text, camera transforms and patterned
fills can wait. `clip(x,y,w,h)` plus `clip_reset()` is the most useful next
stateful feature after this minimum, because both PICO-8 and TIC-80 demonstrate
its value and it reduces application-side bounds work.

### `pget` semantics

```lua
local color:u8 = pget(x, y)
```

`pget` reads the **current drawing target**, not the front buffer currently being
scanned out. It returns 0 outside the target. It observes every earlier drawing
call to that target in program order.

That last rule is important. `PIXEL` and a CPU scratch surface can normally be
read directly. `PLANAR` may have queued blitter lines, fills or triangles, so a
planar `pget` is a synchronization barrier: the owner must submit and finish
earlier commands before reading the four plane bits. This is feasible using the
existing planar pixel reader, but frequent planar readback will destroy batching
and should be documented as a slow operation. No cache with stale results should
be exposed.

This is preferable to restricting `pget` to `PIXEL`: one semantic rule is easier
to learn, and optimized code can still choose the appropriate target.

## Palette API

MIGA-80 should expose real logical palette changes, not PICO-8's draw-time color
remapping in the first version.

```lua
color_response(RESPONSE_WARM_NEGATIVE)
palette_set(PLANAR, 3, 0xf80)
local rgb:u16 = palette_get(PLANAR, 3)
palette_use(PLANAR, "SYS:palettes/sunset.pal")
palette_reset(PLANAR)
flip()
```

Recommended behavior:

- RGB values use the existing logical Amiga `0xRGB` gamut, 0x000..0xfff;
- `PLANAR` owns 16 opaque entries; `PIXEL` owns 15 visible entries and index 0
  remains transparent regardless of its stored RGB value;
- `OBJECTS`, when introduced, shares the overlay palette unless the object
  architecture proves that a separate logical palette is portable;
- `palette_set` stages one entry and `palette_use` stages all 16;
- staged changes are applied atomically on the next `flip`, or on successful
  final publication for a one-shot program;
- changing a palette recolors existing indices; it does not rewrite pixels;
- `palette_get` returns the staged value if that entry has changed, otherwise the
  active value;
- `palette_reset` restores the cartridge's declared initial palette, not an
  undocumented hardware palette.

For the current language, `palette_use` should require a literal resource path,
just like `music_play`. The file is validated and preloaded before native code
runs. A minimal proposed file is exactly 32 bytes: 16 big-endian `u16` values
whose high nibble is zero. A future palette handle or fixed array can replace the
literal-path surface syntax without changing the logical behavior.

Calls to `palette_set` can also construct a full dynamic palette one entry at a
time. Because publication occurs at `flip`, the viewer still sees one atomic
palette change. This avoids adding a 16-argument special call before MIGA Lua has
arrays.

The response-profile LUT planned by the graphics specification remains
underneath this API when that architecture is implemented:

```text
cartridge RGB12 -> selected response LUT -> AGA RGB24 register value
```

Palette operations must not expose AGA register numbers, Copper timing or RGB24
hardware encoding. Per-scanline Copper palette changes are explicitly outside
the minimum.

### Color-response selection

```lua
color_response(RESPONSE_VIOLET_DRIVE)
```

`color_response(profile)` is a declarative intrinsic, not a mutable draw-state
call. Its argument must be one literal symbol from the table below. The compiler
extracts the selected key and version into cartridge metadata, and the host
loads or constructs the matching LUT before native execution. Omitting the call
selects `RESPONSE_NEUTRAL`. Dynamic, conditional, repeated or game-time profile
changes are rejected in the first API.

| Stable API key | LUT documented by the original study |
| --- | --- |
| `RESPONSE_NEUTRAL` | Amiga RGB12 vanilla (1985) |
| `RESPONSE_WARM_NEGATIVE` | Kodak Professional PORTRA 400 study (2010) |
| `RESPONSE_COOL_REVERSAL` | Kodak Professional EKTACHROME E100 study (2018) |
| `RESPONSE_INSTANT_600` | Polaroid Color 600 study (1981) |
| `RESPONSE_MUTED_METROPOLIS` | Lomography LomoChrome Metropolis study (2019) |
| `RESPONSE_PANCHRO_MONO` | ILFORD HP5 PLUS study (1989) |
| `RESPONSE_NTSC_1953` | NTSC 1953 colorimetry |
| `RESPONSE_PAL_SECAM_625` | 625-line PAL/SECAM colorimetry (1967) |
| `RESPONSE_OSKM_1960` | Soviet OSKM reconstruction (1960) |
| `RESPONSE_DEUTAN_2009` | Machado deutan simulation (2009) |
| `RESPONSE_PROTAN_2009` | Machado protan simulation (2009) |
| `RESPONSE_VIOLET_DRIVE` | Mega Drive-inspired midtone-purple response (1988) |

The keys, rather than table positions or public display names, are the stable
source contract. Brand names remain provenance for the studies and do not have
to become UI labels. `RESPONSE_DEUTAN_2009` and `RESPONSE_PROTAN_2009` are
primarily review tools, but remain selectable so diagnostic output can exactly
match the editor preview.

Palette entries remain logical RGB12 under every response. A changed entry costs
one LUT lookup before its RGB24 AGA value is cached; drawing pixels and blitting
indices incur no response lookup. A future raster-palette API must resolve its
colors while building a bounded, double-buffered Copper list. The Copper itself
never reads the response LUT. Intensive raster effects are dominated by the two
register writes required for each full RGB24 color, list size, sequential update
timing and Chip-RAM DMA contention rather than by this indirection.

## Off-screen surfaces and blitting

### Recommended public model

Off-screen drawing is useful and feasible, but it should be the second API slice
because it requires a new value type, ownership rules and preflight budgeting.

```lua
local stamp:surface = surface_new(64, 64)

target(stamp)
cls(0)
tri(0, 63, 32, 0, 63, 63, 12)

target(PIXEL)
blit_key(stamp, 0, 0, 64, 64, 96, 96, 0)
flip()
```

`surface` is an opaque typed handle. `PLANAR` and `PIXEL` should ultimately be
valid predefined surface handles, although only the two screen handles
participate in display composition.

For the first implementation:

- width and height are literal, positive values no greater than 256;
- all `surface_new` sites are discovered by the compiler and allocated during
  preparation, before the worker starts;
- `surface_new` is accepted only at statically single-execution initialization
  sites; placing it in a loop or conditional is a compile-time error;
- a surface lives until the program ends; there is no `surface_free` and no GC;
- the cartridge has an explicit total scratch-surface budget and preflight fails
  cleanly if it cannot be met;
- the surface contains 4-bit indexed pixels, but packed, byte and planar storage
  are implementation details;
- `surface_new` initializes all pixels to 0;
- scratch palettes do not exist: blitting copies indices, and the destination
  palette determines their displayed colors.

The call looks like allocation to the author, but is a declarative resource from
the runtime's point of view. If making a preparation-time call inside `main`
would be too surprising, the same model can be expressed later as a cartridge
resource declaration. What should be avoided is a call that may allocate Chip
RAM unpredictably in the middle of a frame.

### Blit contract

```lua
blit(source, sx, sy, width, height, dx, dy)
blit_key(source, sx, sy, width, height, dx, dy, transparent)
```

The destination is the current target. The first form copies every index. The
second skips source pixels equal to `transparent`. Source and destination
rectangles are clipped without changing their relative alignment. A zero-sized
rectangle is a no-op. Overlapping copies have snapshot/`memmove` semantics.
There is no scale, rotation, tint or flip flag in the minimum.

`PLANAR` and `PIXEL`, when used as blit sources, mean their current working
images, not whichever display buffer the video hardware is scanning. Like
`pget`, a blit observes all earlier writes to its source; the implementation must
flush or wait for queued planar work when required.

The signatures exceed the current three-scalar native-call limit, but that is
not a language limitation: `line` and `tri` already lower one source call into
ordered ABI operations. Blits can similarly lower to a small command builder or
to a pointer to owner-owned command storage.

Expected implementation paths are:

| Source -> destination | Initial path | Optimization opportunity |
| --- | --- | --- |
| CPU-indexed -> CPU-indexed | Row copy or keyed CPU loop | 68020 aligned copy, dirty rectangles |
| Planar -> planar | Blitter copy with clipping/masks | Shifted and descending overlap cases |
| CPU-indexed -> planar | Correct conversion fallback | Cached planar form or bounded region C2P |
| Planar -> CPU-indexed | Four-plane CPU read fallback | Avoid unless readback workloads justify optimization |

The API should promise pixels, clipping and ordering, not equal speed for every
pair. A capability matrix or profiler can identify slow cross-representation
copies without changing results.

### Why not `mem_alloc`, `peek`, `poke` and raw `memcpy`

A raw byte allocator does not by itself create something drawable: the runtime
would still need width, height, stride, pixel format, memory class, alignment and
ownership. Letting a program invent those values would undermine validation and
make optimized backends unsafe.

If byte buffers become necessary for procedural data later, they should use a
separate opaque `buffer` handle with bounds-checked read, write, fill and copy
operations. Converting a buffer into a surface should be explicit and validated.
Even then, offsets should be virtual buffer offsets, never native pointers. This
is not required for the minimal graphics API.

## Music API completion

The proposed complete minimum is:

```lua
music_play("SYS:mods/theme.mod")
music_volume(48)
music_pause()
music_resume()
local row:i32 = music_position()
music_stop()
```

The existing `music_mute(mask)` remains useful for diagnostics and channel
effects, but it is not a substitute for master volume.

Recommended semantics:

- only one MOD is active;
- `music_play` starts or restarts the literal, preloaded resource from the
  beginning, leaves the paused state, and loops at the end as the current replay
  does;
- `music_pause` is idempotent, silences output and freezes replay position;
- `music_resume` is idempotent and continues from the frozen position;
- `music_stop` silences DMA and discards the replay position; resume after stop
  is a no-op;
- `music_volume(level)` uses Paula/ptplayer's native 0..64 range, where 0 is
  silent and 64 is full scale. A literal above 64 is a compile-time diagnostic;
  a dynamic value above 64 leaves the volume unchanged rather than wrapping;
- master volume is remembered across pause and across a subsequent
  `music_play` during the same run;
- `music_position` retains its current `order * 64 + next_row` contract and is
  stable while paused; it returns -1 when stopped;
- automatic stop on normal completion, ESC and faults remains mandatory.

This addition is low risk because the integrated ptplayer 6.4 already exports a
0..64 master-volume function and a replay-enable flag. There is one important
detail: merely disabling replay ticks may leave Paula repeating the current
sample. A correct pause command must both freeze the player and force effective
channel volume to zero, then restore the remembered master volume on resume.
Pending delayed DMA operations and stop/pause ordering need the same interrupt
tests already used by the music host.

PICO-8 and TIC-80 are useful references for starting/stopping and selecting
music, but neither reference API maps exactly to this need. PICO-8 exposes
pattern selection, fade duration and channel reservation; TIC-80 exposes track,
frame, row, loop, sustain, tempo and speed. MIGA-80 should initially keep the
simpler MOD-file abstraction instead of exposing tracker internals prematurely.
One-shot playback and fades can be added later only if a concrete cartridge
needs them; they should not complicate the first ABI.
See the official [PICO-8 audio API](https://www.lexaloffle.com/dl/docs/pico-8_manual.html)
and TIC-80 [`music`](https://github.com/nesbox/TIC-80/wiki/music).

## Feasibility and risk

| Feature | Feasibility | Main work or risk |
| --- | --- | --- |
| `pget` on `PIXEL` | High | Compiler return value and bounds tests |
| `pget` on `PLANAR` | High | Flush/wait semantics and performance traps |
| `rect` / `rectb` | High | Exact half-open clipping and two optimized backends |
| One-entry palette update | High | Logical RGB12 validation and safe frame publication |
| Whole-palette resource | High | Resource scan, 32-byte format validation and atomic staging |
| `color_response` selection | High | Compiler metadata, profile/version validation and LUT preparation before execution |
| Music master volume | High | Small owner command around existing ptplayer support |
| Music pause/resume | High | Silence Paula as well as freezing ticks; interrupt races |
| Scratch surface handles | Medium | New compiler type, resource discovery, quota and ownership |
| Opaque/keyed blit | Medium | Clipping, overlap and backend equivalence |
| Fast cross-format blit | Medium to low | Region conversion, caching and Chip-RAM contention |
| Raw arbitrary memory API | Technically possible, not recommended | Freezes unsafe layout/address assumptions and expands validation surface |

The largest feasibility risk is memory budgeting, not basic rasterization. A
256 x 256 scratch surface costs 64 KiB in the current byte-per-pixel form or
32 KiB packed. The current runtime already uses a 64 KiB chunky source and two
eight-plane display buffers totaling 128 KiB, before triangle scratch and other
resources. Therefore the API must not imply that several full-screen surfaces
are always available. A proposed default scratch quota such as 32 KiB is only a
starting hypothesis; it must be selected from measured stock-machine preflight
profiles and largest-block data.

## ABI and implementation implications

The private native ABI currently appends one handler pointer per service to a
100-byte drawing context, with at most three scalar and two address arguments.
It is possible to keep appending handlers while preserving the frozen prefix,
but surfaces and blits are the point where a versioned graphics service table or
command dispatcher becomes cleaner than one context field per public call.

A practical implementation outline is:

1. Keep direct fast paths for common `PIXEL` `pset`/`pget` operations.
2. Route ordered planar drawing and read barriers through the existing owner
   queue.
3. Store palette changes in owner state, resolve dirty RGB12 entries through the
   selected LUT, and apply cached RGB24 values only during safe publication.
4. Represent public handles as validated small IDs, never pointers.
5. Preallocate surface descriptors and backing stores before native execution.
6. Lower multi-argument source calls to one bounded command while retaining
   source-order evaluation and the existing register-clobber contract.
7. Mark only affected `PIXEL` regions dirty when the partial C2P architecture is
   available; correctness must not depend on that optimization.

## Recommended delivery sequence

### Slice A: complete the existing screen API

- add `pget` with explicit barrier behavior;
- add `rect` and `rectb`;
- add `palette_set`, `palette_get`, `palette_use`, `palette_reset`, and the
  declarative `color_response` selection;
- add `music_pause`, `music_resume`, and `music_volume`;
- retain `layer` for now, documenting whether it will become an alias of
  `target`.

This slice requires no general allocation model and delivers a coherent minimal
API quickly.

### Slice B: off-screen composition

- add the opaque `surface` type and preparation-time `surface_new`;
- generalize target selection;
- add opaque and keyed blits;
- publish scratch-memory use in preflight and runtime reports;
- start with correct CPU/reference fallbacks, then optimize measured hot paths.

### Slice C: ergonomics before assets

- add `clip`/`clip_reset`;
- evaluate `camera` only alongside the already planned independent scrolling;
- consider a bounds-checked byte-buffer API only after a concrete non-graphics
  use case exists.

Sprites and tile maps should then be designed on top of surfaces, palettes and
blits rather than forcing those foundational contracts to match an unknown asset
architecture.

## Acceptance tests

The API should not be considered complete without these checks:

- differential images for every primitive on `PLANAR`, `PIXEL` and scratch
  targets, including clipping, color 0, degenerate and extreme coordinates;
- `pget` immediately after queued planar lines, clears, triangles and blits;
- palette updates before/after drawing, multiple updates before one `flip`,
  resets, invalid RGB12, and overlay index-0 transparency;
- every `color_response` key, the neutral default, rejected dynamic or duplicate
  selections, profile/version mismatch, and editor/runtime LUT identity;
- exact whole-palette resource validation for truncation, trailing bytes and
  invalid high bits;
- blits clipped on every edge, self-overlap in every direction, transparent-key
  copies and all supported source/destination pairs;
- allocation failure with unchanged editor/project state and an exact memory
  report;
- pause during ordinary playback and during delayed DMA restart, stable position
  while paused, volume 0/64, play while paused, stop while paused, ESC and fault
  cleanup;
- golden equivalence between host reference, Musashi native execution, FS-UAE
  AGA output and eventually physical A1200 output;
- performance reports with active display and four-channel audio DMA, separating
  draw, barrier, blit, C2P, blitter wait and publication time.

## Final recommendation

Freeze the screen-level core only after `pget` and palette semantics are added.
Those choices affect every later sprite, tile and object system. Add rectangles
because they are inexpensive and broadly useful. Complete music now because the
underlying player already contains the required controls.

Treat off-screen drawing as a first-class next step, but expose it as bounded,
opaque indexed surfaces allocated during preparation. This provides the desired
"draw into a block, then blit it" workflow without turning Amiga memory and
bitplane layout into permanent parts of the cartridge API. Defer raw buffers,
sprites and tiles until real use cases can shape them.

## Evidence reviewed

External behavior was checked against the official
[PICO-8 0.2.7 manual](https://www.lexaloffle.com/dl/docs/pico-8_manual.html),
the TIC-80 wiki pages for [`pix`](https://github.com/nesbox/TIC-80/wiki/pix),
[`vbank`](https://github.com/nesbox/TIC-80/wiki/vbank),
[`memcpy`](https://github.com/nesbox/TIC-80/wiki/memcpy),
[RAM/VRAM](https://github.com/nesbox/TIC-80/wiki/ram) and
[`music`](https://github.com/nesbox/TIC-80/wiki/music), and the official
[Lua 5.4 manual](https://www.lua.org/manual/5.4/manual.html).

Repository conclusions are grounded in the current
[drawing implementation](../src/graphics/drawing.c),
[drawing host](../src/demo/drawing_host.c),
[native ABI](MIGA-Lua-native-ABI-v0.md),
[graphics specification](MIGA-80-specification-and-roadmap.md),
[music host](../src/demo/music_host.c), and bundled
[ptplayer documentation](../third_party/ptplayer/ptplayer.readme).
