#!/usr/bin/env python3
"""Inspect native-core frames and PCM, export an animation and playable preview."""
import math
from pathlib import Path
import sys
import wave
from PIL import Image

root = Path(sys.argv[1])
raw = (root/'boot-jingle.s8').read_bytes()
signed = [b if b < 128 else b - 256 for b in raw]
# Independently verify the dominant melody with a narrow DFT sweep.
for start, expected in ((1764, 440.0), (9702, 659.25), (17640, 554.37)):
    window = signed[start + 200:start + 2248]
    def power(hz):
        angle = 2 * math.pi * hz / 11025
        a = sum(v * math.cos(angle*i) for i,v in enumerate(window))
        b = sum(v * math.sin(angle*i) for i,v in enumerate(window))
        return a*a+b*b
    peak = max(range(int(expected)-10, int(expected)+11), key=power)
    assert abs(peak-expected) < 2.0, (peak,expected)
    print(f'PASS melody peak {peak} Hz (expected {expected})')
with wave.open(str(root/'boot-jingle.wav'),'wb') as out:
    out.setnchannels(1)
    out.setsampwidth(1)
    out.setframerate(11025)
    out.writeframes(bytes((v+128)&255 for v in signed))
palette = (root/'intro-palette.bin').read_bytes() + bytes((256-16)*3)
raw = (root/'intro-strip-frames.bin').read_bytes()
frames = []
for tick in range(160):
    canvas = Image.new('P',(256,256),0)
    canvas.putpalette(palette)
    strip = Image.frombytes('P',(256,64),raw[tick*16384:(tick+1)*16384])
    strip.putpalette(palette)
    canvas.paste(strip,(0,96))
    frames.append(canvas)
frames[60].save(root/'intro-logo.png')
frames[0].save(root/'boot-intro.gif',save_all=True,append_images=frames[1:],
               duration=20,loop=0,optimize=False,disposal=2)
