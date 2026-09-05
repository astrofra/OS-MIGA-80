# MIGA-80 Product Requirements, Technical Specification, and Development Roadmap

| Field | Value |
| --- | --- |
| Document status | Working specification, revision 0.3 |
| Date | 2026-09-03 |
| Target release | MIGA-80 1.0 |
| Primary hardware | Stock PAL Amiga 1200, 68EC020, AGA, 2 MiB Chip RAM |
| Implementation | C99 with narrowly scoped 68020 assembly |
| Build model | Cross-compiled with GCC plus native host tests and an embedded 68EC020 generated-code runner |

## 1. Purpose

This document defines MIGA-80, a small fantasy-console-style development environment for a stock Amiga 1200. It turns the machine into a focused place for creating and running games and demos, with deliberate limits inspired by PICO-8.

MIGA-80 provides:

- an integrated source-code editor;
- an integrated sprite editor;
- ProTracker module import and playback;
- a strongly typed, Lua-like game language compiled on the Amiga directly to native 68020 code;
- a 256 × 256 hybrid virtual GPU with a native planar layer, a configurable 4-bit chunky viewport, and a virtual object layer transparently mapped onto AGA;
- project and cartridge storage through AmigaDOS;
- a reversible exclusive runtime mode that freezes AmigaOS task scheduling while a game runs;
- both bootable-floppy and hard-disk launch paths.

This specification is intended to be detailed enough to guide architecture, implementation, validation, and scope decisions. Numeric budgets are initial engineering targets. Phase 0 must measure them on a real stock A1200 before they become release commitments.

## 2. Normative language

The terms **MUST**, **MUST NOT**, **SHOULD**, **SHOULD NOT**, and **MAY** describe requirement priority:

- **MUST** is required for MIGA-80 1.0.
- **SHOULD** is expected unless Phase 0 evidence shows that the stock machine cannot support it reliably.
- **MAY** is optional or belongs to a later release.

## 3. Product definition

### 3.1 What MIGA-80 is

MIGA-80 is a fantasy computer and integrated development environment hosted by AmigaOS while editing, with an exclusive hardware-oriented execution mode while running a cartridge. It replaces the normal AmigaOS user experience from the user's perspective, but it does not replace Kickstart, Exec, AmigaDOS, or the installed filesystem implementation in version 1.0.

The product may be called a mini-OS because it supplies its own shell, editors, language, runtime, graphics model, and asset format. Technically, version 1.0 is a single AmigaOS executable with two operating personalities:

1. **Hosted mode:** AmigaOS continues multitasking and provides filesystems, devices, memory allocation, and system-friendly display/input services.
2. **Exclusive runtime mode:** the MIGA-80 task prevents task rescheduling, takes ownership of the display, blitter, input path, and Paula audio resources needed by the cartridge, and makes no filesystem or other blocking OS calls until it restores the hosted environment.

This interpretation resolves an otherwise irreconcilable requirement: a completely independent bare-metal OS cannot use AmigaOS libraries to read OFS and FFS volumes. A future bare-metal edition would need its own filesystem and device drivers and is outside the 1.0 scope.

MIGA-80 MUST reuse AmigaOS and the selected lightweight C runtime wherever that improves reliability without weakening the fantasy-machine contract. In hosted mode, ordinary C99 allocation and stream APIs such as `malloc()`, `free()`, `fopen()`, `fread()`, `fwrite()`, and `fclose()` are valid implementation choices when their runtime is compatible with the target systems. Direct Exec, DOS, graphics, device, and resource APIs remain appropriate where MIGA-80 needs Amiga-specific capabilities such as volume enumeration, Chip RAM, screen ownership, or error details. Reimplementing a general allocator, filesystem, device layer, or window system is not a goal.

### 3.2 PICO-8 principles adopted

MIGA-80 adopts the following general principles rather than attempting API compatibility:

- one coherent machine with fixed capabilities;
- immediate edit–compile–run–stop feedback;
- source, sprites, maps, palette, and music collected into a shareable cartridge;
- intentionally small resource limits that encourage complete projects;
- a compact built-in API rather than a general-purpose operating system API;
- deterministic behavior and visible resource meters;
- creation on the target machine, without requiring a modern host after installation;
- easy escape from a running program back to the editor.
- maximum useful leverage of AmigaOS during creation, with direct hardware access reserved for capabilities and performance that the runtime actually needs.

### 3.3 Product values

When requirements compete, decisions SHOULD favor, in order:

1. safe restoration of AmigaOS and user data;
2. correct operation on a stock 2 MiB A1200;
3. a short and predictable creation loop;
4. deterministic cartridge behavior;
5. a small disk and memory footprint;
6. peak graphics performance;
7. optional convenience features.

### 3.4 Native-hardware leverage policy

MIGA-80 SHOULD use the machine's existing strengths instead of simulating in software what the A1200 already does well. The intended split is:

- Exec, AmigaDOS, the C runtime, Intuition/graphics services, device APIs, and resource arbitration while editing;
- the 68020's 32-bit operations for generated game logic, flexible pixel drawing, fixed-point math, and the parts of C2P it performs best;
- AGA bitplane DMA and 4+4 dual playfields for a native planar base and a transparent pixel overlay, with palette banks, fetch modes, and the Copper for composition;
- the blitter for native planar tiles, fills, copies, masks, incremental scroll updates, object fallback, or C2P stages where measurement shows a gain;
- AGA hardware sprites, attached pairs, and measured Copper-assisted multiplexing behind a virtual object scheduler;
- Paula DMA for the four ProTracker channels;
- CIA and standard ports for precise timing and input when safely owned;
- per-layer dirty tracking and pointer/scroll changes so unchanged pixels are neither redrawn nor reconverted.

The fantasy-machine API remains stable even when a backend optimization changes. A feature is not considered “hardware-accelerated” until it is faster on a real stock A1200 with active display DMA.

The adopted graphics direction is defined in [MIGA-80 Graphics Architecture — Performance-Oriented Design Notes](./MIGA-80-graphics-architecture-notes.md): do not regenerate pixels when AGA can move, compose, or display their existing representation. Exact viewport sizes, scroll margins, object limits, and CPU/blitter division remain Phase 0 measurement decisions.

## 4. Goals and non-goals

### 4.1 Version 1.0 goals

- Boot or launch on a stock A1200 with no accelerator, Fast RAM, hard disk, or FPU.
- Fit a useful MIGA-80 distribution, including one example cartridge, on one standard Amiga DD floppy.
- Launch the same core program from an AmigaOS hard disk.
- Keep AmigaOS alive and schedulable throughout editing.
- Enter and leave exclusive runtime mode repeatedly without rebooting or damaging the prior display, audio, input, or filesystem state.
- Compile strongly typed MIGA Lua source directly to bounded native 68020 code on the Amiga itself.
- Render a 256 × 256 three-layer virtual display: a full-screen native planar layer, a configurable 4-bit chunky viewport, and a virtual object layer.
- Map the planar and pixel layers onto AGA's 4+4-bitplane dual playfields, using 16 opaque base colors and 15 opaque overlay colors selected from a deliberately restricted 12-bit RGB space of 4,096 logical colors, then render them through a cartridge-selected photographic, historical-video, color-vision, or console-inspired response profile into AGA's 24-bit hardware palette.
- Schedule virtual objects onto AGA sprites when possible and use a deterministic, budgeted fallback when hardware sprite capacity is exceeded.
- Import and play conventional four-channel, 31-sample ProTracker modules.
- Read projects and assets from mounted OFS and FFS volumes through AmigaDOS.
- Offer a usable code editor and sprite editor at the virtual resolution.
- Provide deterministic failure handling for compile errors, runtime errors, invalid files, and insufficient resources.

### 4.2 Explicit non-goals for version 1.0

- Replacing Kickstart or implementing a bare-metal kernel.
- Parsing raw OFS or FFS structures directly.
- Accessing files while a cartridge is in exclusive runtime mode.
- Supporting OCS/ECS machines, 68000/68010 CPUs, RTG graphics, or non-Amiga hardware.
- Requiring or optimizing primarily for accelerators, Fast RAM, an FPU, or a CD32.
- Full semantic or library compatibility with standard Lua, C, or PICO-8 Lua; MIGA Lua deliberately preserves familiar Lua syntax while changing its type, number, table, allocation, and execution models.
- Generating 68000-compatible game code or supporting A500-class machines; the native compiler targets the stock A1200's 68EC020/68020 instruction set.
- Dynamic linking, user-loadable native plug-ins, hand-written assembly in projects, or externally supplied machine code in cartridges.
- A tracker or sample editor; version 1.0 imports and previews `.mod` files only.
- A general bitmap-paint package, animation package, or full map editor.
- Networking, printing, serial transfer, MIDI, or source control on the Amiga.
- Standalone executable export for individual cartridges.
- NTSC certification for the 256-line display in version 1.0.

## 5. Assumptions and decisions to validate

| ID | Assumption or proposed decision | Rationale | Validation or fallback |
| --- | --- | --- | --- |
| A-01 | PAL A1200 is the certified 1.0 target. | A non-interlaced 256-line workspace naturally fits PAL and provides a stable 50 Hz cadence. | Detect the video standard before opening the workspace. On NTSC, show a clear unsupported-mode message in a safe AmigaOS screen. A 256 × 200 compatibility mode MAY follow later. |
| A-02 | Kickstart/AmigaOS 3.0 and 3.1 are the initial compatibility targets. | These are the normal stock A1200 environments and expose the required classic APIs. | Test both on emulator and original hardware. Newer 3.x releases are best-effort until separately certified. |
| A-03 | MIGA-80 is an AmigaOS-hosted environment, not a bare-metal kernel. | OFS/FFS access through `dos.library`, safe HD launch, and continued AmigaOS operation during editing require a hosted process. | If literal bare-metal operation becomes mandatory, split it into a separate product track with its own loader, filesystem, input, and device work. |
| A-04 | A standard DD ADF is 901,120 bytes raw, conventionally called 880 KiB. | This is the distribution ceiling and matches a stock internal drive. | The release pipeline MUST build and boot-test the exact image rather than relying only on summed file sizes. |
| A-05 | The safe filesystem payload budget is initially 800 KiB. | OFS overhead, boot metadata, and future headroom make the raw ADF size an unsafe payload target. | Generate both OFS and FFS candidate images in Phase 0, select one boot format, and replace this estimate with measured free-block budgets. |
| A-06 | The virtual palette remains a 12-bit RGB index space even though AGA palette entries are 24-bit. A versioned response-profile LUT maps every logical `0xRGB` value to one `0xRRGGBB` hardware value. | The 4,096-color authoring space preserves the intended fantasy-machine constraint, while AGA's precision can express a film- or video-derived response without increasing cartridge color precision. | Generate and validate all 4,096 mappings offline from documented response data; retain direct nibble replication only as a neutral reference and diagnostic fallback. |
| A-07 | MIGA Lua compiles ahead of time directly to native 68020 machine code in RAM. | Strong static types, fixed layouts, direct API calls, and removal of interpreter dispatch maximize the limited stock CPU. | Phase 0 must prove a minimal on-target emitter, cache synchronization, bounded-loop instrumentation, compile time, code size, and safe abort. A bytecode VM would be a fallback redesign, not a parallel 1.0 runtime. |
| A-08 | Baseline simulation is 25 updates per second with 50 Hz video synchronization; 50 updates per second is an optional cartridge mode. | Native planar work, viewport C2P, object scheduling, compiled game logic, and audio must share a stock 68EC020 and Chip RAM. | Phase 0 benchmarks determine the certified workload for each rate. Frame rate MUST never change adaptively without the cartridge knowing. |
| A-09 | Runtime file I/O is prohibited. | DOS file calls may wait, which breaks the scheduling freeze and conflicts with direct hardware ownership. | Preload the compiled cartridge and all assets before takeover. Save only after restoration. |
| A-10 | Cartridge sprites are virtual objects rather than physical AGA sprite channels. | A stable object API can exploit direct sprites, attached pairs, vertical multiplexing, and planar fallback without making cartridges depend on channel allocation. | Phase 0 MUST freeze object count, dimensions, palettes, priority rules, multiplex limits, and fallback cost. If multiplexing is not reliable, direct sprites plus deterministic fallback remain valid. |
| A-11 | A cartridge is capped initially at 384 KiB uncompressed residency and 256 KiB packed on disk. | This leaves room for the environment, buffers, AmigaOS, and a useful example on one floppy. | Phase 0 and the first vertical-slice game set final caps. The UI MUST display the measured packed and resident sizes. |
| A-12 | One process contains the shell, editors, native compiler, and runtime. | It simplifies boot, disk swapping, generated-code ownership, and restoration. | Split into overlays only if disk or resident-code measurements fail their gates. |
| A-13 | Hosted code uses the C runtime and AmigaOS services instead of duplicating them. | The purpose is to exploit the A1200, not to write another general-purpose OS. | Validate the chosen libc's size, ABI, DOS error behavior, and Kickstart 3.0/3.1 compatibility. Replace only individual facilities whose measured cost or semantics fail the product constraints. |
| A-14 | “Fits on one floppy” is initially interpreted as a self-starting AmigaDOS distribution, not merely a file small enough to copy to floppy. | It provides the most console-like experience and a clean low-memory boot. | If only file-size fit is required, SYS-002 becomes optional and the boot-component licensing gate can be removed; all other disk-size limits remain. |
| A-15 | The 1.0 graphics model is a three-layer virtual GPU rather than two full-screen chunky buffers. | A native planar base, a smaller optional chunky viewport, and virtual hardware-assisted objects map more directly to AGA and avoid unnecessary C2P. | Phase 0 MUST benchmark the complete pipeline on real hardware. The public semantics remain provisional until the graphics freeze gate; the project MUST revise limits rather than silently emulate an unaffordable model. |
| A-16 | Generated 68020 code is exercised locally through a pinned Musashi 68EC020 runner before UAE or hardware integration. | Most compiler and ABI failures can then be reproduced in a sub-second native test without building or launching an Amiga image. | Musashi results establish functional confidence only. A curated corpus SHOULD later run under Moira as an independent oracle, while UAE and real A1200 tests remain mandatory for OS, chipset, cache, and performance behavior. |
| A-17 | An informal stock-A1200 Chip-RAM write estimate near 6 MB/s is treated as a bandwidth warning, not as an engineering budget. | Its loop, access width, alignment, display/DMA state, and unit convention are unknown, and write-only throughput does not describe C2P's mixed traffic. | Phase 0 MUST reproduce aligned byte/word/long read, write, and mixed-access tests on real hardware with display blanked and active and with representative DMA states. Generated disassembly and raw E-Clock/raster distributions must accompany the result. |
| A-18 | Available Fast RAM enables an optional transparent acceleration tier, never a different cartridge contract. | Moving CPU code, stacks, game state, dictionaries, chunky sources, and CPU-only scratch out of contended Chip RAM can free 68020 and chipset cycles while AGA and the blitter retain DMA access to Chip RAM. | Phase 0 MUST compare stock and Fast-assisted placement on real hardware. The allocator records every memory domain, all DMA-visible data remains in Chip RAM, and release certification still uses the stock 2 MiB configuration. |
| A-19 | The eleven complete 4,096-entry color-response tables MUST NOT be compiled as literal arrays into the MIGA-80 executable. | RGB24 storage alone would consume 135,168 bytes (132 KiB), or 180,224 bytes (176 KiB) as runtime-aligned 32-bit entries, before executable-format or alignment overhead. This is disproportionate to the floppy and binary budgets. | Phase 0 compares two integer-only sources for the selected 16 KiB direct-index table: independently compressed canonical LUT blocks, or one-time reconstruction from compact fixed-point profile descriptors. Neither route may perform color calculations after takeover. |

## 6. Target platform and compatibility contract

### 6.1 Required hardware

- Commodore Amiga 1200 or a cycle-compatible equivalent.
- 68EC020/68020-compatible CPU at the stock clock rate or faster.
- AGA chipset.
- 2 MiB Chip RAM; no other memory is assumed.
- PAL video output for the certified release.
- Internal or compatible Amiga floppy drive for floppy workflows.
- Built-in keyboard.
- At least one standard joystick or mouse port.

### 6.2 Optional hardware

- IDE hard disk or CompactFlash exposed as an AmigaDOS volume.
- Additional Fast RAM.
- Second joystick.
- Accelerator.

Optional hardware MUST NOT be required by a version 1.0 cartridge. When Fast RAM is present, MIGA-80 MAY select the `fast_assisted` memory tier without changing fantasy-machine semantics, timing rules, save data, or cartridge compatibility. It MUST preserve the stock-memory execution path and MUST place all DMA-visible data in Chip RAM.

### 6.3 Startup checks

Before opening its workspace, MIGA-80 MUST check:

- CPU capability;
- AGA availability;
- PAL timing;
- required libraries and minimum versions;
- total and largest available Chip RAM blocks;
- whether critical display and audio resources can be acquired;
- whether the launch volume is readable.

Failure MUST return to AmigaOS with a plain explanation. MIGA-80 MUST NOT attempt hardware takeover after a failed preflight.

## 7. Operating model

### 7.1 State model

```text
AmigaOS boot or Workbench/CLI launch
                 |
                 v
      HOSTED INITIALIZATION
                 |
                 v
       HOSTED EDITOR SHELL <---------+
          |             ^            |
          | Run         | Stop/error |
          v             |            |
       PREFLIGHT -------+            |
          | success                  |
          v                          |
    EXCLUSIVE TAKEOVER                |
          |                          |
          v                          |
      CARTRIDGE RUNTIME               |
          |                          |
          v                          |
       RESTORATION ------------------+
```

Every acquisition MUST have a recorded matching release action. Takeover is a transaction: partial failure unwinds only the resources already acquired, in reverse order.

### 7.2 Hosted editor mode

In hosted mode:

- AmigaOS multitasking MUST remain enabled.
- C99 `malloc()`/`free()` and `fopen()`-family functions MAY be used for editor, compiler, and ordinary project I/O, subject to bounded allocations and checked errors.
- Normal disk insertion, validation, and file operations MUST be handled by AmigaDOS.
- The editor SHOULD use a custom Intuition screen or an OS-managed `View` compatible with the 256 × 256 visual design.
- Keyboard and mouse events MUST use OS input facilities.
- Music preview SHOULD use an OS-cooperative audio path and must handle unavailable Paula channels gracefully.
- Direct custom-chip access MUST be limited to operations explicitly coordinated with the appropriate OS resource or library.
- The user MUST be able to leave MIGA-80 and return to Workbench or CLI without rebooting.

### 7.3 Exclusive runtime mode

In exclusive runtime mode:

- The cartridge and all referenced data MUST already be resident.
- All MIGA-80 project/import streams with pending work MUST be flushed and closed before takeover. Inherited CLI/Workbench process handles may remain open but MUST stay untouched until restoration.
- No operation that may call `Wait()` is allowed.
- The MIGA-80 task MUST prevent normal task rescheduling for the duration of the run.
- Hardware interrupts required for MIGA-80 input, video timing, and music MUST continue to operate.
- `Disable()` MUST NOT cover the gameplay session. Official Exec guidance warns that long disabled sections disrupt vital system activity; it may only protect a measured, very short transition when necessary.
- MIGA-80 MUST coordinate ownership of the blitter, display, CIA timer if used, and Paula channels before programming them directly.
- An emergency stop input MUST remain available even when generated user code loops forever. The compiler MUST insert stop and execution-budget checks at every backward control-flow edge and other bounded safe points.
- A generated-code fault detected by a guard, execution-budget exhaustion, or user stop MUST enter the same restoration path.
- The runtime MUST perform no dynamic memory allocation after takeover.

### 7.4 Proposed takeover sequence

The exact register and library sequence is a Phase 0 deliverable, not something to improvise late in development. The initial design is:

1. Compile the cartridge and validate all resources.
2. Validate generated-code bounds, relocations, guarded control-flow metadata, stack requirements, and calls against the immutable MIGA-80 native ABI jump table.
3. Flush the generated code range from the 68020 instruction cache with the appropriate Exec cache-control function before it can execute.
4. Allocate and pin all remaining runtime memory.
5. Finish, flush, and close MIGA-80 project/import file operations; stop hosted audio preview.
6. Save the active `View`, display configuration, palette, DMA/interrupt state that MIGA-80 will modify, and input/audio ownership state.
7. Install or activate preallocated MIGA-80 interrupt handlers through the appropriate Exec/CIA resource interfaces.
8. Obtain exclusive blitter access and wait for any prior blit to finish.
9. Blank or detach the OS view using `graphics.library`, with required frame waits completed before scheduling is forbidden.
10. Call `Forbid()` once the runtime path is guaranteed not to wait.
11. In a short critical transition, install the MIGA-80 Copper list, bitplane pointers, palette, DMA, and owned interrupt sources.
12. Run the fixed-step native callback loop, synchronized by an interrupt-set flag or a nonblocking beam/tick mechanism.

Restoration reverses those steps: stop MIGA-80 DMA and interrupt sources, restore the saved display and owned resources, release the blitter, call `Permit()`, restart hosted input/audio as needed, and redraw the editor. The implementation MUST track nesting and ownership explicitly; it MUST never issue an unmatched `Permit()`, `Enable()`, `DisownBlitter()`, or resource release.

### 7.5 Restoration acceptance test

A stock A1200 MUST survive at least 1,000 automated or operator-assisted run/stop cycles in one session while alternating graphics, input, and music test cartridges. After each stop:

- the editor display is intact;
- keyboard and mouse input work;
- OS task scheduling resumes;
- the system clock continues sensibly;
- disks and hard-disk volumes remain usable;
- no Chip RAM or OS resource leak is detected;
- no stale audio DMA or stuck note remains;
- the prior Workbench/CLI state remains usable after MIGA-80 exits.

## 8. User experience

### 8.1 Main shell

The main shell MUST expose these workspaces without requiring AmigaOS UI interaction:

- **Code**
- **Sprites**
- **Music**
- **Files / Cartridges**
- **Run**
- **Help / System**

The shell SHOULD use consistent function-key shortcuts and show them on screen. Exact bindings are configurable during usability testing, but there MUST always be dedicated actions for run, stop, save, switch editor, and help.

### 8.2 Visual workspace

The MIGA-80 UI SHOULD use the same 256 × 256 logical surface as cartridges so that asset previews are exact. A compact 6 × 8 or similarly legible bitmap font SHOULD provide at least 40 source columns. The code editor MUST support horizontal scrolling because the logical screen is intentionally narrow.

### 8.3 Immediate feedback

- A successful compile SHOULD start the cartridge within two seconds when no disk access is needed.
- Compile diagnostics MUST return focus to the first error.
- Stopping a cartridge MUST return to the prior editor and cursor location.
- Runtime errors MUST report the source file, line, function, and concise cause whenever debug metadata is present.
- Memory, source, generated-code, graphics, music, and packed-cartridge budgets MUST be visible from the shell.

## 9. Functional requirements

### 9.1 Boot, launch, and shutdown

| ID | Requirement |
| --- | --- |
| SYS-001 | The full release image MUST fit on one standard 880 KiB Amiga DD floppy. |
| SYS-002 | The floppy edition MUST boot through a minimal AmigaDOS startup sequence and enter MIGA-80 without opening Workbench. |
| SYS-003 | The same MIGA-80 core executable MUST be launchable from CLI and Workbench on a hard disk. |
| SYS-004 | After a floppy boot completes, the executable and all mandatory UI resources MUST be resident so that the boot disk can be exchanged for a project disk. |
| SYS-005 | Quit MUST restore the original AmigaOS display, input, audio, scheduling, current directory, and error status as far as the public APIs permit. |
| SYS-006 | A failed startup MUST release every resource acquired by that startup. |
| SYS-007 | The release MUST include a version screen containing build ID, cartridge format version, MIGA Lua language version, native ABI version, compiler version, and target profile. |

### 9.2 Code editor

| ID | Requirement |
| --- | --- |
| CODE-001 | The editor MUST create, open, edit, save, and save-as MIGA Lua source stored in a cartridge project. |
| CODE-002 | It MUST support cursor movement, selection, insert/delete, line operations, page movement, and configurable two- or four-column tab expansion. |
| CODE-003 | It MUST provide bounded undo/redo with a visible remaining history budget. |
| CODE-004 | It MUST provide find, find-next, go-to-line, and matching-delimiter navigation. |
| CODE-005 | It MUST display line and column numbers and indicate modified state. |
| CODE-006 | It SHOULD provide token coloring for comments, keywords, literals, identifiers, and diagnostics. |
| CODE-007 | Compiler errors MUST be selectable and navigate to their source span. |
| CODE-008 | Text MUST use a documented single-byte encoding. Version 1.0 SHOULD use printable ASCII plus tab and newline, with LF canonicalized internally. |
| CODE-009 | The editor MUST handle a source file up to the cartridge source limit without an unbounded allocation or quadratic whole-file operation on every keystroke. |
| CODE-010 | Source changes MUST never be written automatically to floppy without an explicit user preference and visible write indication. |

### 9.3 Sprite editor

| ID | Requirement |
| --- | --- |
| SPR-001 | The editor MUST edit indexed virtual-object and planar-tile pixels using the logical palette allowed by the selected asset role. Object index 0 MUST be transparent. |
| SPR-002 | It MUST provide pencil, eraser/transparent color, fill, line, rectangle, selection, move, copy, and paste. |
| SPR-003 | It MUST provide horizontal and vertical flip and 90-degree rotation for square selections. |
| SPR-004 | It MUST support at least 8 × 8, 16 × 16, and 32 × 32 logical cells. It SHOULD support hardware-friendly 16-, 32-, and 64-pixel-wide object frames with bounded height. |
| SPR-005 | It MUST show a zoomed editing grid and a 1:1 preview on both light and dark checker backgrounds. |
| SPR-006 | It MUST edit all 31 opaque logical palette entries as 12-bit RGB values, show each entry's mapped 24-bit AGA value under the selected response profile, and clearly mark overlay/object index 0 as transparent. |
| SPR-007 | Palette edits MUST update previews immediately and be undoable. |
| SPR-008 | Sprite and tile sheets MUST use the same packed asset representation consumed by the runtime or lossless, deterministic build-time transformations into planar, attached-sprite, and fallback caches. |
| SPR-009 | The editor MUST prevent a planar/overlay asset-role or palette mismatch from silently changing pixel indices. |
| SPR-010 | The editor MUST present virtual objects and tiles, not physical AGA channels. It MAY report whether an asset is eligible for direct, attached, multiplexed, or fallback rendering, but channel assignment remains a runtime concern. |
| SPR-011 | The palette editor MUST offer the eleven response profiles defined in section 11.4.1—five photographic, three historical-video, two color-vision, and one console-inspired—preview them through the same lookup table used by the runtime, and identify the selected profile and version. |
| SPR-012 | Changing a response profile MUST preserve every stored 12-bit logical color and pixel index; only the deterministic 24-bit AGA rendering changes. |

### 9.4 ProTracker import and playback

| ID | Requirement |
| --- | --- |
| AUD-001 | MIGA-80 MUST import standard four-channel, 31-sample ProTracker modules with recognized signatures such as `M.K.`, `M!K!`, and `4CHN`. |
| AUD-002 | Import MUST validate the header, song length, order table, pattern count, sample lengths, loop ranges, and total file bounds before allocating or copying bulk data. |
| AUD-003 | Unsupported signatures, effects, corrupt loops, truncated patterns, and over-budget samples MUST produce specific errors and MUST NOT destabilize the editor. |
| AUD-004 | The importer MUST preserve signed 8-bit sample data, finetune, volume, loop points, pattern data, and song order for supported files. |
| AUD-005 | The player MUST use Paula's four DMA audio channels in exclusive runtime mode; samples MUST reside in Chip RAM. |
| AUD-006 | The replay engine MUST implement the ProTracker 2.x effect subset declared by the compatibility test suite. The required 1.0 subset is effects `0`, `1`, `2`, `3`, `4`, `5`, `6`, `9`, `A`, `B`, `C`, `D`, `E1`, `E2`, `E6`, `E9`, `EA`, `EB`, `EC`, `ED`, `EE`, and `F`. |
| AUD-007 | Effects `7`, `8`, `E0`, `E3`, `E4`, `E5`, `E7`, and nonstandard variants SHOULD be implemented when behavior is verified; otherwise import MUST warn and list occurrences. |
| AUD-008 | Both speed and CIA-tempo interpretations of `Fxx` MUST match the selected compatibility reference within the limits of PAL hardware timing. |
| AUD-009 | Hosted-mode preview MUST coexist with AmigaOS through allocated audio/timer resources or report that preview is unavailable. It MUST NOT take exclusive runtime ownership merely to browse a module. |
| AUD-010 | Starting a game MUST stop hosted preview, acquire required resources, and start replay from a deterministic state. |
| AUD-011 | The API MUST provide at least `music_play`, `music_stop`, `music_position`, and per-channel mute for debugging. |
| AUD-012 | Version 1.0 does not need to edit or export `.mod` files. |

### 9.5 Storage and filesystem behavior

| ID | Requirement |
| --- | --- |
| IO-001 | In hosted mode, MIGA-80 MUST enumerate mounted volumes and directories through `dos.library`. |
| IO-002 | It MUST read files on OFS and FFS volumes as exposed by AmigaDOS; it MUST NOT depend on direct knowledge of their on-disk block formats. |
| IO-003 | It MUST open, read, seek when supported, examine, enumerate, create, write, flush, close, rename, and delete through documented AmigaOS facilities or the selected C99 runtime backed by them. Volume enumeration, disk-specific state, and detailed DOS errors MAY use `dos.library` directly; ordinary sequential file data MAY use `fopen()` and related calls. |
| IO-004 | File browsing MUST support `DF0:` and normal hard-disk volume/device names. |
| IO-005 | MIGA-80 MUST detect disk removal, insertion, validation delays, write protection, full media, name collisions, and DOS errors without losing the in-memory project. |
| IO-006 | Saves SHOULD use a temporary sibling file, flush and close it, then replace the destination only after success. If the volume lacks room for both copies, the UI MUST explain the risk and offer Save As to another volume; it MUST NOT silently overwrite the only valid copy. |
| IO-007 | No MIGA-80-owned project/import handle, lock, outstanding DOS packet, or unflushed project write may cross into exclusive runtime mode. Inherited process handles may remain idle but MUST NOT be accessed while task scheduling is frozen. |
| IO-008 | File and volume names MUST be normalized conservatively and MUST remain usable on both OFS and FFS. |
| IO-009 | Unknown files are read-only imports until their parser validates them. |
| IO-010 | A cartridge loaded from hard disk MUST behave identically to the same bytes loaded from floppy. |

### 9.6 Cartridge workflow

| ID | Requirement |
| --- | --- |
| CART-001 | A project MUST contain metadata, MIGA Lua source, logical 12-bit palettes, a color-response profile identifier and version, tile/object assets, a declared graphics profile, optional map data, and optional ProTracker music. Native code and source line mappings are generated in RAM by the trusted compiler. |
| CART-002 | A shareable cartridge SHOULD be a single file with magic, format version, section directory, declared lengths, checksums, and no native pointers. |
| CART-003 | Multi-byte fields MUST use a documented byte order; big-endian is preferred to minimize target conversion. |
| CART-004 | Every section MUST be independently bounds-checked before use. Unknown optional sections MUST be skippable. Unknown mandatory sections MUST reject the cartridge. |
| CART-005 | Version 1.0 cartridges MUST NOT embed executable machine code. MIGA-80 MUST compile source into a fresh native-code arena for the current compiler and ABI, preventing stale or externally injected native code from bypassing language safety. |
| CART-006 | A release cartridge MUST be reproducible from its source and assets. Given the same MIGA Lua compiler, native ABI, and target profile, compilation MUST produce the same generated code and data layout. |
| CART-007 | Compression MUST be deterministic, streamable or bounded-memory, and fast enough to load from floppy. RLE and a small LZ-family codec are candidates. |
| CART-008 | The shell MUST show packed disk size and worst-case resident size before saving. |
| CART-009 | A malformed cartridge MUST never reach native code generation with an unchecked section, offset, count, or decompression result. |
| CART-010 | The cartridge MUST identify its container, MIGA Lua language, and target-profile versions. Compiler and native ABI versions belong to the MIGA-80 system and trusted in-session compiled image, not to an executable cartridge payload. |
| CART-011 | A cartridge MUST store logical palette values and the response-profile identifier, not a private replacement LUT. An unknown mandatory profile or incompatible profile version MUST reject the cartridge with a specific diagnostic rather than silently changing its colors. |

## 10. MIGA Lua language and native compiler

### 10.1 Design intent and compatibility promise

The built-in language, provisionally named **MIGA Lua**, is a strongly and statically typed game language whose surface syntax stays as close as practical to Lua 5.1. Familiarity and easy transfer of programming habits are goals; full Lua semantics, standard-library compatibility, and the ability to run arbitrary `.lua` programs are not.

MIGA Lua deliberately replaces Lua's dynamically typed values, universal tables, floating-point default, garbage-collected heap, dynamic loading, and metaprogramming with fixed layouts that compile efficiently for a 14 MHz 68EC020. Unsupported Lua constructs MUST be rejected at compile time with a specific diagnostic rather than accepted with subtly different runtime behavior.

The native compilation pipeline is:

```text
MIGA Lua source
      -> lexer and Lua-like parser
      -> typed AST and whole-program checks
      -> compact typed IR
      -> data and stack layout
      -> 68020 instruction selection and register allocation
      -> machine-code emission and relocation
      -> generated-code validation
      -> Exec instruction-cache synchronization
      -> bounded native execution
```

This is the shipping on-Amiga pipeline. The compiler MUST run on the stock A1200 and in host-side tests. On the Amiga it writes 68020 instruction words directly into a preallocated code arena; it does not invoke GCC, an assembler, or a linker. It SHOULD use bounded passes, reusable arenas, and simple predictable optimizations. A failed compilation MUST leave no executable partial result.

Backend bring-up on the development host MAY use a second, non-shipping output path:

```text
typed IR -> shared low-level m68k instruction model
         -> textual assembly -> pinned assembler/linker -> ELF + flat image
         -> embedded 68EC020 runner -> register, memory, call, and trace assertions
```

Textual assembly makes early instruction selection, relocations, symbols, and disassembly easy to inspect. It MUST remain a development oracle rather than a product dependency. The direct encoder and assembly renderer SHOULD consume the same low-level instruction model so their outputs can be compared and do not become independent compiler backends.

### 10.2 Proposed source style

```lua
type Player = {
  x: fix,
  y: fix,
  tile: u8
}

const SPEED: fix = 2.0

local player: Player = {
  x = 128.0,
  y = 128.0,
  tile = 0
}

function init(): void
  music_play(0)
end

function update(): void
  if btn(LEFT) then
    player.x = player.x - SPEED
  elseif btn(RIGHT) then
    player.x = player.x + SPEED
  end
end

function draw(): void
  layer(PLANAR)
  clear(0)
  map_draw(0, 0, 32, 32)

  layer(PIXEL)
  clear(0)
  object_set(0, player.tile, player.x, player.y, 0)
end
```

Version 1 deliberately chooses a strict, compact type contract for the
on-Amiga compiler. Every function parameter, function return, and local
variable declaration MUST carry an explicit type. `void` is an explicit return
type; it is not inferred from a missing value. There is no implicit conversion,
except that the frozen grammar MAY later permit a conversion between constants
when the compiler proves it lossless at compile time. There are no polymorphic
or union types and no multiple returns in version 1. Local inference is kept
only as a possible later language extension, not as part of the first release.
The bootstrap currently implements this contract for `i8`, `u8`, `i16`,
`u16`, `i32`, `fix`, `bool`, `string`, and `symbol`; its `void` return path
remains to be implemented.
Phase 3 MUST freeze a grammar and a Lua-compatibility matrix before implementing
the production parser.

### 10.3 Lua-like syntax contract

Version 1.0 MUST support, where compatible with the static type model:

- `function`, `local`, `if`/`then`/`elseif`/`else`/`end`, `while`,
  `break`, the MIGA-specific `continue`, `repeat`/`until`, and numeric `for`
  syntax;
- generic `for` over compiler-known arrays and dictionaries;
- Lua-style function calls, field access, indexing, table constructors, comments, and lexical conventions;
- Lua operator spelling and precedence, with `!=` accepted as an exact alias
  of `~=`, and short-circuit `and`/`or` on `bool` values;
- boolean conditions with no truthy coercion from integers or other values;
- single-target assignment and exactly zero (`void`) or one return value;
- statement-only update sugar `x++`, `x--`, `x += value`, `x -= value`,
  `x *= value`, and `x /= value`; these forms MUST NOT produce a value or
  appear inside any expression or condition; `/=` is implemented in the
  bootstrap while the other update forms remain scheduled;
- the `:` method-call spelling when the receiver and target function resolve statically;
- immutable string literals and compile-time concatenation;
- source line and column mappings for every generated safe point and diagnostic;
- MIGA-specific `type`, `const`, type annotation, capacity, and fixed-size generic syntax.

MIGA Lua source SHOULD look familiar to a Lua programmer, but the documentation MUST call it a dialect and list every material difference from Lua 5.1.

### 10.4 Strong type and number model

The required built-in types are:

- `bool`;
- `i8`, `u8`, `i16`, `u16`, and `i32`;
- `fix`, a signed two's-complement Q16.16 fixed-point value;
- `color`, `layer`, `button`, `sprite_id`, and similar small domain types where they prevent accidental API misuse;
- immutable, interned `string` or `symbol` values;
- fixed-layout records;
- `array<T, N>`;
- `dict<K, V, N>`;
- statically known, monomorphic function signatures.

These names are exact source spellings. In particular, version 1 has no
`byte` or `word` aliases.

`void` is valid only as a function return type. There is no implicit `any`,
universal tagged-value type, polymorphic type, or union in the stock profile.
An integer literal has type `i32`; a decimal point selects `fix` directly.
Narrowing, signed/unsigned changes, and `i32`/`fix` conversion require an explicit
conversion. The bootstrap admits only an `i32` constant expression converted
to `i8`, `u8`, `i16`, or `u16` when the final value is provably representable;
all other implicit conversion remains rejected. No source operation may cause
the compiler or runtime to link software floating-point support.

The bootstrap explicit numeric conversions are deliberately limited to
`fix(i32)` and `i32(fix)`. `fix(i32)` accepts exactly -32768 through 32767,
shifts the value left by 16, rejects an out-of-range constant at compile time,
and sends a dynamic out-of-range input to controlled fault 2 with its source
location. `i32(fix)` truncates toward zero and cannot overflow. Conversions to
or from narrow integer types remain future explicit operations.

Top-level `local` and `const` declarations have module-static storage known at compile time and may be referenced by top-level functions, as in the example above. They do not allocate closure environments. Nested functions that capture activation-local values remain excluded in version 1.0.

Ordinary integer arithmetic SHOULD use defined two's-complement wrapping;
checked conversion helpers MUST be available. Signed integer division truncates
toward zero, defines minimum / `-1` as a per-width wrapping minimum, rejects a
provably zero divisor at compile time, and otherwise reaches a controlled
source-located runtime fault. Unsigned integer division has the analogous
zero-divisor behavior and uses unsigned quotient semantics. `fix` addition,
subtraction, and negation wrap modulo 2^32. Multiplication forms the exact
signed 64-bit product, extracts bits 16 through 47 (rounding toward negative
infinity), and wraps to 32 bits. Decimal literals are converted without host
floating point to nearest Q16.16, with ties away from zero. Fixed-point
division computes `(a * 65536) / b`, truncates toward zero, wraps to 32 bits,
and uses the common controlled zero-divisor fault. Trigonometry and every later
rounding operation MUST remain bit-exact across host tests and the 68020
backend.

### 10.5 Records, arrays, and optimized dictionaries

Lua table-constructor syntax MUST be resolved from an explicit expected type;
it maps to one of three statically selected layouts:

1. **Record:** a constructor with named fields and a fixed declared shape
   becomes a compact record. Field access compiles to a constant byte
   displacement; it performs no hash lookup.
2. **Array:** a homogeneous sequence becomes contiguous `array<T, N>` storage.
   Arrays are zero-based: valid indices are exactly `0` through `N - 1`, index
   `0` is the first element, and table-constructor sequence elements follow
   that same base. Capacity and element size are compile-time constants, and
   each dynamic index is bounds-checked.
3. **Dictionary:** a declared `dict<K, V, N>` becomes a fixed-capacity typed hash table. It is used only when keys are genuinely dynamic.

The 1.0 dictionary contract is:

- capacity `N` is fixed at compile time and storage is allocated before takeover;
- supported keys are bounded integers, enums/symbols, and interned immutable strings; arbitrary runtime strings are excluded initially;
- common asset/state keys use explicit `symbol("name")` literals, avoiding
  runtime string hashing without an implicit `string` conversion;
- capacity SHOULD be a power of two so bucket selection avoids division;
- the implementation SHOULD use deterministic open addressing with linear or Robin Hood probing and a documented maximum load factor;
- deletion uses a bounded documented tombstone or backward-shift strategy;
- insertion into a full dictionary produces a controlled source-level runtime fault;
- iteration order is deterministic and defined by the implementation, but need not match insertion order;
- record syntax MUST NOT silently degrade to a dictionary because of one misspelled field.

There is no general garbage-collected table type. Dynamic game collections use fixed arrays, typed dictionaries, or later a separately specified fixed-capacity pool type.
The zero-based array rule is a deliberate material difference from Lua's
conventional one-based sequences and MUST be called out in user documentation.

The implemented bootstrap `string` value is a canonical pointer into a
deduplicated cartridge constant pool. Its inline descriptor begins with an
explicit 32-bit byte length followed by payload, does not rely on NUL
termination, and compares by pointer identity because equal literals are
canonicalized. The implemented `symbol("name")` value is an opaque nonzero
32-bit interned identity, suitable for constant-time equality and dictionary
keys but not arithmetic or ordering. Conversion between the two is never
implicit. ABI 0.6 fixes their register classes and representation for one
bounded function; cartridge-wide merging and deterministic ID rewriting remain
requirements of the future multi-function pack/link step.

### 10.6 Excluded or restricted language features

- binary floating point and the standard Lua `number` model;
- unrestricted type changes and implicit dynamic dispatch;
- an unbounded heap or general garbage collector;
- runtime source compilation, `load`, `loadstring`, `dofile`, or `require`;
- `io`, `os`, `package`, `debug`, and other host-operating-system libraries;
- metatables, metamethods, weak tables, finalizers, reflection, or `eval`;
- coroutines or threads;
- userdata, pointers, arbitrary memory access, native-code literals, and inline assembly;
- captured closures in version 1.0; non-capturing function values MAY be supported when their targets resolve statically;
- unrestricted varargs;
- exceptions and user-defined trap handlers;
- recursion by default; if later enabled per function, it MUST consume a declared bounded stack and retain stop/budget checks;
- allocation of code or variable-sized objects during exclusive execution.

### 10.7 Program lifecycle

A cartridge MAY define these statically resolved entry points:

- `init(): void` — called once after the runtime and preallocated data are ready;
- `update(): void` — called at the cartridge's declared fixed rate;
- `draw(): void` — called after an update when a new frame is requested;
- `shutdown(): void` — called on a normal stop under a strict execution budget.

Missing callbacks are legal no-ops. MIGA-80, not generated code, owns the main loop, frame pacing, interrupt handling, error trampoline, and emergency stop path. `init()` may populate preallocated arrays and dictionaries but may not grow their capacity or allocate memory.

### 10.8 Native 68020 backend and ABI

The compiler targets the 68EC020/68020 user-mode instruction set directly. It MUST NOT emit FPU, MMU, 68030+, or privileged instructions. Literal 68000 compatibility is not a goal because the certified AGA platform already supplies a 68EC020.

The backend SHOULD initially implement only optimizations with clear value and small code cost:

- constant folding and propagation;
- removal of unreachable and trivially dead code;
- direct register or fixed stack-slot allocation for locals;
- constant-offset record fields;
- scaled or strength-reduced fixed-array addressing;
- inlining of very small arithmetic and guard helpers when it reduces total cost;
- direct native calls for recognized MIGA-80 built-ins;
- shared assembly helpers for expensive fixed-point operations.

The bounded pass architecture, optimization levels, regression signals, and
separation between Musashi correctness and physical-A1200 timing are specified
in [MIGA Lua Optimization Strategy](./MIGA-Lua-optimization-strategy.md).

The native ABI MUST define:

- one reserved address register for an immutable runtime-context pointer or an equivalent measured convention;
- caller/callee-saved data and address registers;
- a dedicated, fixed-size generated-code stack, with the hosted process stack saved outside it by a reviewed assembly entry trampoline;
- stack alignment, statically computed call-graph depth, maximum frame size, and overflow guard strategy;
- argument and multiple-return placement;
- an immutable, versioned jump table containing the only runtime functions callable by generated code;
- typed signatures and stable numeric IDs for every fantasy API function;
- error and stop trampolines that return control to MIGA-80 without returning through an invalid user stack;
- relocation kinds, code alignment, code-arena bounds, and source-map metadata.

Generated code MUST NOT address AmigaOS libraries, custom-chip registers, the Copper list, unrelated MIGA-80 state, or arbitrary absolute memory. Hardware access remains inside reviewed C/assembly runtime functions reached through the native ABI.

ABI preservation is executable behavior, not documentation alone. Before
invoking a generated function, the host runner MUST initialize callee-saved
registers, the stack, and guard memory with recognizable values, then verify
them after return. [MIGA Lua Native ABI 0.6](./MIGA-Lua-native-ABI-v0.md)
freezes the bootstrap register, immutable-value, and stack core: scalar
arguments in `D0-D2`, scalar result in `D0`, string arguments in `A0-A1`,
string result in `A0`, `D3-D7` and `A2-A6` callee-saved, runtime context in
`A5`, optional frame pointer in `A6`, and four-byte stack alignment.
Runtime-context offset zero points to the
non-returning controlled fault handler; fault code, source line, and source
column arrive in `D0-D2`. Code 1 denotes numeric division by zero and code 2
denotes an explicit numeric conversion outside its destination range. Multiple
returns are excluded from language version 1; stack arguments and additional
traps/context entries remain ABI-extension decisions.

### 10.9 Host-side generated-code execution

The primary local runner, provisionally named `miga68k-test`, embeds a pinned Musashi core configured as `M68K_CPU_TYPE_68EC020`. It is a compiler test appliance, not an Amiga emulator.

Its first version MUST provide:

- a bounds-checked 24-bit virtual address space with explicit big-endian 8-, 16-, and 32-bit memory callbacks;
- separate bounded regions for vectors/control data, code/constants, globals/test fixtures, a guarded stack, and synthetic host-call controls;
- ELF symbol or generated-manifest lookup plus a flat load image at deterministic test addresses;
- direct initialization and inspection of CPU registers, PC, SR, stack, and memory;
- a runner-owned return sentinel or reserved trap and a separate instruction ceiling, so normal return, controlled fault, and infinite execution cannot be confused;
- rejection and diagnostics for out-of-map access, execution outside code, forbidden instructions, stack damage, and the selected odd-address policy;
- deterministic runtime-service mocks implemented as reserved traps or synthetic control-page accesses that validate arguments, record calls, and return defined values;
- a circular disassembly trace, normally retained silently and printed with registers and nearby memory only on failure.

The proposed local memory addresses in [MIGA-80 Local 68020 Tooling](./MIGA-80-local-68020-tooling.md) are runner configuration, not cartridge or native ABI. Tests MUST cover big-endian layout, 24-bit address handling, sign/zero extension, stack alignment, branch limits, arithmetic flags, function calls, guards, runtime traps, illegal operations, and timeout behavior.

Most generated-code tests SHOULD be semantic: compile a function, execute it, and compare its result and side effects with the host typed-IR oracle. A small reviewed set MAY use normalized-disassembly goldens for prologues, epilogues, branches, addressing, fixed-point kernels, and guard sequences. Deterministic randomized programs and inputs SHOULD compare the typed-IR oracle with Musashi; a later curated edge corpus SHOULD also compare Musashi with Moira before hardware sign-off.

The runner SHOULD record code bytes, executed instructions, approximate core cycles, maximum stack use, runtime-call counts, and memory-operation widths. These are stable regression signals for CPU-bound generated code, not predictions of stock-A1200 wall time. The runner MUST NOT be extended to simulate Copper, bitplane, blitter, sprite, audio, interrupt, or Chip-RAM contention behavior; those remain UAE and real-hardware responsibilities.

### 10.10 Native-code safety, interruption, and determinism

Native generation removes interpreter overhead but also removes the structural safety of a bytecode dispatch loop. A stock 68EC020 provides no process isolation suitable for this design, so the compiler, emitter, guards, and ABI become part of the trusted computing base.

The following controls are mandatory:

- every array access, dynamic sprite/map index, dictionary operation, division, shift, conversion, and API argument with a safety range MUST be guarded unless the compiler proves it safe;
- every backward control-flow edge MUST decrement an execution budget and test the asynchronous stop flag;
- the compiler MUST reject recursive call graphs in version 1.0 and compute a worst-case native stack requirement before execution;
- function entry and other bounded safe points MUST test stack and stop state as required by the call graph;
- finite straight-line code is bounded by the maximum source/generated-code size;
- budget exhaustion, dictionary-full, divide-by-zero, invalid shift, bounds failure, stack failure, and invalid API input branch to a runtime error trampoline carrying a source location;
- generated branch targets, relocations, code/data ranges, entry points, stack requirements, guard metadata, and jump-table call targets MUST be validated before execution;
- release tests MUST disassemble or otherwise independently check emitted instruction streams on the host; malformed IR and relocation fuzzing MUST never produce an installable code image;
- generated code MUST reside in a dedicated fixed-size arena and MUST never be loaded directly from an untrusted cartridge section;
- after emission and relocation, MIGA-80 MUST call the appropriate Exec cache-clear function for the generated range before execution, because the 68020 has an instruction cache;
- no generated code or game data allocation occurs after takeover;
- random number generation uses a documented algorithm and explicit seed;
- given the same source, compiler version, assets, seed, and input sequence, game-visible state MUST be reproducible.

A compiler or native runtime defect can still corrupt the process and prevent restoration because there is no hardware sandbox. This residual risk MUST be documented, minimized through differential tests and fuzzing, and treated as more severe than an ordinary cartridge error.

### 10.11 Minimum fantasy API

The 1.0 API MUST cover:

- lifecycle and timing: `frame`, `ticks`, and cartridge rate metadata;
- input: `btn`, `btn_pressed`, keyboard key state where supported;
- graphics state: `layer`, `camera`, `layer_scroll`, `clip`, `palette`, `transparent`, and cartridge-level chunky viewport metadata;
- pixels and primitives: `clear`, `pixel`, `line`, `rect`, `rect_fill`, `circle`, `circle_fill`;
- planar assets: `tile`, `tile_region`, and `map_draw`;
- virtual objects: `object_set`, `object_hide`, `object_clear`, and bounded status/profiling queries;
- text: `print` with the built-in font;
- math: integer, fixed-point, trigonometric lookup, clamp, min/max, and deterministic random;
- audio: module play/stop/position and debug mute controls;
- diagnostics: bounded `trace` captured for display after restoration.

APIs MUST use logical coordinates and typed handles. They must not expose Chip RAM addresses, bitplane layouts, Copper instructions, AmigaOS handles, native function pointers, or arbitrary callbacks.

## 11. Graphics specification

### 11.1 Three-layer virtual GPU contract

The logical display is exactly 256 × 256 pixels and contains three independently updated layers:

| Layer | Logical role | Base representation | Intended strengths |
| --- | --- | --- | --- |
| `PLANAR` | Full-screen base | Four native AGA bitplanes | Tile maps, backgrounds, fonts, fills, planar sprites, and hardware scrolling |
| `PIXEL` | Positioned transparent viewport | 4-bit chunky source converted into a four-plane overlay | Software 3D, plasma, raycasting, particles, and arbitrary pixel access |
| `OBJECTS` | Ordered virtual object list | AGA sprites where possible, deterministic planar fallback otherwise | Players, enemies, projectiles, icons, and frequently moved images |

The baseline composition is `OBJECTS` over `PIXEL` over `PLANAR`, with no blending or partial alpha. Alternative object priority bands MAY be added only if their behavior and fallback equivalence are proven and documented.

- `PLANAR` has 16 visible colors numbered 0–15.
- `PIXEL` has transparent index 0 and 15 visible colors numbered 1–15.
- `OBJECTS` use transparent index 0 and the overlay palette so hardware and fallback rendering can remain visually equivalent.
- The resulting portable playfield palette contains 31 opaque logical colors, each selected from the 12-bit RGB index space (`0xRGB`, four bits per component); the selected response profile maps them to 24-bit AGA register values.
- Coordinates are integer with the origin at the top left of the 256 × 256 display.
- Each layer has independent dirty state and an independent scroll/camera offset. A common camera operation MAY update several offsets together.
- Out-of-bounds drawing is clipped, never wrapped unless an individual API explicitly requests wrapping.

`OBJECTS` is not a third bitmap playfield. It is a fantasy-machine abstraction over a scheduler that may choose direct hardware sprites, attached pairs, vertical multiplexing, or a planar fallback. Cartridge behavior MUST NOT depend on the selected path; documented profiler and capacity status may reveal it for optimization.

### 11.2 Operation routing and authoritative semantics

High-level operations map to the cheapest conforming backend rather than implicitly touching a generic full-screen framebuffer:

| Operation family | Preferred path |
| --- | --- |
| `map_draw`, tile copies, fonts, clear/fill, and planar sprites on `PLANAR` | Native plane writes, blitter operations, pointer movement, or cached planar assets |
| `pixel`, arbitrary primitives, and procedural effects on `PIXEL` | Chunky source drawing followed by viewport-only C2P |
| `object_set` and other object-list updates | Sprite control/pointer updates; Copper-assisted scheduling where proven |
| Object overflow or unsupported object shape/palette | Deterministic planar rendering into an appropriate display buffer, with affected regions marked dirty |

The API MAY permit a primitive on either drawable playfield, but its visible result, clipping, palette, and ordering MUST remain identical across optimized paths. Backend selection, dirty-region tracking, and cached asset formats are not part of the language or cartridge ABI.

A portable C reference compositor is authoritative. It models the three logical layers and object priority without depending on AGA channel allocation. Every optimized planar, C2P, blitter, sprite, multiplex, and fallback path MUST match reference golden images for supported inputs.

The initial C99 compositor now produces canonical palette identities for a planar view, positioned transparent pixel view, and priority-ordered object list. Native goldens cover clipping, source origins, camera translation, object tie-breaking, and hardware-sprite/fallback hint equivalence. An inverse AGA reference decoder reconstructs the canonical image from BPL1–BPL8. The single-layer C2P4 suite proves complete compositor → packed4/byte4 converter → dual-playfield decoder output for every viewport profile and available host backend; FS-UAE exercises the real 68020 assembly cores against the same canonical oracle. These correctness layers are integrated into `gmake check`; the hardware-sprite output adapter remains open until the object benchmark tranche. See [Three-Layer Graphics Reference Compositor](./graphics-reference-compositor.md), [AGA Dual-Playfield Reference Decoder](./aga-reference-decoder.md), and [Four-Plane C2P Reference and Benchmark](./c2p4-benchmark.md).

### 11.3 Chunky viewport profiles and source layouts

The chunky layer is a positioned viewport, not necessarily a full-screen framebuffer. Phase 0 MUST evaluate at least these profiles:

| Profile candidate | Pixels | Packed 4-bit source | Byte-per-pixel 4-bit source | Intended use |
| --- | ---: | ---: | ---: | --- |
| Small | 160 × 128 | 10 KiB | 20 KiB | Effects embedded in a mostly planar scene |
| Medium | 192 × 160 | 15 KiB | 30 KiB | Proposed default gameplay viewport |
| Full | 256 × 256 | 32 KiB | 64 KiB | Explicitly expensive full-screen pixel effects |

The cartridge declares its maximum viewport profile before runtime takeover. Resizing MUST NOT trigger allocation during a frame. The final preset names, dimensions, placement/alignment rules, and whether arbitrary smaller rectangles are accepted remain graphics-freeze decisions.

For the one-layer 4-bit C2P source, the primary layout candidates are:

- packed 4-bit, with two pixels per byte, minimizing source traffic and storage;
- byte-per-pixel 4-bit, with the low nibble significant, simplifying random writes at the cost of additional storage and read traffic.

The earlier combined-byte and two-layer layout benchmark remains useful evidence about source construction and the test protocol, but it no longer defines the target architecture. The current single-layer series compares packed4 and byte4 at all three viewport sizes using C99/68020 pair-LUT converters, table-free C99/68020 32-pixel transposes, and a staged blitter-publication negative control. The real 68020 C2P4 cores are now wrapped by a report-format-3 exclusive screen/DMA/memory matrix: 204 stock cases, or 260 when a complete Fast-assisted source/LUT/stack tier is available. It preserves exact canonical output, records source/destination/lookup placement and offsets, and separates total payload traffic from its Chip-RAM subset. The FS-UAE signal favors packed4 plus mask32, but it does not select a layout: exclusive real-hardware timing, genuine CPU/blitter merge work, safe publication, and stock-A1200 distributions remain mandatory. See [Four-Plane C2P Reference and Benchmark](./c2p4-benchmark.md); [Chunky Layout and C2P Benchmark](./c2p-layout-benchmark.md) records the historical experiment.

The converted `PIXEL` playfield remains a planar overlay. Regions outside the visible chunky viewport are transparent and MAY also receive cached planar fallback objects if the selected implementation can preserve ordering and damage restoration correctly.

### 11.4 AGA composition and object scheduling

The display uses eight low-resolution AGA bitplanes in dual-playfield mode:

- one four-plane playfield carries `PLANAR` without C2P;
- the other four-plane playfield carries the converted `PIXEL` overlay and any compatible planar fallback work;
- overlay index 0 is transparent and reveals `PLANAR`;
- AGA hardware sprites form the fast path for `OBJECTS`, conceptually acting as a third visual plane;
- object images that use four colors may consume one physical sprite channel; 16-color images may use an attached pair;
- moving a resident hardware object should normally update coordinates/control data rather than redraw pixels.

The exact PF1/PF2 assignment, palette-register and sprite-bank mapping, playfield/sprite priority, fetch mode, modulo, plane pointers, sprite widths, attached-pair rules, multiplex schedule, and Copper program MUST be captured in hardware tests and frozen in a register-level design note. If a hardware object cannot reproduce the logical palette or priority exactly, the scheduler MUST use the documented fallback rather than silently alter the image.

The first hosted Phase 0 smoke test provisionally maps the transparent overlay to PF1 (BPL1/3/5/7, palette base 0) and the opaque base to PF2 (BPL2/4/6/8, palette base 16). An Intuition-managed PAL 256 × 256 × 8 dual-playfield screen passes mode, palette, Chip-RAM, reference-C2P output, raster-pattern, and repeated restoration checks under FS-UAE/Kickstart 3.0. The C2P4 benchmark exercises a native-planar PF2 base with packed4/byte4 viewport conversion into PF1, including real pair-LUT and mask32 68020 assembly and staged `BltBitMap()` publication paths. Its exclusive successor runs those two assembly cores directly into the live PF1 planes under blanked display and six additive active-DMA profiles, including four Paula channels, seven controlled sprite payloads, and fair/hog concurrent blits, with exact output and state-restoration checks. It also covers aligned and misaligned raw reads, writes, copies, and read-modify-write loops, with optional Fast-source/LUT comparisons. These FS-UAE results prove mapping, integration, and protocol—not final performance or safe display publication. A private Copper/runtime interrupt path, virtual-object scheduling, genuine hybrid C2P, Kickstart 3.1, stress tests, and real-A1200 gates remain open. See [Hosted AGA Screen Smoke Test](./aga-screen-smoke.md), [Four-Plane C2P Reference and Benchmark](./c2p4-benchmark.md), and [Exclusive Graphics Benchmark Plan](./graphics-exclusive-benchmark.md).

#### 11.4.1 Logical 12-bit gamut and color-response profiles

MIGA-80 presents exactly 4,096 colors to cartridge code and asset tools, following the classic Amiga `0xRGB` convention. These values are logical color coordinates, not literal AGA register contents. Each built-in response profile owns an immutable 4,096-entry lookup table:

```text
logical 0xRGB (12-bit) -> profile LUT[4096] -> hardware 0xRRGGBB (24-bit AGA)
```

Consequently, a predefined MIGA-80 gamut may draw each of its 4,096 entries from anywhere in AGA's approximately 16.7-million-color space while the cartridge still sees only 4,096 possible colors. Only the 31 opaque colors selected by the current dual-playfield palette are resident simultaneously; the lookup table does not change that display limit. Different logical colors MAY converge after quantization or by design, most notably in the monochrome profile.

Version 1.0 MUST provide exactly five photographic-stock profiles, three historical-video profiles, two common color-vision-deficiency simulations, and one console-inspired profile:

| Profile family | Version 1.0 response target | Reference year |
| --- | --- | ---: |
| Photographic negative | Kodak Professional PORTRA 400 | 2010 |
| Photographic reversal | Kodak Professional EKTACHROME E100 | 2018 |
| Instant film | Polaroid Color 600 | 1981 |
| Photographic negative | Lomography LomoChrome Metropolis | 2019 |
| Panchromatic monochrome | ILFORD HP5 PLUS | 1989 |
| Historical video | NTSC 1953 colorimetry | 1953 |
| Historical video | 625-line PAL/SECAM colorimetry | 1967 |
| Historical Soviet video | OSKM (ОСКМ, «Одновременная совместимая система с квадратурной модуляцией»), the experimental Soviet 625/50 quadrature system used before SECAM adoption | 1960 |
| Color vision | Deutan red–green deficiency simulation, Machado model | 2009 |
| Color vision | Protan red–green deficiency simulation, Machado model | 2009 |
| Console-inspired | Mega Drive soft quantization with a midtone-only violet bias | 1988 |

The stock and console names are response targets, not claims of manufacturer endorsement or exact chemical or hardware reproduction; public preset naming remains subject to trademark review. The profile is selected per cartridge and applies uniformly to editor preview, reference rendering, hardware sprites, planar fallback, and exclusive runtime output. Copper palette changes are not part of the base cartridge API.

The generator MUST model color in linear light before final display encoding and 8-bit quantization. For each film stock it SHOULD use published spectral-sensitivity, characteristic, and dye-density data where available, supplemented by a calibrated color-target capture of the named stock, processing chemistry, scan or print path, illuminant, and reference white. For each video profile it MUST use documented primaries, white point, transfer behavior, luma coefficients, and the static color effect of its encode/decode path. OSKM is modeled as a documented historical reconstruction and MUST be labelled as such wherever surviving source data leaves a parameter uncertain.

Each response contracts red, green, and blue sensitivity and tone range according to its source measurements. The transform MAY also include a 3 × 3 linear-light cross-channel matrix, per-channel characteristic curves, and bounded gamut mapping where physically warranted; three arbitrary independent RGB multipliers are not sufficient evidence of fidelity. ILFORD HP5 PLUS MUST derive one luminance response from its panchromatic spectral sensitivity and emit neutral `R = G = B` output unless a separately documented paper or viewing tint is later approved. Grain, halation, scanlines, chroma delay, noise, and other spatial or temporal artifacts are outside this palette-only contract.

The deutan and protan modes MUST apply the corresponding full-deficiency endpoint matrices from the 2009 Machado–Oliveira–Fernandes physiologically based model in linear RGB. They are simulations for finding confusing color pairs, not corrective filters and not substitutes for contrast, shape, text, outlines, or redundant non-color cues. The editor MUST label that individual perception and severity vary.

The Mega Drive mode MUST preserve all 4,096 logical colors as distinct 24-bit results. It pulls each component only part-way—initially 28%—toward the nearest value in the console's 3-bit-per-channel RGB vocabulary; it MUST NOT collapse the gamut to the Mega Drive's 512 hardware colors. A smooth luminance bell then raises red and blue and contracts green in the midtones, with exactly zero violet bias at black and white. This violet cast is an explicit MIGA-80 art-direction choice inspired by the appearance of Mega Drive titles, not a claim that every VDP, encoder, cable, or display had that measured response.

Every shipped LUT MUST record its profile ID and version, source-data provenance, illuminant and white-point assumptions, transform parameters, generator revision, and checksum. Tests MUST cover all 4,096 inputs, deterministic rounding, black/white behavior, neutral-ramp behavior, gamut bounds, representative color-chart patches, editor/runtime identity, and hardware register output. The Mega Drive test additionally asserts 4,096 distinct mapped colors and zero purple bias at both luminance endpoints. Direct nibble replication (`0xRGB -> 0xRRGGBB`) remains a non-creative reference path for diagnostics and differential tests, not one of the eleven selectable profiles.

The initial 256-color grids and their reproducible generator are documented in [MIGA-80 Color-Response Palette Studies](./color-response-palettes/README.md).

#### 11.4.2 LUT storage and integer-only runtime contract

Floating point MAY be used by the host tooling to fit spectral data, matrices, curves, and gamut mappings, but the canonical shipping transform is frozen either as quantized RGB24 values or as a precisely specified fixed-point descriptor. The Amiga build MUST NOT pull in software floating-point support for this feature. Any target-side table construction occurs only in hosted mode, before takeover, and its output becomes the same immutable direct-index LUT used by the precomputed route.

A naive embedded representation is too large:

| Representation | One profile | Eleven profiles |
| --- | ---: | ---: |
| Packed RGB24, three bytes per logical color | 12,288 bytes (12 KiB) | 135,168 bytes (132 KiB) |
| Aligned `uint32_t`, low 24 bits significant | 16,384 bytes (16 KiB) | 180,224 bytes (176 KiB) |

Phase 0 MUST implement and measure both of these alternatives:

| Alternative | Stored representation | Hosted-mode work | Exclusive-runtime result |
| --- | --- | --- | --- |
| Precomputed LUT pack | One independently compressed, checksummed RGB24 block per profile | Validate, decompress, and widen the selected block | One aligned 16 KiB table |
| Dynamic fixed-point generation | Compact matrices, curve tables, gamut parameters, and mode flags | Generate and checksum all 4,096 entries once with integer operations | The identical aligned 16 KiB table |

The precomputed alternative uses a separate, versioned response-pack resource. Its directory records, for every profile, the stable ID and version, compressed offset and length, decoded length, decoded LUT checksum, compression method, and flags. Profile blocks MUST be independently decompressible so selecting one profile never expands all eleven. The packed resource remains part of the mandatory floppy/data budget even though it is not linked into the executable.

The dynamic alternative stores only a versioned fixed-point descriptor for each profile. The canonical target arithmetic SHOULD use 16-bit operands and 32-bit accumulators convenient for the 68020: normalized channels initially use Q14 (`0x0000` = 0 and `0x4000` = 1), signed matrix coefficients use Q13 (`0x2000` = 1), and every multiply-accumulate step has explicit rounding and saturation. Small integer curve tables represent nonlinear responses. Gamma, film toe/shoulder, the Mega Drive midtone bell, and any other nonlinear operation MUST use bounded lookup tables or documented fixed-point polynomials; target code MUST NOT call `pow`, trigonometric functions, floating-point conversion, or general division. Shifts, rounding of negative values, saturation order, and intermediate widths MUST be specified bit-for-bit rather than relying on implementation-defined signed shifts or overflow.

Because there are only 4,096 source values, the dynamic generator MAY exhaustively reconstruct the table when a profile is selected. It MUST run in hosted mode into a preallocated destination, remain responsive enough for interactive profile changes, report a controlled error on failure, and produce the canonical profile checksum before the table is accepted. Phase 0 MUST record generation time, peak scratch memory, descriptor size, code size, and checksum equality on a stock 14 MHz 68EC020. The fixed-point representation becomes authoritative only if the host reference generator can execute the same integer path and produce byte-identical output for all eleven profiles.

In hosted mode, MIGA-80 either validates and expands the selected packed block or generates it from the selected fixed-point descriptor. Both routes write into a preallocated, 32-bit-aligned 4,096-entry table whose entries have the form `0x00RRGGBB`, and both use checked integer operations only. At most one full response table need be expanded at a time; an optional second table for before/after editor comparison MUST be charged explicitly to the editor memory budget. Fast RAM is preferred when available, but the stock configuration allocates the table in ordinary CPU-accessible Chip RAM.

Once selected, mapping a logical color is semantically just:

```c
aga_rgb = response_lut[logical_rgb12 & 0x0fff];
```

This direct lookup is the only exclusive-runtime color-response operation. It requires no per-color transform, interpolation, multiplication, division, decompression, table generation, or floating point. The 31 active opaque colors SHOULD additionally be cached as 32-bit AGA values, so an unchanged palette performs no LUT reads or register preparation during a frame. The selected table, active cache, and response profile are immutable after exclusive takeover; no response-pack disk I/O, block decompression, or fixed-point reconstruction is permitted there.

For the floppy edition, Phase 0 SHOULD prefer dynamic fixed-point generation if its descriptors, code, latency, and scratch memory are materially cheaper than retaining the compressed LUT pack and it remains byte-identical to the canonical output. If the packed route wins, the response pack is a mandatory startup resource and MUST be read before the boot disk may be exchanged. If the dynamic route wins, all descriptors and small shared curve tables MUST already be resident before disk exchange. A hybrid MAY keep packed blocks only for profiles whose response cannot be reconstructed compactly or quickly enough, but every profile still produces the same table format and runtime behavior.

Tests MUST prove byte-identical output between the host canonical LUT, packed-resource decode where used, fixed-point reconstruction where used, editor preview, and values written to AGA. They MUST exercise all 4,096 inputs for every profile and include overflow, saturation, rounding, corrupted-descriptor, and wrong-checksum cases. The target link map MUST be checked for unintended floating-point helper symbols. Malformed lengths, offsets, codecs, descriptors, checksums, or profile IDs MUST fail before allocation or runtime takeover.

### 11.5 Dirty updates, C2P, and buffering

- The three layers MUST NOT implicitly force one another to redraw.
- An unchanged `PLANAR` layer performs no bitmap work; scrolling SHOULD use plane pointers, fine scroll, and incremental row/column updates where possible.
- An unchanged `PIXEL` viewport performs no C2P. A changed viewport converts only its required area into four destination planes.
- A hardware-backed object move SHOULD update only sprite coordinates/control data. Fallback objects MUST add only their damaged regions to the relevant work list.
- The implemented reference four-plane C converters produce bit-exact packed4 and byte4 output for every tested pixel, plane, viewport profile, and stride. This contract MUST remain authoritative.
- Optimized candidates MUST include CPU-only 68020 assembly, blitter-assisted conversion, and a CPU/blitter hybrid. Blitter participation is accepted only when the full pipeline is faster under active display and audio DMA.
- Display memory MUST be in Chip RAM and aligned for the selected AGA fetch mode.
- Safe publication MAY use double buffering, pointer swaps, Copper changes, or bounded damage repair, but MUST never expose a partially updated frame.
- A correct full-viewport path is mandatory even if dirty tiles or scanline spans are optimized.
- A missed deadline MUST repeat the prior display frame rather than expose partial work or change simulation timing implicitly.

The existing allocation-free C99 converters and layout tests remain correctness oracles for plane ordering. Their scalar timings do not satisfy a performance target and MUST NOT select the new single-layer source layout.

Performance decisions MUST measure the combined cost of source construction, representative pixels/primitives, planar tile and scroll work, C2P, object scheduling and fallback, safe display publication, and audio coexistence. CPU and blitter stages MAY be pipelined only when real-hardware measurements show useful overlap after Chip-RAM contention.

### 11.6 Independent scrolling and bounded margins

The API SHOULD expose `layer_scroll(PLANAR, x, y)`, `layer_scroll(PIXEL, x, y)`, and `layer_scroll(OBJECTS, x, y)`, with `camera(x, y)` available as common-offset sugar.

- `PLANAR` scrolling SHOULD use bitplane pointer adjustment, AGA fine scrolling, and a small planar backing margin.
- The converted `PIXEL` representation MAY use a larger planar backing area around the visible viewport. A candidate 192 × 160 viewport in a 256 × 224 backing area provides ±32-pixel margins.
- `OBJECTS` scrolling SHOULD apply a camera offset to scheduled coordinates without changing resident image data.
- Slow tile-map movement SHOULD update only newly exposed rows or columns.

Phase 0 MUST compare no margin, ±16 pixels, and ±32 pixels. The guarantee is a bounded hardware-assisted movement window, not a requirement for the cartridge to redraw the enlarged backing area every frame. Alignment, edge refill, wrapping, and the point at which a backing area is rebased MUST be deterministic and visible in profiling.

### 11.7 Video profiles and visible budgets

- Certified video output is PAL 50 Hz.
- **MIGA-80 Standard** targets deterministic 25 Hz simulation/rendering, with each completed frame displayed for two refreshes.
- **MIGA-80 Turbo** targets 50 Hz and requires an explicit cartridge opt-in plus stricter measured budgets.
- A full 256 × 256 chunky refresh is a high-cost feature, not the baseline workload. It is available only within the performance envelope frozen by Phase 0.
- Input-to-display latency is no more than two display refreshes in the standard path.
- The empty runtime MUST miss no video deadline over a 30-minute test.
- A representative Standard cartridge MUST show no torn frames or audio starvation over a 30-minute test.

The runtime profiler MUST separately expose at least CPU update/draw time, C2P time and bytes, blitter occupancy, dirty-layer/region state, chunky pixels touched, object count, physical sprite channels/pairs used, multiplex events, fallback objects/bytes, audio time, total frame time, and 25/50 Hz deadline misses. Emulator measurements validate the protocol only; performance sign-off requires a stock 2 MiB A1200 with active bitplane, sprite, blitter, and audio DMA representative of the tested profile.

### 11.8 Editor and runtime drawing

The editor and generated-code runtime MUST share the same logical clipping, palette, primitive, tile, object, and composition semantics. Generated code reaches them through the native ABI; optimized paths may replace individual operations only when golden-image and fallback-equivalence tests remain identical. The editor MAY use a simpler AmigaOS-cooperative backend as long as its preview is visibly equivalent.

## 12. Input specification

### 12.1 Hosted input

Hosted mode MUST use AmigaOS input facilities and support:

- built-in keyboard;
- mouse movement and buttons;
- joystick port 1;
- joystick port 2 when present.

### 12.2 Runtime input

Runtime MUST support at least:

- four directions and one action button from a standard Amiga joystick, plus a second button when the connected controller and chosen input method support it;
- a defined subset of keyboard keys;
- a stop chord that generated code cannot mask or redefine;
- per-frame current, pressed, and released states.

The implementation may use preinstalled OS interrupt/resource mechanisms or carefully owned hardware access, but it MUST not depend on an AmigaOS task being scheduled during exclusive mode. Keyboard acknowledgement and CIA ownership are high-risk items and MUST be proven in Phase 0 on real hardware.

Input events used by the cartridge MUST be sampled at deterministic frame boundaries. The interrupt/input backend owns the emergency-stop flag, and compiler-inserted safe points MUST observe it independently of cartridge logic.

## 13. Audio architecture

### 13.1 Hosted backend

The hosted backend owns no hardware without allocation. It uses `audio.device` and the appropriate CIA resource or a similarly cooperative mechanism. If another application holds the channels or timer, MIGA-80 SHOULD continue editing silently and explain why preview is disabled.

### 13.2 Exclusive backend

Before takeover, the runtime reserves the four Paula channels and any required timer. During execution:

- Paula reads samples directly from Chip RAM;
- the replayer updates periods, volumes, sample addresses, and lengths from an interrupt-safe state;
- generated code never writes Paula registers directly;
- tempo processing must not allocate, call DOS, or wait;
- stopping resets DMA and volumes before OS audio ownership is returned.

CIA-timed replay is preferred for correct ProTracker tempo. A VBlank-based fractional scheduler MAY be used initially if its compatibility and jitter are measured, documented, and accepted at a roadmap gate.

### 13.3 Compatibility corpus

The project MUST maintain a legal, redistributable test corpus containing:

- one minimal module for each required effect;
- combined-effect and effect-memory cases;
- loop edge cases;
- maximum legal pattern/order cases;
- truncated and malicious files;
- at least three representative songs cleared for inclusion or test use.

Expected row, tick, period, volume, loop, and song-position traces SHOULD be compared with a declared ProTracker compatibility reference. Audio waveform equivalence is useful but state-trace equivalence is the primary deterministic test.

## 14. Storage and packaging

### 14.1 Floppy edition

The release build MUST produce a bootable `.adf` and a manifest. The image SHOULD contain only:

- boot metadata and `S:Startup-Sequence`;
- the MIGA-80 executable;
- embedded or separate mandatory fonts/help data;
- one small example cartridge;
- license and short read-me files if space permits.

Initial payload allocation:

| Component | Target ceiling |
| --- | ---: |
| Boot glue and required system files | 48 KiB |
| Packed MIGA-80 executable and mandatory data | 440 KiB |
| Built-in help, font, and templates | 64 KiB |
| Example cartridge | 192 KiB |
| Filesystem and growth reserve | 56 KiB |
| **Total planned payload** | **800 KiB** |

The release pipeline MUST fail when the actual image exceeds its block budget. Compression MAY be used, but the decompressor becomes target code and must obey the C99/limited-assembly policy and restoration requirements.

### 14.2 AmigaOS licensing constraint

A bootable AmigaDOS disk may require copyrighted operating-system components or files not owned by the MIGA-80 project. The project MUST NOT redistribute them without a valid license.

At least one legally viable release route is required:

1. distribute an installer that builds the bootable image from the user's licensed AmigaOS media;
2. obtain redistribution permission;
3. use a compatible redistributable component after proving it meets the stock A1200 requirements; or
4. distribute the MIGA-80 files as a non-bootable disk plus instructions, while treating the bootable-image requirement as not yet complete.

This legal decision is a release gate, not a documentation footnote.

### 14.3 Hard-disk edition

- Installation MUST work by copying one directory to an AmigaDOS volume.
- The main binary MUST launch from CLI.
- A Workbench tool icon and tooltypes SHOULD be provided.
- Paths MUST be relative to the program or use an assigned MIGA-80 volume name; hard-coded `DH0:` paths are prohibited.
- The hard-disk edition MAY contain more examples and offline documentation, but the core behavior and cartridge limits MUST match the floppy edition.

### 14.4 Suggested cartridge container

The proposed `.m80` layout is:

```text
Header
  magic = "M80C"
  container version
  MIGA Lua language version
  target profile
  flags
  section count
  total packed and resident sizes
  whole-file checksum

Section directory[]
  type
  flags (mandatory, compressed)
  offset
  packed length
  resident length
  checksum

Sections
  META  cartridge metadata, graphics profile, and limits
  SRC   canonical MIGA Lua source
  PAL   31 logical 12-bit colors + response-profile ID/version
  GFX   packed tile and virtual-object image data
  MAP   optional map data
  MOD   optional validated module
  NOTE  optional author notes
```

No loader may trust header counts, sizes, offsets, compression output sizes, or checksums before bounds validation.

The canonical response LUTs are system resources, not cartridge sections. Cartridges store only the stable response-profile ID/version and their 31 logical RGB12 colors; they MUST NOT duplicate the 4,096-entry mapping.

## 15. Memory and performance budgets

### 15.1 Memory policy

All RAM on a stock A1200 is Chip RAM and is shared with DMA. MIGA-80 MUST behave correctly without Fast RAM and MUST account for contention, not merely capacity.

The runtime exposes two orthogonal memory tiers internally:

| Memory tier | Contract |
| --- | --- |
| `stock_chip_only` | Required baseline: all code and data fit and meet their certified budgets with exactly 2 MiB Chip RAM. |
| `fast_assisted` | Optional transparent acceleration: CPU-only code/data prefer Fast RAM while the same cartridge and virtual-hardware contract remain valid. |

Good Fast-RAM candidates include the executable where the loader permits it, generated 68020 code, the dedicated runtime stack, MIGA Lua globals and dictionaries, game state, the packed4 chunky source, and CPU-only lookup or scratch data. Bitplanes, Copper lists, hardware-sprite data, Paula samples, and every blitter source or destination MUST remain in Chip RAM. A buffer that changes from CPU-only to DMA-visible must be allocated or explicitly staged in the correct domain; a pointer must never silently cross that boundary.

The profiler MUST record the selected memory tier and per-domain bytes. Fast-assisted results may demonstrate useful acceleration, but they may not raise the minimum cartridge requirements or substitute for stock release gates. C2P and combined-frame benchmarks SHOULD compare Chip-source-to-Chip-plane against Fast-source-to-Chip-plane traffic on equipped hardware.

Initial peak target:

| Area | Target ceiling |
| --- | ---: |
| Program code, read-only data, globals, and C runtime | 300 KiB |
| Virtual-GPU buffers, scroll margins, object tables, and caches | 192 KiB |
| Loaded cartridge, including module samples | 384 KiB |
| Editor text, undo, compiler arenas, and diagnostics | 256 KiB |
| Generated native code, globals, guarded stack, dictionaries, and runtime work memory | 128 KiB |
| Copper, audio state, input queues, OS objects, alignment, and reserve | 128 KiB |
| **MIGA-80 target peak** | **1,388 KiB** |
| **Nominal remainder for AmigaOS and safety** | **660 KiB** |

These are ceilings, not allocations that must all be permanent. The 192 KiB graphics figure is a planning envelope for double-buffered four-plane playfields, a selected chunky viewport source, bounded scroll margins, and object control/cache data; it is not a promise that every maximum option can coexist. Phase 0 MUST publish measured memory profiles for Small, Medium, and Full viewports and for the chosen object fallback. Editor/compiler arenas SHOULD be reset and reused for runtime work. The executable MUST avoid a single large contiguous allocation when smaller pools are sufficient. Preflight MUST report both total free memory and largest free block.

### 15.2 Allocation rules

- Every subsystem MUST have an explicit arena or owner.
- Hosted subsystems MAY use `malloc()`/`free()` where the ownership and ceiling remain explicit; a custom general-purpose allocator is not required.
- Runtime allocations MUST finish before takeover.
- Importers MUST validate declared lengths before allocation.
- Undo history and diagnostic logs MUST be bounded.
- Temporary compiler structures SHOULD be arena-allocated and released in bulk.
- DMA-visible buffers and samples MUST use Chip RAM allocation flags.
- If Fast RAM exists, non-DMA hosted and runtime data MAY prefer it according to the declared memory tier; success on Fast RAM MUST not conceal a stock-memory regression.
- Allocation failure MUST preserve the current project and return a useful size report.

### 15.3 CPU budgets

Phase 0 must establish measured budgets rather than estimate them from emulator speed. The frame profiler MUST separately measure:

- generated native `update()` logic;
- generated native `draw()` logic and fantasy-API calls;
- native planar tile, primitive, and incremental-scroll work;
- chunky viewport drawing and four-plane conversion;
- object scheduling, multiplexing, and planar fallback;
- blitter occupancy and CPU/blitter overlap;
- Copper/frame synchronization;
- ProTracker tick processing;
- input handling;
- editor redraw and compiler time.

The runtime SHOULD expose an optional raster-bar or numeric profiler. A representative stock cartridge at 25 Hz MUST retain at least 20% measured CPU headroom after audio and conversion. Optional 50 Hz mode must pass the same test with its declared content.

Graphics reports MUST also account for a conservative payload lower bound: source construction and edits, source reads, lookup/intermediate reads, plane writes, staged publication, and display-plane fetch. This is not a substitute for measurement and MUST exclude no cost silently. Instruction fetch, memory refresh, Copper, sprites, audio, transaction granularity, and arbitration must be listed separately as omitted or measured effects.

Fine timing has two distinct harnesses. The hosted cooperative harness validates APIs, allocations, exact pixels, reports, and cleanup. Its `WaitTOF()` calls occur outside timed brackets, but task scheduling can still perturb a bracket and the waits substantially increase wall-clock duration. Its reports MUST use `timing_scope=hosted_cooperative` and cannot select an implementation.

The performance-authority harness enters a bounded exclusive-runtime section only after files, allocations, and setup are complete. It owns the required Copper/display, DMA, and interrupt state, freezes AmigaOS scheduling, supports a raster-polled kernel-batch mode and a minimal MIGA-80 VBlank/runtime-frame mode, measures batched or back-to-back iterations without per-sample `WaitTOF()`, and restores state symmetrically. It MUST distinguish `exclusive_kernel_batch` from `exclusive_runtime_frame` and use stack canaries, phase/progress markers, and practical exception diagnostics so a controller can distinguish slow execution, deadlock, stack damage, a crash, and teardown failure. This requirement does not authorize an unbounded blind `Disable()` region: the matched call is restricted to short atomic entry/leave windows, and hardware/interrupt ownership must follow [Exclusive Graphics Benchmark Plan](./graphics-exclusive-benchmark.md).

## 16. Software architecture

### 16.1 Major components

| Component | Responsibility |
| --- | --- |
| `shell` | Workspace navigation, commands, help, status, and error presentation. |
| `editor_code` | Text buffer, rendering, commands, undo, and diagnostic navigation. |
| `editor_sprite` | Tile/object pixel tools, palettes, hardware-eligibility diagnostics, reference previews, and undo. |
| `compiler_frontend` | Lua-like lexer/parser, explicit type checking, semantics, typed AST/IR, and source maps. |
| `codegen_68020` | Layout, instruction selection, shared low-level m68k model, direct encoding, development assembly rendering, relocation, validation metadata, and cache synchronization. |
| `native_runtime` | Versioned jump table, guarded callback invocation, execution budgets, stop/error trampolines, and fantasy API dispatch. |
| `miga68k_test` | Host-only Musashi 68EC020 runner, bounded memory, runtime traps, assertions, traces, and CPU regression metrics. |
| `runtime_test_stubs` | Deterministic host-call stubs that validate the generated native ABI without pretending to emulate Amiga hardware. |
| `cartridge` | Container validation, packing/unpacking, versioning, and size accounting. |
| `storage` | Hosted C stream and AmigaDOS volume, directory, error, and safe-save operations. |
| `video_core` | Three-layer virtual-GPU semantics, command routing, dirty tracking, palettes, clipping, and portable reference composition. |
| `video_planar` | Native planar tiles, primitives, cached assets, damage tracking, and hardware-assisted scrolling. |
| `video_chunky` | Configurable viewport sources, pixel primitives, four-plane C2P, and viewport damage tracking. |
| `video_objects` | Virtual object list, AGA sprite allocation, attached pairs, multiplex scheduling, priority, and deterministic fallback. |
| `video_hosted` | OS-cooperative editor display. |
| `video_exclusive` | Copper, dual playfields, sprite DMA, blitter coordination, safe publication, and hardware state restoration. |
| `audio_mod` | Validated MOD model, tick/effect state machine, and trace tests. |
| `audio_hosted` | OS-cooperative preview. |
| `audio_exclusive` | Paula/CIA ownership and register backend. |
| `input_hosted` | AmigaOS keyboard/mouse/joystick events. |
| `input_exclusive` | Runtime polling/interrupt capture and emergency stop. |
| `platform_amigaos` | C runtime integration plus AmigaOS libraries, memory classes, timing, startup, shutdown, and resource ownership. |
| `takeover` | Transactional state machine for exclusive entry and restoration. |

The compiler frontend, typed IR, 68020 emitter, cartridge parser, MOD state machine, and reference renderer SHOULD also compile natively on the host for fast tests. A host-only typed-IR interpreter SHOULD provide the semantic oracle for generated 68020 execution; `miga68k_test` executes the actual emitted instructions. Platform dependencies MUST sit behind narrow interfaces.

### 16.2 C99 and assembly policy

- All shipping target source MUST be C99 except documented `.s`/`.S` files.
- Compiler-specific attributes and register conventions MUST be isolated in platform headers.
- Assembly is justified for interrupt entry/exit, exact custom-chip access sequences, chunky-to-planar conversion, and proven rendering hot spots.
- Every assembly optimization MUST have a readable C reference or a precise behavioral test oracle.
- Assembly functions MUST document clobbered registers, stack alignment, memory alignment, address-space requirements, and C ABI.
- No assembly optimization may be merged without real-hardware measurements.
- User cartridges contain no hand-authored native assembly or C and MUST NOT embed arbitrary machine code; only the trusted MIGA Lua compiler may populate the native code arena.

### 16.3 Toolchain

The preferred toolchain is the maintained `m68k-amigaos-gcc` family with AmigaOS NDK-compatible headers and libraries. It builds MIGA-80 itself; it is not invoked by the on-Amiga MIGA Lua compiler. The build MUST pin an exact toolchain commit or reproducible container image even though the product does not require a particular GCC version.

Initial MIGA-80 system compiler/linker policy:

- C language mode: `-std=c99`;
- CPU baseline: `-m68020` or the verified equivalent;
- no hardware FPU assumption; target code MUST not introduce FPU instructions;
- C runtime: libnix Kickstart 2+ startup selected with `-mcrt=nix20`, placed last on the link command;
- optimize for size for cold/editor code and benchmarked speed for hot paths;
- emit an AmigaOS Hunk executable compatible with the target Kickstart versions;
- avoid ixemul and other non-stock runtime dependencies;
- retain a host-side symbol/map file while stripping release debugging data from the floppy binary;
- treat warnings as errors in project code;
- generate a size report by object and section for every release build.

The initial system ABI and toolchain revisions are locked in `toolchain/versions.lock`. The runtime matrix selected libnix over clib2 on size after both passed the required allocation/filesystem tests under `vamos` and FS-UAE/Kickstart 3.0; newlib failed required semantics and introduced an external math-library startup dependency. Kickstart 3.1 and real-A1200 results remain mandatory revalidation gates, and any resulting ABI change MUST be recorded explicitly.

### 16.4 Host compiler-validation toolchain

The compiler-development loop defined in [MIGA-80 Local 68020 Tooling](./MIGA-80-local-68020-tooling.md) is separate from the Amiga Hunk build:

| Role | Initial choice | Policy |
| --- | --- | --- |
| Primary CPU execution | Musashi in 68EC020 mode | Required for fast generated-code tests; source revision and license MUST be pinned |
| Secondary CPU oracle | Moira | Optional until a curated differential corpus justifies its C++20 integration cost |
| Development assembler/linker | GNU `m68k-elf` binutils or vasm/vlink | Select and pin one syntax/toolchain before substantial backend work |
| Binary inspection | ELF symbols plus `objdump`/disassembler output | Preserve deterministic failure artifacts and normalized disassembly |
| Test driver | Existing project build/test system | One ordinary host command MUST run native and generated-code tests |

The recommended bring-up route renders textual assembly, creates a linked ELF image at deterministic addresses, extracts a flat image for the runner, and retains symbols for diagnostics. Once the ABI and instruction selection stabilize, the shipping direct encoder becomes authoritative. Differential tests MUST compare its bytes/behavior with the reviewed assembly route until the bootstrap route can safely become optional.

Third-party sources MUST be reproducibly fetched or vendored with notices and checksums. No developer-specific absolute path may be embedded in build scripts, manifests, test reports, or documentation. Adopted Musashi, Moira, assembler, linker, and binary-tool versions MUST be added to `toolchain/versions.lock`.

The initial source boundary SHOULD be recognizable without constraining later directory refactors:

```text
compiler/{frontend,ir,backend_m68k}
runtime/{abi,stubs,amiga}
tools/miga68k-test
tests/{compile,diagnostics,execute,differential,performance}
```

Musashi integration belongs behind the runner interface; compiler and runtime code MUST NOT include emulator-specific APIs.

### 16.5 Build outputs

One command SHOULD produce:

- host unit-test binaries;
- host `miga68k-test` runner and generated-code semantic/ABI suite;
- retained ELF, flat image, symbol manifest, and short trace for failed compiler tests;
- debug Amiga Hunk executable and map;
- stripped release executable;
- hard-disk installation directory;
- legal bootable ADF when required licensed inputs are supplied;
- image manifest with hashes and byte/block usage;
- example `.m80` cartridge;
- test report separating native semantics, local 68EC020 execution, UAE integration, real-hardware validation, graphics, MOD parsing, and packaging.

## 17. Error handling and data safety

### 17.1 Error classes

MIGA-80 MUST distinguish:

- startup/platform incompatibility;
- insufficient or fragmented memory;
- DOS/device/media error;
- invalid cartridge/container;
- compile error;
- type/IR error or native code-generation/validation error;
- controlled runtime fault;
- resource acquisition/takeover failure;
- internal invariant failure.

Expected user errors return to the relevant editor. A takeover failure unwinds to hosted mode. An internal error before takeover should exit cleanly when safe. A fatal hardware-state error may require a reboot; the documentation must be honest about this residual risk.

### 17.2 Project safety

- The in-memory project remains authoritative until a save completes.
- Compile and run MUST NOT implicitly mark an unsaved project as saved.
- The UI MUST display unsaved state before quit or disk exchange.
- Save failures MUST retain the old valid file and in-memory edits whenever possible.
- Checksums detect corruption but do not replace safe write ordering.
- Recovery files MAY be written to hard disk, but automatic floppy writes are off by default.

### 17.3 Untrusted input

Cartridges and modules are untrusted input. Host builds MUST run their parsers under sanitizers and fuzzing. Target builds MUST use checked arithmetic for offsets, sizes, counts, multiplication, decompression bounds, and sample-loop calculations.

## 18. Verification strategy

### 18.1 Test layers

1. **Native host unit tests:** lexer, Lua-like grammar, explicit type rules, typed IR, record/array/dictionary layouts, instruction encoding and relocation, fixed-point math, cartridge parser, compression, MOD effect state, and the three-layer graphics reference model.
2. **Property and fuzz tests:** cartridge/MOD parsing, decompression, source lexer/parser, typed IR, relocation inputs, dictionary operations, checked arithmetic, and malformed or nonterminating generated programs.
3. **Local 68EC020 execution:** run generated functions under Musashi; assert results, side effects, ABI preservation, stack balance, guards, runtime calls, controlled traps, and instruction limits. A curated edge corpus SHOULD later run under Moira as an independent CPU oracle.
4. **Golden and differential tests:** three-layer composite hashes, planar operation output, four-plane C2P bytes, complete 4,096-entry color-response LUTs, packed-resource decode, palette mapping, editor/runtime/hardware color identity, object-scheduler/fallback traces, selected normalized 68020 disassembly, direct-encoder behavior versus the host typed-IR oracle and assembly route, source error locations, runtime traces, and MOD tick traces.
5. **UAE integration:** PAL A1200, 68EC020, AGA, exactly 2 MiB Chip RAM, no Fast RAM; generated-code cache synchronization, native ABI integration, floppy boot, HD launch, disk swaps, error paths, hosted/exclusive timing-scope labeling, and crash-versus-timeout diagnostics.
6. **Real-hardware tests:** at least two stock A1200 units if available, original or representative floppy drive/media, CRT and modern display adapter where relevant; authoritative graphics runs use the bounded exclusive harness and repeat the Chip-RAM calibration under recorded DMA states.
7. **Soak tests:** editor idle, music preview, generated-code replacement, runtime, repeated run/stop, repeated save/load, and low-memory behavior.

The local CPU runner is essential for fast compiler diagnosis but validates neither AmigaOS integration nor hardware timing. UAE is essential for system automation but cannot sign off custom-chip timing, Chip-RAM contention, CIA behavior, floppy reliability, restoration, or stock-machine frame budgets.

Every commit SHOULD run native frontend/IR tests, Musashi generated-code semantics and ABI checks, malformed-code/time-limit cases, deterministic differential tests with recorded seeds, and the small approved code-size/instruction regression set. UAE integration MAY run on a slower schedule; stock-A1200 execution and timing remain milestone and release gates. A failed generated-code case MUST retain enough deterministic input, image, symbols, and short trace to reproduce it locally.

### 18.2 Required compatibility matrix

| Configuration | Floppy boot | HD CLI | Workbench launch | Edit | Run/restore |
| --- | --- | --- | --- | --- | --- |
| PAL A1200, KS/WB 3.0, 2 MiB only | Required | Required | Required | Required | Required |
| PAL A1200, KS/WB 3.1, 2 MiB only | Required | Required | Required | Required | Required |
| PAL A1200, 3.0/3.1 with Fast RAM | Required | Required | Required | Required | Required |
| NTSC A1200 | Safe rejection | Safe rejection | Safe rejection | Not certified | Not certified |
| AGA emulator, exact stock profile | Required in CI | Required in CI | Recommended | Required | Required, then confirm on hardware |

### 18.3 Release acceptance criteria

MIGA-80 1.0 is complete only when all of the following are true:

- A legal distribution artifact fits and boots from one real DD floppy.
- The hard-disk edition launches from CLI and Workbench.
- A user can create a project, edit source, draw sprites, import a supported MOD, save, reload, compile, run, stop, and continue editing on a stock A1200.
- The included example cartridge demonstrates input, the native planar layer, a chunky viewport, virtual objects, transparency, scrolling, object fallback, text, music, and a controlled runtime error screen.
- The frozen Standard hybrid-graphics workload sustains 25 Hz without tearing or starving music; each optional larger-viewport or Turbo profile meets its separately documented envelope.
- Compiler, generated native code, and runtime behavior are deterministic across the host typed-IR oracle, Musashi runner, UAE, and hardware, plus selected independent CPU-oracle cases when that optional runner is adopted.
- The generated-code suite covers ABI preservation, memory guards, controlled runtime traps, invalid accesses, forbidden instructions, and instruction-budget exhaustion with reproducible failure traces.
- Malformed corpus and fuzz regressions do not crash or corrupt memory.
- The 1,000-cycle takeover/restoration test passes.
- A 30-minute representative runtime and a two-hour hosted editing/music-preview soak pass.
- Peak memory and ADF block usage remain within frozen release budgets.
- User documentation describes controls, limits, file compatibility, backup practices, and unsupported configurations.

## 19. Development roadmap

The estimates below are engineering effort for one experienced developer, not calendar promises. They include implementation and ordinary testing but not large hardware-procurement or licensing delays. The critical path is the hybrid graphics freeze on real hardware first, then safe on-target native compilation and the content workflow, then hardening. The local CPU-runner workstream is independent of AGA work and does not block the next graphics benchmarks.

Current Phase 0 evidence includes the reproducible cross-toolchain, native C2P golden vectors, the initial three-layer C99 reference compositor and goldens, an inverse dual-playfield decoder, validated graphics-report schemas, Amiga Hunk benchmark harnesses, and an Intuition-managed 4+4 dual-playfield smoke test under FS-UAE. The architecture-aligned C2P4 tranche includes packed4/byte4 scalar oracles, C99 and 68020 pair-LUT implementations, table-free C99 and 68020 32-pixel transposes, a staged blitter-publication negative control, all three viewport profiles, payload traffic accounting, and exact full-frame canonical differentials under host tests and FS-UAE. The 24-case hosted run remains `hosted_cooperative`. Its report-format-3 `exclusive_kernel_batch` successor has a 204-case stock baseline: six core raw kernels over seven additive display/DMA profiles, 26 extended raw read/read-modify-write/alignment kernels over three representative profiles, and twelve real 68020 C2P4 cases per profile. A complete Fast allocation appends 32 source-reading raw and 24 source/LUT C2P cases under blanked and fair-blitter states, for 260 total; DMA-visible data and planar destinations remain in Chip RAM while the dedicated stack moves to Fast and code placement is reported. Every case records memory domains, offsets, total payload traffic, and the Chip-contended subset.

The harness runs from a guarded 16 KiB dedicated stack inside `Forbid()`,
raster-polls blanking transitions without per-sample `WaitTOF()`, uses short
`Disable()` handovers, drives four real muted Paula channels, drives seven
16 × 224 hardware sprites with a declared minimum fetch load of 6,328 bytes per
video frame, and overlaps each CPU kernel with an adaptive fair-blitter transfer
or a separately identified hog-mode preemption burst. The 128-byte blitter
working set is repeated into logical loads from 512 KiB through 4,194,176 bytes,
allowing the fair fixture to remain busy across even the Full conversion without
consuming a second multi-megabyte buffer. A dedicated 32-bit timer made from two
cascaded CIA timers replaces `ReadEClock()` inside this long
custom-interrupt-masked section, after testing exposed a lost lower-word rollover
around 92 seconds; timer ownership is acquired and released through
`cia.resource`. The passing format-3 FS-UAE reports validate all output hashes
and `DMACON`/`INTENA` restoration, measure 268 bytes of stack high water, and
discard no timer sample. A bootable writable OFS hardware-candidate ADF and
manifest provide `running`/FAIL/PASS on-disk result states, bounded raster and
blitter waits, instruction-cache state restoration, and a tester handoff.
Emulator reports remain non-authoritative for physical timing, and stock-A1200
distributions, safe publication, a genuine hybrid, Level-3 runtime timing, and
stress/fault tests remain open. See
[MIGA-80 Physical A1200 Graphics Test](./physical-a1200-graphics-test.md). Older
report formats remain regression fixtures so distributed candidate images can
still be diagnosed, but their timings must not be pooled with format 3.

The pinned Musashi runner foundation and compiler connection are implemented.
One explicitly typed `i8`/`u8`/`i16`/`u16`/`i32`/`fix`/`bool`/`string`/`symbol`
function with typed local declarations, assignments, signed/unsigned
comparisons, integer `/`, fixed-point multiplication/division, explicit
`fix(i32)`/`i32(fix)`, immutable literal equality, statement-only `/=`, nested `if`/`else`,
and nested `while` is parsed into a bounded AST, lowered to a typed multi-block
stack IR, and interpreted as a host oracle. ABI 0.6 freezes the register/stack
core, immutable descriptor/ID representation, mixed register classes, and first
controlled-fault entry and numeric fault codes in a machine-readable module shared by the frontend,
backend, and Musashi checks, with a saved-register negative control.
The `-O1` path renames locals through cyclic CFGs, creates typed branch and loop `phi` joins,
folds and simplifies values, removes dead values and overwritten assignments,
solves fixed-point per-block liveness with edge-specific `phi` uses, reuses
locations across exclusive branches, coalesces compatible `phi` slots,
resolves parallel edge copies including cycles, requires one canonical latch
and exit per cyclic loop, folds multiple `continue`/`break` sites through
binary control funnels, preserves dynamic division and conversion faults even
when their result is dead, preserves used saved registers, and renders assembly
byte-identically from host and 68020 compiler builds. When seven ordinary registers are insufficient in a
spilling plan it uses `D7` as a saved scratch, reuses bounded spill slots, and
emits an ABI `A6` frame. Seven ordinary source corpora at `-O0` and `-O1`, a
signed-division corpus, an exact-width integer corpus, an immutable-value
corpus, a fixed-point corpus, a fixed-division corpus, an explicit-conversion
corpus, and a forced spill fixture agree with their oracles across 168 Musashi executions while recording
image size, instruction count,
maximum callee stack use, and controlled fault source locations. Calls, `void`
code generation,
ELF/symbol-manifest loading, the shipping direct encoder, and UAE/hardware
performance validation remain pending.

### Phase 0 — Feasibility and irreversible decisions

**Estimate:** 7–10 engineer-weeks

Deliverables:

- reproducible GCC cross-toolchain and minimal C99 Hunk executable;
- bootable test ADF and HD launch test on Kickstart 3.0/3.1;
- exact empty-boot and Workbench-launch memory measurements;
- portable reference compositor for `PLANAR`, positioned `PIXEL`, and `OBJECTS`, including deterministic object fallback;
- AGA 256 × 256 4+4 dual-playfield display with a native planar base, transparent four-plane overlay, and frozen 31-color playfield mapping;
- versioned offline color-response LUT generator, provenance manifests, 4,096-entry goldens for all eleven profiles, comparative prototypes for the external compressed-pack decoder and dynamic fixed-point generator, one-profile 16 KiB expansion, byte-identical checksum proof, stock-68020 generation/decode timing and size measurements, link-map proof of no floating-point dependency, and verified 24-bit AGA palette programming;
- a reproducible graphics benchmark suite covering source construction, draw operations, conversion, safe publication, and checksums;
- stock-versus-Fast-assisted placement measurements for generated code, runtime stack, packed4 source, and CPU-only scratch, without changing cartridge semantics;
- reference C and optimized CPU-only, blitter-assisted, and CPU/blitter-hybrid four-plane C2P candidates;
- native planar tile/primitive rendering, pointer/fine scrolling, incremental row/column refill, and no-margin/±16/±32-margin measurements;
- direct, attached, and vertically multiplexed AGA sprite prototypes plus measured deterministic fallback;
- combined scenes with bitplane, sprite, blitter, and Paula DMA active at the same time;
- pinned Musashi integration configured for 68EC020, with license/notice handling and a bounds-checked big-endian 24-bit test memory model;
- `miga68k-test` execution of a hand-written function with controlled return, instruction limit, register/stack guards, and failure trace;
- minimal host compiler path from one typed function through textual assembly, linked ELF/flat image, symbols, and local semantic assertions;
- minimal strongly typed expression/function compiler that emits and safely invokes native 68020 code on the A1200;
- generated-code instruction-cache synchronization and stop-at-loop-backedge proof;
- prototype hosted-to-exclusive takeover and restoration loop;
- joystick and keyboard emergency-stop prototype;
- Paula four-channel playback of a minimal MOD with candidate timing backends;
- measured OFS/FFS image payload capacity;
- licensing decision record for the boot disk.

Exit gate:

- The display is stable on real PAL A1200 hardware.
- A frozen Standard profile sustains 25 Hz on a stock A1200 with audio enabled and at least 20% measured CPU headroom in representative planar, pixel-viewport, object-heavy, and combined scenes.
- Small, Medium, and Full chunky profiles have measured packed-versus-byte source results; Full may remain explicitly expensive, but its correct behavior and budget are documented.
- The project has selected its four-plane C2P path, scroll margin, object/palette/priority contract, color-response data and transforms, direct/attached/multiplex limits, and deterministic fallback policy from real-hardware evidence.
- The local runner executes hand-written and compiler-generated arithmetic/control-flow functions, detects nonreturn and invalid memory, and verifies the provisional ABI without UAE.
- At least one generated function produces matching observable results under the typed-IR oracle, Musashi, UAE, and the on-target direct emitter; target cache synchronization and guards remain part of that proof.
- The native compiler can emit, relocate, validate, cache-synchronize, run, budget-stop, and discard a small guarded 68020 program without destabilizing AmigaOS.
- Run/restore succeeds 100 consecutive times without a leak or broken OS state.
- A credible path exists to stay below 1,388 KiB peak MIGA-80 memory and 800 KiB disk payload.
- The project has a legal route to a bootable release image.

If this gate fails, do not build editors. Reduce the default viewport, object guarantee, scroll margin, video buffering, memory model, cartridge cap, or boot scope first. Preserve the three-layer API only where a deterministic implementation remains affordable; do not hide an unbounded software fallback behind it. If the native compiler itself fails its time, size, cache, or safety gate, simplify its language/backend or explicitly revisit the bytecode fallback before proceeding; do not maintain two production execution engines.

#### Phase 0 graphics benchmark order

The next graphics work MUST proceed in this order so each result has an oracle and a clear decision consequence:

Step 1 is complete for dual-playfield output. The later object tranche must extend canonical decoding with a hardware-sprite adapter, without changing the playfield contract.

1. **Completed:** establish the portable compositor, inverse AGA dual-playfield decoder, exact differential path, and common benchmark report schema. Use identical logical scenes and canonical hashes across every backend.
2. **In progress:** benchmark four-plane C2P at 160 × 128, 192 × 160, and 256 × 256 for packed and byte-per-pixel sources. The hosted matrix compares C99 and 68020 pair-LUT CPU paths, a table-free 32-pixel 68020 transpose, and a staged blitter-publication negative control. The exclusive successor wraps the real pair-LUT and mask32 68020 cores plus a 32-kernel raw set in blanked and additive active-DMA profiles. Report format 3 includes byte/word/long reads, read-modify-write, 68020 misalignment cases, guarded active ranges, separate total/Chip traffic, and an optional all-or-none Fast source/LUT/stack tier. It retains four real muted Paula channels, seven explicit 16 × 224 sprite payloads, adaptive sustained fair-blitter traffic, a hog-mode preemption burst, per-case contention counters, and direct 32-bit CIA-cascade timing that remains autonomous while custom interrupts are masked. Its 204-case stock and 260-case Fast-equipped FS-UAE runs are protocol-valid, and the hardware-candidate ADF has a documented physical handoff and recoverable on-disk status. Before selecting a path, collect repeated physical stock-A1200 distributions and separately labelled Fast comparisons, then implement genuine CPU/blitter merges, dirty/no-change cases, safe publication, and Level-3 runtime-frame timing.
3. Benchmark native planar clears, fills, tiles, text, cached planar sprites, whole-screen pointer scroll, fine scroll, and incremental exposed-row/column updates.
4. Compare no scroll margin, ±16 pixels, and ±32 pixels, including Chip-RAM footprint, rebase spikes, and worst-case refill cost.
5. Benchmark the virtual object scheduler with direct four-color sprites, attached 16-color pairs, realistic vertical multiplexing, channel exhaustion, priority conflicts, and planar fallback.
6. Run representative planar-only, medium-viewport, object-heavy, and combined scenes with bitplane, blitter, sprite, and Paula DMA contention. Measure Standard and Turbo deadlines and the cost of safe publication.
7. Repeat decisive cases on real stock hardware, publish distributions rather than a single sample, and freeze the smallest contract that meets the headroom gate.

Every case MUST follow the [Graphics Benchmark Report Format](./graphics-benchmark-report-format.md): source construction/drawing, CPU conversion work, blitter work or wait, publication, total frame distribution, memory footprint, dirty bytes/regions, object allocation/fallback counts, E-Clock frequency, DMA state, timing scope, conservative traffic accounting, and matching oracle/output checksums. FS-UAE remains useful for correctness and automation but cannot choose a performance winner. A controller timeout alone is not a benchmark result; successful cleanup or an explicit failure marker is required.

#### Compiler-tooling workstream

This workstream follows [MIGA-80 Local 68020 Tooling](./MIGA-80-local-68020-tooling.md) and is staged to avoid turning the local runner into another Amiga emulator:

1. **Runner foundation — Phase 0 (implemented):** embed Musashi, select 68EC020 mode, implement bounded big-endian memory, execute a reviewed `mul_add` function, stop through a return sentinel/trap, and report a short failure trace.
2. **Compiler connection — Phase 0/early Phase 3 (initial subset implemented):** render integer arithmetic and return through textual assembly, assemble/link automatically, retain ELF symbols, load a flat image, and execute several inputs from the ordinary host test command. The bootstrap currently retains an Amiga relocatable object rather than ELF, so ELF/symbol-manifest loading remains part of this step.
3. **ABI freeze — early Phase 3 (register/stack, exact-width scalar, fixed-point, immutable value, source-local, loop-edge, spill-frame, and numeric-fault tranches implemented):** ABI 0.6 fixes register roles, `A5` runtime-context ownership, scalar and string returns, canonical `bool`/`i8`/`u8`/`i16`/`u16`/`i32`/`fix`/`symbol` register and slot representations, canonical string descriptors, mixed register classes, per-width wrapping, Q16.16 multiplication/division and explicit conversion semantics, frame bounds, stack alignment, executable saved-register checks, and runtime-context offset zero for a non-returning controlled fault handler. Fault codes 1 and 2 identify numeric division by zero and conversion out of range. `-O0` assigns typed source locals to bounded `A6` slots; `-O1` renames locals through cyclic control flow and emits bounded `A6` spill/edge frames when needed while restoring `D3-D7/A6`. Calls, `void`, cartridge-wide pool linking, additional context entries, and later fault codes remain open extensions; multiple returns are excluded from version 1.
4. **Semantic expansion — Phase 3 (`bool`, exact-width integers, signed Q16.16 `fix` including division and explicit `i32` conversions, immutable `string`/`symbol`, signed/unsigned comparisons and integer division, `if`/`else`, `while`, `break`, `continue`, and `/=` implemented):** the typed IR now has bounded cyclic multi-block control flow, canonical single-latch/single-exit loops, binary funnels for multiple loop-control sites, loop-carried O1 value joins, fixed-point CFG liveness, `phi`-slot coalescing, parallel edge-copy resolution, fall-through jump elision, per-width result normalization, bit-exact Q16.16 multiplication/division/conversion, a bounded deduplicated immutable pool, and controlled source-located numeric faults. Add globals, arrays, records, dictionaries, narrow explicit conversions, other update sugar and guards, runtime mocks, negative tests, and deterministic randomized differential tests.
5. **Direct-encoder convergence — Phase 3:** compare the shipping encoder with the assembly route and typed-IR oracle until encoding, relocation, source maps, and behavior agree; retain only selective normalized-disassembly goldens.
6. **Performance regression — Phase 3 onward (cyclic CFG allocation, division, exact-width, immutable-value, fixed-point, and conversion signals implemented):** the `-O0`/`-O1` differential records code size, executed instructions, and maximum callee stack use for seven ordinary corpora, signed-integer and fixed-division corpora, an exact-width integer corpus, an immutable-value corpus, a fixed-point corpus, an explicit-conversion corpus, and a forced spill fixture. The conditional case verifies CFG-aware register reuse and the coalescing of six live `phi` values into two stack slots; the loop case reduces 308/313/48 code-bytes/instructions/stack-bytes at O0 to 188/164/28 at O1, while the loop-control image falls from 356 to 288 bytes. The integer-division image falls from 108/28/28 to 44/7/0 on its normal path, and four controlled faults preserve their source locations. The exact-width image falls from 472 bytes at O0 to 220 at O1, with its normal paths reduced from 107-110 to 32-34 instructions and stack high-water from 44 to 20 bytes. The immutable-value image falls from 196 to 136 bytes and its paths from 46-47 to 23-24 instructions. The fixed-point image falls from 196 to 104 bytes and its paths from 57-58 to 25-26 instructions. The fixed-division image falls from 300 to 164 bytes and its normal paths from 95-101 to 51-55 instructions. The conversion image falls from 208 to 112 bytes and its normal paths from 40-50 to 22-26 instructions. The suite currently covers 168 Musashi executions. Approximate core cycles, calls, memory-operation counts, and representative cartridge kernels remain pending. Never translate emulator values into A1200 frame claims.
7. **Independent CPU validation — late Phase 3 or hardening:** add Moira only when the edge-case corpus is large enough to justify the adapter; investigate every divergent final state and keep real hardware authoritative.

The runner mocks graphics/audio/input calls only at the native ABI boundary. Copper lists, blits, sprite DMA, audio DMA, raster interrupts, Chip-RAM contention, C2P/display interaction, and 25/50 Hz deadlines remain exclusively in the UAE/real-hardware workstream.

### Phase 1 — Platform foundation and hosted shell

**Estimate:** 4–6 engineer-weeks

Deliverables:

- platform library/resource wrappers with strict ownership tracking;
- Chip/Fast memory arenas and low-memory diagnostics;
- hosted 256 × 256 screen, three-layer-equivalent preview, font, widgets, and command routing;
- keyboard, mouse, and joystick hosted input;
- DOS volume browser and bounded file reader;
- safe shutdown and startup error paths;
- hard-disk install layout and Workbench icon prototype;
- continuous emulator smoke test.

Exit gate:

- Shell boots from ADF and HD, remains responsive under AmigaOS multitasking, browses OFS/FFS, and exits cleanly on both target OS versions.

### Phase 2 — Graphics, input, and exclusive runtime core

**Estimate:** 7–10 engineer-weeks

Deliverables:

- frozen three-layer virtual graphics semantics and portable reference compositor;
- native planar renderer for tiles, primitives, fonts, cached images, dirtiness, and bounded hardware scrolling;
- selected chunky viewport layouts, optimized four-plane C2P, dirty-region handling, and safe publication;
- virtual object table, direct/attached sprite allocator, accepted multiplex scheduler, palette/priority handling, and deterministic planar fallback;
- frozen dual-playfield, sprite, palette, Copper, blitter, and buffering backend;
- exclusive input backend and protected emergency stop;
- transactional takeover/restoration implementation;
- frame scheduler, deterministic Standard/Turbo modes, and per-layer/DMA profiler;
- soak and 1,000-cycle restoration harness.

Exit gate:

- Native C planar-scroller, medium-pixel-viewport, object-stress/fallback, and combined scenes run at their certified rates with input, audio contention, and clean restoration on real stock hardware.

### Phase 3 — MIGA Lua compiler and native 68020 backend

**Estimate:** 10–16 engineer-weeks

Deliverables:

- versioned MIGA Lua reference, Lua 5.1 compatibility matrix, and grammar;
- lexer, parser, strict explicit type checking, fixed-point semantics, AST, and typed IR;
- fixed-layout records, arrays, optionals, and deterministic fixed-capacity dictionaries;
- shared low-level m68k representation, development assembly renderer, 68020 data/stack layout, instruction selector, register/stack allocation, direct binary encoder, relocation, and generated-code validator;
- versioned native ABI, immutable runtime jump table, typed planar/pixel/object/input/audio bindings, and stop/error trampolines;
- bounds/division/shift/stack guards plus execution-budget and stop checks at every backward control-flow edge;
- generated-code arena ownership and Exec instruction-cache synchronization;
- deterministic random, lifecycle, source line maps, and runtime diagnostic capture;
- host typed-IR oracle, Musashi semantic/ABI/negative suites, selective emitter/disassembly goldens, deterministic differential execution, and relocation/IR fuzz targets;
- optional Moira adapter and curated cross-core edge corpus when justified;
- native-host compiler test build, retained failure artifacts, code-size/instruction/stack regression reports, and on-target compile-time/code-size reports.

Exit gate:

- A nontrivial MIGA Lua game agrees with the typed-IR oracle and local 68EC020 runner, compiles to native 68020 code on the stock A1200, runs deterministically, detects an infinite loop and representative guarded faults, and returns source-level errors after restoring AmigaOS.

### Phase 4 — Cartridge, code editor, and sprite editor

**Estimate:** 7–10 engineer-weeks

Deliverables:

- versioned `.m80` container and bounded compression;
- safe-load and safe-save workflow;
- source editor with navigation, diagnostics, search, and bounded undo;
- tile/object/palette editor with required tools, all eleven color-response profile previews, logical/mapped color inspection, hardware-eligibility diagnostics, and reference-composited preview;
- budget meters and project metadata;
- integrated compile/run/stop loop preserving editor state;
- example cartridge graphics and source.

Exit gate:

- A user can create, save, reload, edit, compile, and run a small sprite-based game entirely on a stock A1200.

### Phase 5 — ProTracker import and integrated audio

**Estimate:** 4–7 engineer-weeks

Deliverables:

- defensive MOD importer and compatibility report;
- required ProTracker effect set and trace corpus;
- hosted preview backend;
- exclusive Paula/CIA or accepted timing backend;
- cartridge music controls and size accounting;
- audio restoration and soak tests.

Exit gate:

- Supported corpus modules preview and run correctly, malformed modules fail safely, timing traces match the declared reference, and no stuck DMA/channel state survives stop.

Audio may begin as a Phase 0 spike and proceed in parallel with Phases 3–4 once the ownership model is fixed, but the final integration gate remains here.

### Phase 6 — Floppy productization and release hardening

**Estimate:** 6–8 engineer-weeks

Deliverables:

- final boot image generation and legal packaging route;
- program/data size optimization and locked budgets;
- help screens, templates, example cartridge, and user manual;
- complete low-memory, disk-full, write-protected, disk-swap, and corrupt-input behavior;
- compatibility matrix and real-hardware sign-off;
- performance tuning driven by profiler evidence;
- release candidate soak, recovery, and regression runs;
- reproducible build manifest and source release packaging;
- pinned third-party CPU-runner/tool licenses, notices, source revisions, and reproducible host-test bootstrap instructions.

Exit gate:

- Every release acceptance criterion in section 18.3 passes.

### 19.1 Overall estimates

| Milestone | Cumulative estimate | Outcome |
| --- | ---: | --- |
| Feasibility gate | 7–10 weeks | Hybrid GPU, local 68EC020 runner, core hardware, memory, disk, legal model, and minimal native compiler path proven. |
| Runtime prototype | 18–26 weeks | Native C three-layer scenes with safe takeover, graphics, input, audio, and generated-code test foundations. |
| Creator alpha | 35–52 weeks | Locally validated on-machine MIGA Lua-to-68020 compiler, code editor, tile/object editor, cartridges, and integrated run loop. |
| Version 1.0 | 45–67 weeks | Floppy/HD packaging, MOD compatibility, native-code hardening, docs, and real-hardware certification. |

A solo part-time project should translate these into milestones rather than fixed dates. Schedule contingency belongs mainly to takeover/restoration, object scheduling/fallback, Chip-RAM contention, four-plane C2P performance, native ABI/direct-encoder convergence, CIA/keyboard behavior, floppy packaging, and editor usability.

## 20. Prioritization and scope cuts

If measured constraints force reductions, cut in this order:

1. Moira integration and cross-core coverage beyond a small curated edge corpus;
2. syntax coloring and advanced editor commands;
3. optional MIGA Lua conveniences such as non-capturing function values, method-call sugar, and generic dictionary iteration;
4. 50 Hz simulation mode;
5. optional ProTracker effects and hosted live preview;
6. extended map tooling and formats beyond the minimal native planar tile path;
7. sprite transform conveniences beyond flip;
8. multiple example cartridges and expanded on-disk help;
9. compression sophistication.

Do not cut:

- restoration safety;
- stock 2 MiB support;
- on-Amiga MIGA Lua compilation to native 68020 code;
- strong types, fixed data layouts, generated-code guards, stop safe points, and the closed native ABI;
- fast local semantic execution of generated 68EC020 code with bounds, ABI, guard, and timeout checks;
- the code and sprite editors;
- the 256 × 256 three-layer virtual graphics contract, independent dirty state, and transparent backend selection;
- a native planar path and a bounded chunky viewport path;
- deterministic object fallback when hardware sprite capacity is unavailable;
- basic supported MOD import/playback;
- OFS/FFS access through AmigaDOS;
- both floppy and HD launch routes;
- bounded input parsing and guarded native execution.

## 21. Risk register

| ID | Risk | Probability | Impact | Mitigation and trigger |
| --- | --- | --- | --- | --- |
| R-01 | “Replace AmigaOS” is interpreted as bare metal, conflicting with library-backed filesystems and live editing. | High | Critical | Adopt the two-personality hosted/exclusive definition. Any demand for literal bare metal triggers a separate architecture and roadmap. |
| R-02 | The selected chunky viewport and four-plane C2P leave insufficient time or Chip-RAM bandwidth for game logic and audio. | Medium | High | Certify 25 Hz first, benchmark three viewport sizes and two source layouts, record conservative source/LUT/intermediate/plane/display traffic, calibrate real Chip RAM, optimize measured hot paths, skip clean viewports, and make Full/Turbo costs explicit. |
| R-03 | 2 MiB Chip RAM is insufficient or too fragmented after Workbench launch. | High | High | Preflight total/largest blocks, reserve critical buffers at launch, reuse arenas, offer minimal floppy boot, and fail before takeover with a size report. |
| R-04 | Long `Forbid()` sessions or incorrect interrupt/resource handling destabilize AmigaOS. | Medium | Critical | No waiting calls in runtime; avoid long `Disable()`; isolate takeover; record ownership; stress 1,000 transitions; test multiple OS versions and real machines. |
| R-05 | Keyboard/CIA and ProTracker timer ownership conflict. | Medium | Critical | Prototype both in Phase 0, use OS resources to reserve hardware, select distinct or shared interrupt-safe scheduling, and make emergency stop independent of generated code and audio. |
| R-06 | Generated native code crashes after takeover and cannot restore hardware. | Medium | Critical | Strong types, no pointers or imported machine code, guarded operations, budget/stop checks at backward edges, a closed jump-table ABI, validated emission/relocation, differential tests, small interrupt handlers, and a documented residual reboot risk because the 68EC020 provides no process sandbox. |
| R-07 | The bootable disk cannot legally include required AmigaOS files. | High until resolved | Critical | Decide installer/licensing/compatible-component route in Phase 0. No public boot image until resolved. |
| R-08 | The executable and useful example exceed floppy capacity. | Medium | High | Continuous block-budget CI, size maps, embedded compact assets, `-Os` for cold code, optional compressor, and reduced examples/help before features essential to creation. |
| R-09 | ProTracker compatibility expands without bound because historical players disagree on effects. | High | Medium | Declare signatures and effects, use trace-based conformance, warn for unsupported behavior, and version the compatibility profile. |
| R-10 | Malformed MOD or cartridge data causes overflow or corruption. | Medium | Critical | Checked arithmetic, bounds-first parsing, host fuzzing/sanitizers, target corpus, checksums, no native-code sections in cartridges, and no code generation until all source/assets are validated. |
| R-11 | Lua familiarity drives the language toward dynamic general-purpose Lua and erodes predictable native layouts. | High | High | Freeze the Lua compatibility matrix, retain explicit strong types, fixed records/arrays/dictionaries, no universal table or GC, and reject features without a representative-game need. |
| R-12 | A 256-pixel-wide code editor is frustrating. | Medium | High | Test 5–6-pixel fonts early, guarantee horizontal scrolling and shortcuts, keep UI chrome minimal, and consider an optional hosted high-resolution editor only after the core workflow works. |
| R-13 | Emulator success hides real Chip-RAM contention, interrupt behavior, or timing faults. | High | High | Real-hardware gate in every hardware-facing phase; use emulators for regression, never final timing sign-off; use the bounded exclusive-runtime harness for authoritative fine timings. |
| R-14 | Physical floppy writes are slow or unreliable and safe replacement needs extra free space. | High | Medium | Keep projects in memory, show writes, use temporary-file replacement, encourage separate project disks/HD, verify after write, and never autosave to floppy by default. |
| R-15 | AGA playfield/sprite priority, palette-bank, fetch, alignment, or Copper behavior differs from the proposed compositor. | Medium | High | Freeze behavior from register-level tests using Commodore documentation, capture working values and traces, and add composite/palette/sprite-priority golden screens. |
| R-16 | C runtime or GCC output pulls in large or non-stock dependencies. | Medium | High | Inspect link maps from day one, avoid floating point and heavyweight stdio, use a minimal compatible runtime, and pin the toolchain/ABI. |
| R-17 | Hosted module preview cannot acquire all Paula channels without disrupting other applications. | Medium | Low | Make preview cooperative and optional, report contention, and reserve exclusive guarantees for Run mode. |
| R-18 | Disk swapping prompts for the boot volume after MIGA-80 starts. | Medium | Medium | Embed fonts/help needed after launch, avoid overlays initially, pre-open/close required libraries and files, and test single-drive boot-to-project swaps. |
| R-19 | The on-target native compiler is too large or too slow for an edit–run loop. | Medium | High | Prove a minimal emitter in Phase 0, use compact typed IR and bounded arenas, prefer simple passes, measure per-phase time/peak memory/code size, and cache the compiled image only within the trusted current session. |
| R-20 | A code-emitter or relocation bug generates a legal-looking but unsafe 68020 instruction stream. | Medium | Critical | Template-driven emission, independent host disassembly/golden tests, typed relocations, code-range and call-target validation, differential execution against a host IR oracle, and aggressive malformed-IR fuzzing. |
| R-21 | The 68020 executes stale instructions after code generation. | Medium | Critical | Never execute before relocation and validation complete; call the appropriate Exec cache-clear function over the generated range before takeover; include repeated compile/run code-replacement tests on real hardware. |
| R-22 | Virtual objects exceed physical sprite channels or cannot be multiplexed reliably in a crowded scene. | High | High | Expose fixed virtual limits, schedule deterministically, count allocation failures, and provide a bounded planar fallback whose worst-case frame and memory costs are part of the cartridge budget. |
| R-23 | Object fallback changes colors, priority, or pixels relative to the hardware-sprite path. | Medium | High | Share a constrained logical overlay palette, compare both paths to the reference compositor, and force fallback whenever exact hardware mapping is impossible. |
| R-24 | A blitter-assisted path is slower because CPU, display, sprite, audio, and blitter DMA contend for Chip RAM. | High | High | Measure CPU-only, blitter-only stages, and hybrids with all representative DMA active; record CPU work, blitter busy/wait, payload lower bounds, and total frame time; select by exclusive real-hardware total rather than isolated throughput. |
| R-25 | Scroll margins save steady-state work but create visible rebase spikes or consume too much Chip RAM. | Medium | Medium | Compare no margin, ±16, and ±32; measure worst-case refill/rebase separately from averages; cap work per frame or select a smaller guarantee. |
| R-26 | The three-layer abstraction grows into an unpredictable general compositor. | Medium | High | Freeze one baseline composition order, bounded viewport presets, fixed object capabilities, and explicit fallback rules; reject per-pixel alpha, arbitrary layer graphs, and undocumented adaptive behavior. |
| R-27 | The textual-assembly bootstrap and shipping direct encoder diverge into two subtly different backends. | Medium | Critical | Share one low-level m68k instruction model, run behavioral and relocation comparisons, retain selective disassembly goldens, and retire bootstrap-only paths once direct encoding is independently trusted. |
| R-28 | A Musashi-specific behavior masks a generated-code defect or rejects valid target behavior. | Medium | High | Use the typed-IR oracle, UAE, and real hardware as independent levels; add a curated Moira cross-check when useful; investigate disagreements instead of voting by majority. |
| R-29 | Full color-response LUTs bloat the executable or consume excessive floppy and resident memory. | High | Medium | Keep them out of linked literal arrays; compare independently compressed blocks with compact fixed-point descriptors, construct only the selected 16 KiB aligned table in hosted mode, cache the 31 active colors, and require canonical checksum equality with no floating-point dependency. |
| R-29 | The CPU runner grows into a partial Amiga emulator and consumes effort without improving compiler diagnosis. | Medium | Medium | Freeze its scope at CPU, bounded memory, traps, assertions, and traces. Mock runtime calls and keep all chipset, DMA, interrupt, and frame-budget work in UAE/hardware tests. |
| R-30 | Approximate emulator cycle counts are mistaken for stock-A1200 performance. | High | High | Label them regression-only, never convert them directly to milliseconds, and permit performance claims only from EClock/raster measurements on real stock hardware with representative DMA. |
| R-31 | Host assembler, linker, CPU core, or binary tools vary across machines or introduce an unresolved license/reproducibility issue. | Medium | High | Pin revisions and checksums, archive notices, provide a scripted bootstrap, preserve ELF/flat artifacts, and keep developer-specific absolute paths out of all tracked configuration. |
| R-32 | The local runner's synthetic memory addresses or trap protocol accidentally become cartridge ABI. | Low | High | Keep runner maps in test-only configuration, expose services through the versioned native ABI, and require an explicit decision record before any test address or trap number can become normative. |
| R-33 | A small target stack, exception, or teardown fault is misreported as a slow benchmark because the external controller sees only a timeout. | Medium | High | Stream results instead of retaining matrices on stack; require stack-margin checks/canaries, phase markers, explicit cleanup success, and practical crash diagnostics; never treat a timeout as a timing sample. |
| R-34 | Development on expanded machines introduces a hidden Fast-RAM dependency or a DMA-invisible buffer. | Medium | High | Keep `stock_chip_only` in CI and release gates, record allocation domains, reject invalid DMA pointers, run the same cartridge in both tiers, and treat Fast-assisted measurements as optional acceleration only. |

## 22. Decision log required before implementation freeze

The following decisions must be recorded with measurements or prototypes:

1. Exact certified Kickstart/Workbench versions.
2. OFS or FFS format for the boot image and actual payload ceiling.
3. Legal source of boot components.
4. Exact hosted display API and screen depth.
5. Exact AGA dual-playfield, sprite, priority, palette-bank, fetch, modulo, pointer, and Copper settings.
6. Frozen `PLANAR`/`PIXEL`/`OBJECTS` semantics, baseline composition order, API names, and dirty-state rules.
7. Small/Medium/Full chunky dimensions, placement/alignment constraints, default profile, and packed-versus-byte source layout.
8. Four-plane CPU-only versus blitter-assisted versus hybrid C2P implementation, raw Chip-RAM calibration, exclusive timing/interrupt protocol, failure diagnostics, and certified Standard/Turbo envelope.
9. Native planar tile/cache formats, blitter operations, scroll mechanism, margin size, refill policy, and worst-case rebase budget.
10. Virtual object count and dimensions, palette/priority contract, direct/attached/multiplex limits, scheduler order, and fallback representation/cost.
11. Runtime keyboard acquisition and emergency-stop mechanism.
12. CIA-timed versus fractional-VBlank ProTracker scheduler.
13. Selected GCC fork, commit, NDK, C runtime, target ABI, release flags, and host bootstrap procedure.
14. Frozen MIGA Lua grammar, Lua 5.1 compatibility matrix, explicit type rules, numeric semantics, and restricted feature set.
15. Record/array/dictionary layouts, zero-based array indexing, supported key types, hash/probing algorithm, capacities, load factor, and deterministic iteration order.
16. Native 68020 ABI, exact register convention, Boolean/fixed-point representation, runtime-context convention, jump table, relocation model, code arena, stack rules, guards, traps, loop budgets, stop safe points, and cache-clear sequence.
18. Final source, generated-code, packed-cartridge, resident-cartridge, undo, and module limits.
19. Cartridge checksum and compression algorithms.
20. Safe-save behavior when a floppy cannot hold old and temporary copies.
21. Pinned Musashi revision/license/integration method and the trigger for adding a pinned Moira oracle.
22. Local runner memory map, 24-bit/endianness/alignment policy, return/exit protocol, instruction ceiling, runtime-call mocks, trace format, and failure artifacts.
23. Development assembler/linker and syntax, ELF layout, symbol manifest, flat-image extraction, disassembly normalization, and direct-encoder convergence criteria.
24. CPU regression metrics, reviewed kernels, comparison policy, and thresholds that cannot be presented as hardware timing.
25. Fast-assisted allocation policy, fallback behavior, profiler labels, and stock-versus-Fast benchmark results for generated code, stack, chunky source, and CPU-only scratch.
26. Frozen color-response profile IDs and versions, source-data provenance, calibration targets, illuminant/white-point assumptions, color-vision simulation severity, Mega Drive soft-quantization and midtone-bias parameters, transform and gamut-mapping method, LUT generator revision, trademark-safe public names, golden checksums for all eleven profiles, compressed-pack versus dynamic fixed-point strategy per profile, descriptor Q formats and curve tables, response-pack format and codec where used, startup residency policy, selected-table alignment, target construction path, stock-68020 latency/size results, and no-floating-point link-map gate.

## 23. Recommended first vertical slice

After Phase 0, the first end-to-end slice SHOULD be deliberately small:

1. Boot from ADF into the hosted shell.
2. Open a hard-coded 20-line strongly typed MIGA Lua example in a minimal editor.
3. Compile it on the A1200 directly to guarded native 68020 code, validate/relocate it, and synchronize the instruction cache.
4. Enter exclusive mode.
5. Read joystick input.
6. Scroll a native planar tile background without C2P.
7. Update a small procedural chunky viewport independently of that background.
8. Move one virtual object through the hardware-sprite path, then force the same object through fallback and verify identical composition.
9. Play one validated looping MOD while the three graphics paths are active.
10. Stop with the protected key chord.
11. Restore the editor at the same cursor position.
12. Save and reload the single-file cartridge through AmigaDOS.

This slice exercises every architectural boundary before richer editor tools or language features make failures harder to isolate.

## 24. References and technical basis

These sources inform the assumptions above; they are not runtime dependencies:

- [MIGA-80 Local 68020 Tooling](./MIGA-80-local-68020-tooling.md), defining the fast generated-code test loop and its boundary with UAE and real hardware.
- [Musashi — Motorola 680x0 emulator](https://github.com/kstenerud/Musashi), the proposed primary embedded 68EC020 core for host compiler tests.
- [Moira — Motorola 68000/68010/68EC020/68020 emulator](https://dirkwhoffmann.github.io/Moira/), the proposed optional independent CPU oracle.
- [GNU Binutils](https://sourceware.org/binutils/) and [vasm](https://sun.hasenbraten.de/vasm/), candidate development assembly, link, extraction, and inspection tools.
- [AmigaOS Documentation Wiki — Exec Tasks](https://wiki.amigaos.net/wiki/Exec_Tasks), especially the behavior of `Forbid()`, `Permit()`, `Disable()`, `Enable()`, and the warning against long disabled sections.
- [AmigaOS Documentation Wiki — Exec processor and cache control](https://wiki.amigaos.net/wiki/Exec_Tasks#Processor_and_Cache_Control), for `CacheClearE()`, `CacheClearU()`, the 68020 instruction cache, and generated/self-modifying code synchronization.
- [AmigaOS Documentation Wiki — Classic Graphics Primitives](https://wiki.amigaos.net/wiki/Classic_Graphics_Primitives), covering `View`, dual playfields, double buffering, direct blitter coordination, and display construction.
- [AmigaOS Documentation Wiki — Graphics Primitives](https://wiki.amigaos.net/wiki/Graphics_Primitives), including `LoadView()`, `OwnBlitter()`, `WaitBlit()`, and `DisownBlitter()`.
- [AmigaOS Documentation Wiki — Basic Input and Output Programming](https://wiki.amigaos.net/wiki/Basic_Input_and_Output_Programming), for AmigaDOS locks and file-handle operations.
- [Commodore Amiga Hardware Reference Manual mirror — Forming a Dual-Playfield Display](https://amigadev.elowar.com/read/ADCD_2.1/Hardware_Manual_guide/node0078.html), for odd/even bitplane grouping, transparency, palette grouping, and playfield priority.
- [Commodore AA Chip Set Functional Specification mirror](https://shanson.com/spencer/Amiga-AA-Chipset.pdf), for AGA's eight-bitplane, palette-bank, fetch-mode, and `PF2OF` extensions.
- [MIGA-80 Color-Response Palette Studies](./color-response-palettes/README.md), containing the common 256-color RGB12 sample grid, all nine comparison PNGs, the reproducible generator, dates, assumptions, and source links.
- [ITU-R BT.470-6 — Conventional Television Systems](https://www.itu.int/dms_pubrec/itu-r/rec/bt/r-rec-bt.470-6-199811-s!!pdf-e.pdf), especially the historical NTSC and 625-line PAL/SECAM primary chromaticities and reference whites.
- [Kodak Professional PORTRA 400 technical data](https://www.kodakprofessional.com/sites/default/files/wysiwyg/pro/resources/e4050_portra_400.pdf) and [EKTACHROME E100 technical data](https://www.kodakprofessional.com/sites/default/files/wysiwyg/pro/resources/e4000_ektachrome_100.pdf), including characteristic, spectral-sensitivity, and dye-density curves.
- [ILFORD HP5 PLUS technical information](https://www.ilfordphoto.com/amfile/file/download/file/1903/product/691/), including its panchromatic spectral-sensitivity and characteristic curves.
- [Polaroid Color 600 product specification](https://www.polaroid.com/en_gb/products/color-600-instant-film) and [Lomography history](https://www.lomography.com/about/history), to be supplemented by calibrated target captures because the published product material is not sufficient to derive complete colorimetric transforms.
- [Sokolov and Sudravskii — *Colour Amateur Television Receiver “Tsvet-2”* (1963)](http://ca.cryptocom.ru/tmpfiles/mrb_0469.djvu), an original-period technical source for reconstructing the experimental Soviet OSKM system; later historical summaries may be used only to locate and cross-check primary material.
- [Machado, Oliveira, and Fernandes — *A Physiologically-based Model for Simulation of Color Vision Deficiency* (2009)](https://doi.org/10.1109/TVCG.2009.113), for the deutan and protan simulation model and matrices.
- [SEGA Genesis Software Manual](https://segaretro.org/images/9/95/GenesisSoftwareManual.pdf), for the console's 3-bit-per-channel RGB coding, and [SEGA corporate history](https://www.sega.jp/history/companyTimeline/en/), for the original 1988 Mega Drive reference date.
- [NXP/Freescale MC68020/MC68EC020 User's Manual](https://www.nxp.com/docs/en/data-sheet/MC68020UM.pdf), for the certified code generator's instruction set, cache, exception, and timing model.
- [Lua 5.1 Reference Manual](https://www.lua.org/manual/5.1/manual.html) and [official Lua 5.1 source](https://www.lua.org/source/5.1/), used to define and test the documented boundary between familiar Lua syntax and MIGA Lua's static semantics.
- [AmigaPorts m68k-amigaos-gcc](https://github.com/AmigaPorts/m68k-amigaos-gcc), the preferred starting point for the cross-compilation toolchain.
- [8bitbubsy pt2-clone](https://github.com/8bitbubsy/pt2-clone), a useful declared behavioral comparison point for ProTracker replay tests; reuse of code would require a separate license review.

Before direct custom-chip code is frozen, the project SHOULD archive the exact Commodore/NDK documentation revision used and cite register pages in a dedicated hardware design note.
