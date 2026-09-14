#!/usr/bin/env python3
"""Execute the real chunky span filler with all alignments and colors, with boundary widths."""
import pathlib
import random
import struct
import subprocess
import sys


def run(*args):
    p = subprocess.run([str(a) for a in args], text=True, capture_output=True)
    if p.returncode:
        raise RuntimeError(f'{args}:\n{p.stdout}{p.stderr}')
    return p.stdout


def main():
    runner, cc, objcopy = sys.argv[1:]
    root = pathlib.Path(__file__).resolve().parent.parent
    out = root/'build/host/triangle-asm'
    out.mkdir(parents=True, exist_ok=True)
    rng = random.Random(68020)
    for color in range(16):
        # Cover empty, 1/2/3-byte tails, unrolled blocks, full rows and clipping edges.
        spans = [(a, min(256, a+w)) for a in range(4) for w in (0,1,2,3,4,7,8,15,16,31,32,33,64,127,128,255,256)]
        spans += [(256,256),(255,256),(0,256)]
        while len(spans)<128:
            a=rng.randrange(257);spans.append((a,rng.randrange(a,257)))
        expected=bytearray([0xa5]*(128*256+32))
        for y,(a,b) in enumerate(spans):
            expected[16+y*256+a:16+y*256+b]=bytes([color])*(b-a)
        (out/'expected.bin').write_bytes(expected)
        (out/'left.bin').write_bytes(b''.join(struct.pack('>h',a) for a,b in spans))
        (out/'right.bin').write_bytes(b''.join(struct.pack('>h',b) for a,b in spans))
        code=['.text','.balign 4','entry:',
              ' movea.l #0x30000,%a0',f' move.l #{len(expected)-1},%d0',
              '1: move.b #0xa5,(%a0)+',' dbra %d0,1b',
              f' move.l #{color},-(%sp)',' move.l #128,-(%sp)',
              ' lea right(%pc),%a0',' move.l %a0,-(%sp)',
              ' lea left(%pc),%a0',' move.l %a0,-(%sp)',
              ' move.l #0x30010,-(%sp)',' bsr _miga80_draw_chunky_spans_m68k',' lea 20(%sp),%sp',
              # The native runner also checks callee-saved registers/stack.
              ' movea.l #0x30000,%a0',' lea expected(%pc),%a1',
              f' move.l #{len(expected)-1},%d0','1: move.b (%a0)+,%d1',' cmp.b (%a1)+,%d1',
              ' bne fail',' dbra %d0,1b',' moveq #0,%d0',' rts',
              'fail: moveq #1,%d0',' rts','.balign 4',
              '#include "src/graphics/triangle_m68k.S"','.balign 4',
              'left:',f' .incbin "{out}/left.bin"','right:',f' .incbin "{out}/right.bin"',
              'expected:',f' .incbin "{out}/expected.bin"']
        source,obj,binary=[out/name for name in ('entry.S','entry.o','entry.bin')]
        source.write_text('\n'.join(code)+'\n')
        run(cc,'-c','-m68020','-I'+str(root),source,'-o',obj)
        run(objcopy,'-O','binary','-j','.text',obj,binary)
        print(run(runner,'--pset-case',binary,f'chunky-spans-color{color}','0','0','0','0','0'),end='',flush=True)
    print('PASS 2048 real ASM spans: all colors, alignments, tails, row boundaries, guards and C ABI')


if __name__=='__main__':
    main()
