# MIGA-80 Four-Plane C2P Reference and Benchmark

**Status:** Reference, pair-LUT C99/68020, table-free mask32 C99/68020, staged blitter-publication control, and the report-format-3 exclusive display/DMA/memory wrapper implemented; native, 204-case stock FS-UAE, and 260-case Fast-assisted FS-UAE protocols pass

**Decision authority:** Correctness is byte-exact on the host and under FS-UAE. Layout and performance decisions remain blocked on a physical stock PAL A1200.

The animated runtime now uses mask32 assembly by default and includes a
selectable Kalms adaptation. Its same-ADF, double-buffered cube comparison is
documented separately in [Runtime C2P comparison](MIGA-80-c2p-runtime-comparison.md).
The historical benchmark matrices below retain their original scope and results.

## 1. Scope

This is the first C2P benchmark aligned with the three-layer graphics architecture. It converts only the transparent `PIXEL` viewport into the four PF1 planes. The native `PLANAR` base remains in PF2 and is never converted.

The matrix covers:

| Profile | Pixels | Packed source | Byte source | Four-plane output |
| --- | ---: | ---: | ---: | ---: |
| Small | 160 × 128 | 10 KiB | 20 KiB | 10 KiB |
| Medium | 192 × 160 | 15 KiB | 30 KiB | 15 KiB |
| Full | 256 × 256 | 32 KiB | 64 KiB | 32 KiB |

All viewports are centered at byte-aligned x coordinates in the 256 × 256 display. The scalar and pair-LUT APIs accept arbitrary non-zero widths divisible by eight and explicit source/destination strides. The mask32 APIs require widths divisible by 32. All three product profiles satisfy both contracts; none is hard-coded into a converter.

## 2. Conversion Contract

`src/graphics/c2p4_reference.h` exposes packed4 and byte4 variants. For packed4, even x uses the high nibble and odd x the low nibble. For byte4, only the low nibble is significant.

The four output pointers have this logical order:

| C2P4 destination | AGA destination in MIGA-80 | Source color bit |
| ---: | --- | ---: |
| 0 | BPL1 / PF1 | 0 |
| 1 | BPL3 / PF1 | 1 |
| 2 | BPL5 / PF1 | 2 |
| 3 | BPL7 / PF1 | 3 |

Bit 7 is the leftmost pixel in each output byte. The routines allocate no memory and require non-overlapping source and destinations.

## 3. Implementations

### 3.1 Scalar reference

`src/graphics/c2p4_reference.c` uses explicit pixel and bit loops. It exists for review and correctness, not timing. It is exercised natively but deliberately excluded from the target performance report.

### 3.2 Pair-LUT C99 candidate

`src/graphics/c2p4_lookup.c` builds a persistent 256-entry table. Each 32-bit entry transposes two four-bit pixels into their four destination-byte positions. Four lookups and shifts produce eight pixels. The table costs 1 KiB and is built outside the frame loop.

This remains portable C99 and supplies a readable optimized candidate against which the assembly can be compared.

### 3.3 Pair-LUT 68020 candidate

`src/graphics/c2p4_m68k.S` contains separate packed4 and byte4 inner loops. They preserve the C ABI's non-volatile registers, process one eight-pixel group per iteration, write directly to four non-interleaved destination planes, and support row gaps through source and destination skip values.

`src/graphics/c2p4_m68k.c` validates the public arguments before entering assembly. Non-m68k builds call the C99 lookup implementation, allowing the same API contract to run under host sanitizers; the Amiga benchmark is the differential execution test for the real assembly cores.

### 3.4 Table-free 32-pixel transpose

`src/graphics/c2p4_mask32.c` supplies a portable C99 implementation of the five merge stages and explicit big-endian loads/stores. It is both a readable algorithm reference and a native-host oracle for the assembly implementation.

`src/graphics/c2p4_m68k.S` also contains dedicated packed4 and byte4 mask32 loops. Each iteration produces 32 pixels with a five-stage register merge network and four longword plane writes. The packed4 path loads its 16 source bytes directly into four data registers. The byte4 path first compacts 32 low nibbles into the same four-register form. Neither path reads the 1 KiB pair table.

The final register-to-plane mapping is covered by byte-exact host vectors and by the complete target-side compositor → C2P4 → AGA-decoder differential. The public wrappers reject widths that are not multiples of 32 before modifying the destination.

### 3.5 Staged blitter-publication control

The `pair_lut_m68k_blit` backend converts into four temporary Chip-RAM planes and uses `graphics.library/BltBitMap()` with minterm `0xC0` to copy the viewport into PF1, followed by `WaitBlit()` before validation or freeing memory. This follows the documented plain-copy operation and synchronization contract.

This is intentionally a control experiment, not CPU3BLIT1 and not a claim that the blitter accelerates C2P. It adds one full four-plane write/read/copy path and viewport-sized scratch memory. A true hybrid must move an actual transpose/merge stage to the blitter and overlap useful CPU work.

## 4. Native Correctness Suite

Run:

```sh
gmake c2p4-test
```

The suite checks:

- the independent eight-pixel golden `F 8 4 2 1 0 5 A`, which produces plane bytes `8A 91 A2 C1`;
- identical packed4 and noisy-high-nibble byte4 output;
- scalar, C/LUT, mask32 C99, and both public m68k-wrapper equivalence paths;
- padded source/destination strides and preserved destination padding;
- validation failures without destination modification;
- all three viewport profiles;
- exact 256 × 256 canonical images after centered PF1 placement over a deterministic native PF2 base;
- compositor → C2P4 → AGA decoder equality for every layout/backend path available on the host.

The host m68k wrapper falls back to C, so assembly instruction/ABI correctness is additionally gated by the target benchmark's full canonical comparisons.

## 5. Target Benchmark Protocol

Run:

```sh
gmake c2p4-benchmark-fs-uae
```

The target builds an `-O2`, 68020, soft-float Hunk executable and opens an Intuition-managed PAL 256 × 256 eight-plane dual-playfield screen. Display DMA remains active. Source buffers and staged planes use `AllocMem(MEMF_CHIP)` and are rejected unless `TypeOfMem()` confirms Chip RAM. Each of the 24 cases combines three profiles, two source layouts, and four target candidates:

- `pair_lut_c99`;
- `pair_lut_m68k`;
- `mask32_m68k`;
- `pair_lut_m68k_blit`.

For each case it performs one warm-up and three measured samples. `WaitTOF()` is called once before a sample and outside the complete E-Clock bracket. This keeps the existing hosted run reproducible enough for integration, but it is not a fine-timing design: the wait dominates wall-clock automation time, AmigaOS can still schedule inside a measured interval, and a timeout cannot by itself distinguish a slow case from a deadlock or crash. A sample:

1. reconstructs the complete deterministic source;
2. performs one quarter-screen worth of deterministic pixel writes using the layout's natural operation; byte4 construction and writes deliberately fill the ignored high nibble so the target assembly path must mask it correctly;
3. converts the complete viewport;
4. for the staged backend, publishes the temporary planes with `BltBitMap()` and waits for completion;
5. records independently bracketed phases and complete-pipeline E-Clock ticks.

After the last sample, the harness composes the logical oracle, decodes the eight live AGA planes, requires exact equality of all 65,536 canonical bytes, and emits matching FNV-1a checksums. Reports are streamed case by case so a timeout retains useful diagnostics; the final `result=pass` appears only after resource cleanup and successful screen closure.

When the matrix first grew from 18 to 24 cases, an intermediate harness retained `BenchmarkResult results[24]` on the process stack. This consumed 2,976 bytes before the rest of `main()` and its callees. Several runs printed all case records but timed out before the footer and screen closure. Reusing and streaming one 124-byte result allowed the same matrix to complete and clean up. That is strong evidence of default-stack pressure, but not a captured exception and therefore not proof of the exact failure mode. The implementation keeps the bounded form; the exclusive successor must measure stack margin and provide explicit crash diagnostics.

The report is stored at `build/reports/c2p4-fs-uae.txt` and validated with the common graphics schema. The reported memory footprint covers the eight display planes, selected source, backend state, and scratch where applicable. Only pair-LUT backends charge the 1 KiB table. Oracle-only harness buffers are excluded from the proposed runtime working set. The executable, map, symbol-size list, and disassembly remain under `build/benchmark/c2p4/` and `build/reports/`.

Each case also reports `lookup_traffic_bytes`, `minimum_chip_traffic_bytes`, `display_plane_fetch_bytes_per_video_frame`, `video_hz`, and `timing_scope=hosted_cooperative`. The traffic value is a payload lower bound for source construction, the quarter-screen pixel-write workload, source reads, output writes, lookup reads, and any staged publication. It excludes instruction fetch, memory refresh, bus transaction granularity, Copper, sprites, audio, allocator activity, and DMA arbitration. It must therefore never be presented as measured bus bandwidth.

## 6. Initial FS-UAE Protocol Result

The passing 24-case run reported an E-Clock rate of 709,379 Hz, timer-bracket overhead of 40 ticks, active display and system-owned sprite DMA, inactive audio DMA, and a 28,375-tick Standard 25 Hz budget. The following values are medians over three samples:

| Profile | Layout | C/LUT C2P | 68020/LUT C2P | mask32 C2P | mask32 vs LUT | LUT total | mask32 total |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 160 × 128 | packed4 | 35,222 | 25,340 | 10,465 | −58.7% | 107,403 | 92,431 |
| 160 × 128 | byte4 | 48,747 | 39,759 | 40,580 | +2.1% | 118,455 | 119,185 |
| 192 × 160 | packed4 | 52,295 | 37,773 | 14,961 | −60.4% | 161,446 | 138,201 |
| 192 × 160 | byte4 | 72,271 | 59,217 | 60,158 | +1.6% | 177,617 | 178,542 |
| 256 × 256 | packed4 | 111,157 | 81,045 | 32,205 | −60.3% | 342,467 | 293,668 |
| 256 × 256 | byte4 | 153,234 | 126,033 | 126,819 | +0.6% | 379,140 | 379,514 |

The pair-LUT assembly candidate reduced C2P ticks relative to C/LUT in every FS-UAE case. The mask32 packed4 path then reduced the assembly/LUT C2P phase by about 59–60%, and the complete synthetic pipeline by about 14%. Mask32 byte4 was within roughly 2% of the assembly/LUT path. This makes packed4 plus a wide transpose the strongest current candidate, but not a selected runtime contract. Byte4 still has cheaper direct pixel stores, and emulator timing cannot resolve the hardware trade-off.

Static target inspection gives one plausible reason for the byte4 result. The mask32 packed4 assembly core is 260 bytes in total and its repeated block is approximately 200 bytes. The byte4 core is 528 bytes and its repeated block is approximately 468 bytes because it must compact 32 low nibbles before transposition. That repeated region exceeds the 68EC020's 256-byte instruction cache, while the packed4 region can fit by capacity. This is not cycle evidence: alignment and direct-mapped conflicts still require stock-hardware measurement. It does make a shorter byte4 compaction loop a required candidate rather than treating the current implementation as final.

The staged blitter copy remained a negative control. At 256 × 256 its median publication region was 16,711 ticks for packed4 and 16,508 ticks for byte4, while its total was 359,345 and 394,950 ticks respectively. It increased memory and complete time because it copies already converted data rather than performing a merge stage.

Every sample missed the nominal 25 Hz budget in FS-UAE. These numbers are not cycle-accurate performance evidence and must not be converted into a product claim. They validate output equivalence, report plumbing, workload scaling, assembly execution, blitter synchronization, and the ability to distinguish costs.

### 6.1 Chip-RAM traffic warning

An informal stock-A1200 observation places brute-force CPU writes to Chip RAM near 6 MB/s. This is a valuable warning, not yet a benchmark result: the exact loop, access width, alignment, display state, DMA load, timing method, and distinction between decimal MB/s and MiB/s are unknown. Read traffic and mixed read/write traffic are not equivalent to write-only throughput.

For the Full profile, the current payload model produces these lower bounds at 25 complete updates per second:

| Backend/layout | Bytes per update | Payload rate |
| --- | ---: | ---: |
| mask32 packed4 direct | 131,072 | 3.125 MiB/s |
| mask32 byte4 direct | 180,224 | 4.297 MiB/s |
| pair-LUT packed4 direct | 262,144 | 6.250 MiB/s |
| pair-LUT byte4 direct | 311,296 | 7.422 MiB/s |
| staged pair-LUT packed4 | 327,680 | 7.813 MiB/s |
| staged pair-LUT byte4 | 376,832 | 8.984 MiB/s |

Independently, fetching all eight 256 × 256 display planes accounts for 65,536 payload bytes per 50 Hz video frame, or another 3.125 MiB/s. These rates cannot simply be compared with or added to the anecdotal write figure as if they shared one efficiency. They do show why eliminating LUT reads, scratch passes, and unchanged work is architectural rather than cosmetic.

The report-format-3 harness now implements aligned byte/word/long reads and writes, unrolled longword paths, copy and read-modify-write loops, source and destination offsets `+1`, `+2`, and `+3`, and optional Fast-source variants. Its three extended-memory profiles cover fully blanked, active display/Copper/sprite/audio, and the same active state with fair blitter overlap; the six original core kernels still cover all seven additive DMA profiles. Exact source/destination guards, observable return checksums, separate total/Chip payload traffic, CIA timing, and generated disassembly are part of the protocol. Phase 0 still requires repeated distributions from a physical stock A1200 before any rate is authoritative.

## 7. Open Work Before a Decision

This tranche does not yet select packed4, byte4, or any backend. The next C2P4 work is:

1. run the implemented exclusive baseline on a physical stock machine before interpreting C2P rates; it already covers blanked/active display, seven additive DMA profiles, all three viewport sizes, both source layouts, and the real pair-LUT/mask32 assembly cores;
2. collect separately labelled optional Fast-equipped runs for the implemented source/LUT/stack tier; never substitute them for stock certification;
3. shorten or restructure the byte4 compaction core so its repeated instruction footprint can be compared with the current approximately 468-byte loop;
4. implement a genuine CPU/blitter merge split such as a C2P4 adaptation of CPU3BLIT1, with explicit intermediate layout and direct-register ownership documented;
5. add aligned dirty-rectangle and no-change workloads;
6. add double-buffered or Copper-safe publication rather than direct writes to the visible PF1 planes;
7. add the minimal MIGA-80 Level-3 VBlank/raster path, exception capture and visible persistent phase marker needed for `exclusive_runtime_frame`;
8. stress hosted-to-exclusive restoration and forced failures, then repeat with representative native-planar, sprite, blitter, and Paula traffic;
9. run longer distributions on a stock 2 MiB PAL A1200 and freeze only the smallest profile/layout contract that meets headroom.

The stock target is built and exercised with `gmake exclusive-graphics-benchmark-fs-uae`; the optional comparison uses `gmake exclusive-graphics-benchmark-fs-uae-fast`. The 204-case baseline comprises 120 raw-memory and 84 C2P4 cases. A complete Fast allocation adds 32 source-reading raw and 24 C2P4 cases under blanked and fair-blitter states, producing 260 total. Paula contributes four real muted DMA channels, seven controlled sprites supply a declared 6,328-byte minimum fetch per video frame, and the fair/hog blitter cases overlap adaptive direct-register loads with the CPU kernel. Direct PF1 writes are still not safe publication. Both reports therefore remain `protocol_only` even though their output hashes, placement/traffic metadata, DMA/interrupt restoration, and guarded-stack invariants pass.

The hosted and exclusive harnesses have different jobs. Hosted cooperative runs remain the frequent correctness, API, allocation, and restoration regression. Fine performance runs use a bounded exclusive section after all files and allocations are prepared, with no DOS calls or general OS waits inside it. The two exclusive scopes and their takeover, interrupt, stack, crash, and Chip-RAM calibration contracts are defined in [Exclusive Graphics Benchmark Plan](./graphics-exclusive-benchmark.md). Takeover must use documented ownership and a small controlled interrupt path; it must not be implemented as an unbounded blind `Disable()` region.

The blitter shares Chip-RAM bandwidth with display and other DMA users, so a hybrid is acceptable only if complete frame time improves under realistic contention. The classic CPU3BLIT1 technique performs early merges on the CPU and delegates later word-oriented merges to the blitter; its scratch layout and two-direction blits need a dedicated implementation rather than treating a copy as conversion.

Primary implementation references:

- [Amiga Hardware Reference Manual — Blitter Operations and System DMA](https://amigadev.elowar.com/read/ADCD_2.1/Hardware_Manual_guide/node012B.html)
- [Amiga ROM Kernel Reference Manual — Copying Rectangular Areas](https://amigadev.elowar.com/read/ADCD_2.1/Libraries_Manual_guide/node0366.html)
- [graphics.library/WaitBlit autodoc](https://amigadev.elowar.com/read/ADCD_2.1/Includes_and_Autodocs_3._guide/node0339.html)
- [Kalms C2P tutorial — CPU/blitter merge ordering](https://amycoders.org/sources/c2ptut.html)
- [Motorola M68020 User's Manual](https://www.ele.uva.es/~jesman/BigSeti/ftp/Microprocesadores/Motorola/68020um.pdf)
