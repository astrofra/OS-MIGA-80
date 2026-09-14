#!/usr/bin/env python3
"""Run real C2P cores in Musashi against an independent bit-by-bit oracle."""
import pathlib
import random
import subprocess
import sys


def run(*args):
    result = subprocess.run(args, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f'{args}:\n{result.stdout}{result.stderr}')
    return result.stdout


def main():
    runner, cc, objcopy = sys.argv[1:]
    root = pathlib.Path(__file__).resolve().parent.parent
    out = root / 'build/host/c2p4-asm'
    out.mkdir(parents=True, exist_ok=True)
    rng = random.Random(68020)
    cases = 0
    for width, height in ((32, 1), (64, 1), (160, 3), (192, 5), (256, 16)):
        for gaps in (False, True):
            src_stride = width + (8 if gaps else 0)
            dst_stride = width // 8 + (4 if gaps else 0)
            size = dst_stride * height + 16
            # Noncontiguous, longword-aligned destinations (Musashi memory policy).
            bases = [0x30000 + n * 0x4000 for n in range(4)]
            for pattern in ('zero', 'ones', 'ramp', 'random'):
                pixels = bytearray([0xDE] * (height * src_stride))
                expected = [bytearray([0xA5] * size) for _ in range(4)]
                for y in range(height):
                    for x in range(width):
                        color = (0 if pattern == 'zero' else 15 if pattern == 'ones'
                                 else x % 16 if pattern == 'ramp' else rng.randrange(16))
                        pixels[y * src_stride + x] = color
                    for p in range(4):
                        for bx in range(width // 8):
                            expected[p][8 + y * dst_stride + bx] = sum(
                                ((pixels[y * src_stride + bx * 8 + bit] >> p) & 1)
                                << (7 - bit) for bit in range(8))
                (out / 'source.bin').write_bytes(pixels)
                (out / 'expected.bin').write_bytes(b''.join(expected))
                for backend in ('mask32', 'kalms'):
                    code = ['.text', '.balign 4', 'entry:']
                    for base in bases:
                        code += [f'move.l #{base},a0', f'move.l #{size-1},d0',
                                 '1: move.b #0xa5,(a0)+', 'dbra d0,1b']
                    # Both cores receive padded row strides. Test Kalms' fused
                    # whole-surface path too, as selected by its C wrapper.
                    if backend == 'kalms':
                        w, h = (width, height) if gaps else (width * height, 1)
                        code += [f'move.l #{dst_stride-width//8},-(sp)',
                                 'lea planes(pc),a0', 'move.l a0,-(sp)',
                                 f'move.l #{src_stride-width},-(sp)',
                                 f'move.l #{h},-(sp)', f'move.l #{w},-(sp)']
                        function = '_miga80_c2p4_kalms_color4_core'
                        args = 24
                    else:
                        code += [f'move.l #{dst_stride-width//8},-(sp)']
                        code += [f'move.l #{base+8},-(sp)' for base in reversed(bases)]
                        code += [f'move.l #{src_stride-width},-(sp)',
                                 f'move.l #{height},-(sp)', f'move.l #{width//32},-(sp)']
                        function = '_miga80_c2p4_mask32_m68k_byte4_core'
                        args = 36
                    code += ['lea source(pc),a0', 'move.l a0,-(sp)',
                             f'bsr {function}', f'lea {args}(sp),sp',
                             'lea expected(pc),a1']
                    for base in bases:
                        code += [f'move.l #{base},a0', f'move.l #{size-1},d0',
                                 '1: move.b (a0)+,d1', 'cmp.b (a1)+,d1',
                                 'bne fail', 'dbra d0,1b']
                    code += ['moveq #0,d0', 'rts', 'fail: moveq #1,d0', 'rts',
                             '.balign 4', 'planes:']
                    code += [f'.long {base+8}' for base in bases]
                    code += ['.balign 4', 'source:',
                             f'.incbin "{out}/source.bin"', 'expected:',
                             f'.incbin "{out}/expected.bin"', '.balign 4',
                             '#include "src/graphics/c2p4_' +
                             ('kalms.S' if backend == 'kalms' else 'm68k.S') + '"']
                    source, obj, binary = [out / name for name in ('entry.S', 'entry.o', 'entry.bin')]
                    source.write_text('\n'.join(code) + '\n')
                    run(cc, '-c', '-m68020', '-I'+str(root), str(source), '-o', str(obj))
                    run(objcopy, '-O', 'binary', '-j', '.text', str(obj), str(binary))
                    name = f'{backend}-{width}x{height}-{pattern}-gaps{int(gaps)}'
                    print(run(runner, '--pset-case', str(binary), name,
                              '0', '0', '0', '0', '0'), end='', flush=True)
                    cases += 1
    print(f'PASS  {cases} real ASM cases: pixels, row padding, guards, read-only source and C ABI')


if __name__ == '__main__':
    main()
