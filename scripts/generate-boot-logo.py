#!/usr/bin/env python3
"""Encode the supplied logo at native size; no resizing or palette reduction."""
import hashlib
from pathlib import Path
import sys
from PIL import Image

source, output = map(Path, sys.argv[1:])
im = Image.open(source).convert('RGB')
if im.size != (196, 43):
    raise SystemExit('The boot logo must be 196 x 43 pixels (native layout).')
colors = sorted(set(im.getdata()))
if len(colors) > 4 or colors[0] != (0, 0, 0):
    raise SystemExit('The boot logo must have a black background and at most four colors.')
indices = bytes(colors.index(pixel) for pixel in im.getdata())
output.parent.mkdir(parents=True, exist_ok=True)
lines = ['/* Generated from works/logo.png; native pixels, no resampling. */',
         '#ifndef MIGA80_BOOT_LOGO_DATA_H', '#define MIGA80_BOOT_LOGO_DATA_H',
         '#include <stdint.h>', '#define MIGA80_BOOT_LOGO_WIDTH 196U',
         '#define MIGA80_BOOT_LOGO_HEIGHT 43U',
         f'#define MIGA80_BOOT_LOGO_SHA256 "{hashlib.sha256(source.read_bytes()).hexdigest()}"',
         'static const uint32_t miga80_boot_logo_rgb[4] = {']
lines += [f'    0x{r:02x}{g:02x}{b:02x}U,' for r,g,b in colors]
lines += ['};', 'static const uint8_t miga80_boot_logo_pixels[196U * 43U] = {']
lines += ['    ' + ','.join(str(v) for v in indices[i:i+28]) + ',' for i in range(0,len(indices),28)]
lines += ['};', '#endif', '']
output.write_text('\n'.join(lines))
print(f'Encoded {source}: {im.width} x {im.height}, {len(colors)} exact colors')
