# MIGA-80 color-response palette studies

These plates compare the same 256-color sample of MIGA-80's logical 12-bit RGB gamut after each proposed photographic-film, historical-video, color-vision, or console-inspired response. They are design references for the palette system described in the main specification, not claims of exact manufacturer-approved film simulation or individual human perception.

## Reading the grid

The full MIGA-80 color space contains 4,096 logical values (`0xRGB`, four bits per component). The generator builds a 24-bit output for every one of those values, then displays this shared 256-color subset:

- red and green use levels `0, 2, 4, 6, 9, 11, 13, 15`;
- blue uses levels `0, 5, 10, 15`;
- the four 8 × 8 quadrants are the four blue levels, ordered top-left, top-right, bottom-left, bottom-right;
- within each quadrant, red increases from left to right and green from top to bottom.

This gives `8 × 8 × 4 = 256` samples distributed regularly through RGB12. Every plate preserves this exact ordering, so corresponding cells can be compared directly.

| Stable `color_response` key | Kind | Response target | Reference year | Image |
| --- | --- | --- | ---: | --- |
| `RESPONSE_NEUTRAL` | Source | Amiga RGB12, vanilla | 1985 | [`00-amiga-rgb12-vanilla-1985.png`](./00-amiga-rgb12-vanilla-1985.png) |
| `RESPONSE_WARM_NEGATIVE` | Film | Kodak Professional PORTRA 400 | 2010 | [`01-kodak-portra-400-2010.png`](./01-kodak-portra-400-2010.png) |
| `RESPONSE_COOL_REVERSAL` | Film | Kodak Professional EKTACHROME E100 | 2018 | [`02-kodak-ektachrome-e100-2018.png`](./02-kodak-ektachrome-e100-2018.png) |
| `RESPONSE_INSTANT_600` | Film | Polaroid Color 600 | 1981 | [`03-polaroid-color-600-1981.png`](./03-polaroid-color-600-1981.png) |
| `RESPONSE_MUTED_METROPOLIS` | Film | Lomography LomoChrome Metropolis | 2019 | [`04-lomochrome-metropolis-2019.png`](./04-lomochrome-metropolis-2019.png) |
| `RESPONSE_PANCHRO_MONO` | Film, B&W | ILFORD HP5 PLUS | 1989 | [`05-ilford-hp5-plus-1989.png`](./05-ilford-hp5-plus-1989.png) |
| `RESPONSE_NTSC_1953` | Video | NTSC 1953 | 1953 | [`06-ntsc-1953.png`](./06-ntsc-1953.png) |
| `RESPONSE_PAL_SECAM_625` | Video | 625-line PAL/SECAM | 1967 | [`07-pal-secam-625-1967.png`](./07-pal-secam-625-1967.png) |
| `RESPONSE_OSKM_1960` | Video | Soviet OSKM (ОСКМ) | 1960 | [`08-oskm-1960.png`](./08-oskm-1960.png) |
| `RESPONSE_DEUTAN_2009` | Color vision | Deutan simulation, Machado model | 2009 | [`09-deutan-machado-2009.png`](./09-deutan-machado-2009.png) |
| `RESPONSE_PROTAN_2009` | Color vision | Protan simulation, Machado model | 2009 | [`10-protan-machado-2009.png`](./10-protan-machado-2009.png) |
| `RESPONSE_VIOLET_DRIVE` | Console-inspired | Mega Drive midtone-purple response | 1988 | [`11-megadrive-1988.png`](./11-megadrive-1988.png) |

## Amiga RGB12 — vanilla (1985)

The unfiltered reference expands each 4-bit component by nibble replication (`0xRGB -> 0xRRGGBB`). The 1985 date refers to the original Amiga RGB12 palette convention; an AGA machine can instead program each selected color with a full 24-bit value.

![Vanilla Amiga RGB12 256-color grid](./00-amiga-rgb12-vanilla-1985.png)

## Kodak Professional PORTRA 400 (2010)

Soft toe and shoulder, restrained saturation, warm highlights, and a comparatively gentle blue response. The year identifies the unified PORTRA 400 formulation introduced in 2010, replacing the former NC and VC variants.

![Kodak Professional PORTRA 400 response](./01-kodak-portra-400-2010.png)

## Kodak Professional EKTACHROME E100 (2018)

A reversal-film response with a firmer characteristic curve, clean highlights, and moderately reinforced blue/cyan and saturation. The year identifies the current E100 reintroduction, not the much older EKTACHROME family name.

![Kodak Professional EKTACHROME E100 response](./02-kodak-ektachrome-e100-2018.png)

## Polaroid Color 600 (1981)

Lifted blacks, compressed highlights, lower chroma, and a warm instant-print balance. The reference year is the introduction of Polaroid's ASA 600 integral-film system; current Color 600 chemistry is not assumed to be identical to the 1981 material.

![Polaroid Color 600 response](./03-polaroid-color-600-1981.png)

## Lomography LomoChrome Metropolis (2019)

Muted chroma, strong contrast, cool green/cyan shadows, and warmer highlights, following the stock's stated “muted tones” direction. LomoChrome Metropolis was introduced in 2019.

![Lomography LomoChrome Metropolis response](./04-lomochrome-metropolis-2019.png)

## ILFORD HP5 PLUS (1989)

Neutral monochrome output derived from a panchromatic weighting, with a modest toe and shoulder. The RGB weights intentionally differ from ordinary display luma so the result can follow HP5 PLUS's broad spectral sensitivity more closely. HP5 PLUS was introduced in 1989; the earlier HP5 dates to 1976.

![ILFORD HP5 PLUS response](./05-ilford-hp5-plus-1989.png)

## NTSC 1953 (1953)

The static colorimetric component uses the original NTSC 1953 primaries and Illuminant C reference white, adapted to the PNG's D65 sRGB display space. It does not add composite-video artifacts such as hue error, dot crawl, or bandwidth-dependent bleeding.

![NTSC 1953 response](./06-ntsc-1953.png)

## 625-line PAL/SECAM (1967)

PAL and SECAM differ as transmission systems, but this shared palette response uses the 625-line primary chromaticities and D65 white documented by ITU-R BT.470. The 1967 date marks their regular public introduction in Europe. Line alternation, SECAM's sequential chroma, and other spatial or temporal behavior cannot be represented by a pointwise palette LUT.

![625-line PAL and SECAM response](./07-pal-secam-625-1967.png)

## Soviet OSKM / ОСКМ (1960)

OSKM—`Одновременная совместимая система с квадратурной модуляцией`, or “simultaneous compatible system with quadrature modulation”—was the experimental Soviet 625/50 color system broadcast in Moscow from 1960 before SECAM was adopted. The plate treats it as a period NTSC-derived, U/V-coded system and applies a conservative asymmetric chroma contraction. That contraction is explicitly a reconstruction assumption until primary receiver measurements or sufficiently detailed period specifications are available.

![Soviet OSKM response](./08-oskm-1960.png)

## Deutan color-vision simulation (2009 model)

This mode previews the complete deutan red–green deficiency endpoint using the linear-RGB matrix published with the physiologically based Machado–Oliveira–Fernandes model. Deutan deficiencies are the most prevalent color-vision-deficiency family. The 2009 date belongs to the simulation model, not to the condition.

![Deutan color-vision simulation](./09-deutan-machado-2009.png)

## Protan color-vision simulation (2009 model)

This mode previews the corresponding complete protan red–green deficiency endpoint from the same model. Protan deficiencies are the other common red–green family and notably alter perceived red brightness. As with the deutan plate, one fixed transform cannot reproduce every observer or severity.

![Protan color-vision simulation](./10-protan-machado-2009.png)

These two modes are simulations for design review, not automatic accessibility corrections. MIGA-80 SHOULD use them to expose confusing palette pairs while keeping contrast, symbols, outlines, and redundant non-color cues as the actual accessibility controls.

## Mega Drive-inspired midtone purple (1988)

Each RGB12 component is pulled only 28% toward the nearest value in the Mega Drive's 3-bit-per-channel RGB vocabulary. This restrained posterization evokes the console's stepped palette without reducing MIGA-80 to 512 colors: all 4,096 logical inputs remain available to the profile. MIGA-80 then adds a luminance-gated violet bias: red and blue rise and green contracts around the middle of the tone range, while the bias mathematically falls to zero at black and white. The 1988 date is the Japanese Mega Drive launch year.

The violet tendency is an explicit MIGA-80 art-direction choice inspired by the appearance of many Mega Drive titles; it is not presented as a measured property of every console VDP, encoder, cable, or display.

![Mega Drive-inspired midtone-purple response](./11-megadrive-1988.png)

## Reproduction and fidelity

Run the dependency-free generator from the repository root:

```sh
python3 documentation/color-response-palettes/generate_palettes.py
```

The generator evaluates all 4,096 RGB12 inputs for each profile, validates 8-bit output bounds, and writes sRGB-tagged, 516 × 516 RGB PNGs. Its matrices, tone curves, gamut clipping, panchromatic weighting, historical-video colorimetry, color-vision matrices, and Mega Drive treatment are visible in [`generate_palettes.py`](./generate_palettes.py).

The present film transforms are reproducible visual baselines. A release-quality claim of physical fidelity requires digitized manufacturer spectral-sensitivity, characteristic, and dye-density curves where available, plus controlled color-target captures for stocks whose makers do not publish enough data. The target stock, processing chemistry, illuminant, print or scanner path, reference white, and fitting error must all be recorded. A static 4,096-entry palette LUT can reproduce color and tone mapping, but not grain, halation, local exposure effects, optical flare, chroma delay, scanlines, or noise.

## Target storage and lookup

The floating-point transforms in this generator are strictly host-side build operations. Embedding all twelve tables in the MIGA-80 executable would cost 144 KiB as packed RGB24 or 192 KiB as aligned 32-bit entries, which is too large relative to the binary and floppy budgets.

Two alternatives must be benchmarked on the stock A1200. The first stores each canonical table as an independently compressed block in an external, versioned response pack. The second stores compact fixed-point descriptors—initially Q14 channels, signed Q13 matrices, and small integer curve tables—and reconstructs all 4,096 entries once when the profile is selected. The fixed-point path is accepted only if its table is byte-identical to the canonical output, has the same checksum, and is fast enough for an interactive profile change.

Either route constructs immutable aligned `0x00RRGGBB` data before runtime.
Profiles referenced by a cartridge also receive precomputed projections of the
31 active opaque palette entries, so `color_response` can switch dynamically by
publishing a small cache. No matrix, gamma, interpolation, decompression, table
generation, or floating-point work is permitted after exclusive takeover. The
current implementation embeds exact projections for the demo's fixed 16-color
RGB study palette; general runtime palette editing still requires the full
LUT/cache path. The study palette traverses red, yellow, green, cyan, blue and
magenta to make response differences visible in a compact two-dimensional test
field.
The full comparison and hybrid fallback policy are specified in [section
11.4.2 of the main specification](../MIGA-80-specification-and-roadmap.md#1142-lut-storage-and-integer-only-runtime-contract).

## Sources

- [ITU-R BT.470-6, *Conventional Television Systems*](https://www.itu.int/dms_pubrec/itu-r/rec/bt/r-rec-bt.470-6-199811-s!!pdf-e.pdf) — NTSC and 625-line PAL/SECAM primaries and reference whites.
- [Kodak Professional PORTRA 400 technical data, E-4050](https://www.kodakprofessional.com/sites/default/files/wysiwyg/pro/resources/e4050_portra_400.pdf) — characteristic, spectral-sensitivity, and dye-density curves.
- [Kodak Professional EKTACHROME E100 technical data, E-4000](https://www.kodakprofessional.com/sites/default/files/wysiwyg/pro/resources/e4000_ektachrome_100.pdf) — spectral-sensitivity and dye-density curves.
- [Kodak's 2018 EKTACHROME availability announcement](https://www.kodak.com/en/company/press-release/ektachrome-film-begins-shipping/) and [the 2010 PORTRA 400 announcement](https://www.prnewswire.com/news-releases/kodak-continues-to-enhance-award-winning-professional-film-portfolio-102848974.html) — formulation reference years.
- [Polaroid Color 600 product information](https://www.polaroid.com/en_gb/products/color-600-instant-film) and [the 1981 *Chemical & Engineering News* report](https://pubs.acs.org/doi/10.1021/cen-v059n025.p052) — current material and original 600-system date.
- [Lomography history](https://www.lomography.com/about/history) — the 2019 LomoChrome Metropolis introduction and stated muted-color direction.
- [ILFORD HP5 PLUS technical information](https://www.ilfordphoto.com/amfile/file/download/file/1903/product/691/) and [ILFORD history](https://www.ilfordphoto.com/about-us/history/) — panchromatic sensitivity and 1989 introduction.
- [Sokolov and Sudravskii, *Colour Amateur Television Receiver “Tsvet-2”* (1963), period scan](http://ca.cryptocom.ru/tmpfiles/mrb_0469.djvu) — primary-period technical material for OSKM reconstruction.
- [Machado, Oliveira, and Fernandes, *A Physiologically-based Model for Simulation of Color Vision Deficiency* (2009)](https://doi.org/10.1109/TVCG.2009.113) — validated model and matrices for the deutan and protan simulations.
- [Global prevalence study of congenital color-vision deficiency](https://www.aaojournal.org/article/S0161-6420%2825%2900465-8/fulltext) — deutan is the most prevalent family, followed by protan.
- [SEGA Genesis Software Manual](https://segaretro.org/images/9/95/GenesisSoftwareManual.pdf) — original development documentation for the console's 3-bit-per-channel RGB color coding.
- [SEGA corporate history](https://www.sega.jp/history/companyTimeline/en/) — Japanese Mega Drive release in October 1988.

Film and product names identify response targets only. They do not imply manufacturer endorsement.
