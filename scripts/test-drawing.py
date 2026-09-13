#!/usr/bin/env python3
"""Exercise graphics calls with hostile callee registers and generate AGA oracles."""
import json
import pathlib
import subprocess
import sys


def run(*args, **kwargs):
    return subprocess.run(args, check=True, text=True, **kwargs)


def main():
    host, runner, cc, assembler, objcopy = sys.argv[1:]
    root = pathlib.Path(__file__).resolve().parent.parent
    out = root / 'build/host/drawing'
    out.mkdir(parents=True, exist_ok=True)
    fixture = root / 'tests/runtime/layers.lua'
    report = run(host, str(fixture), str(out), capture_output=True).stdout
    values = dict(line.split('=') for line in report.splitlines())
    print(report, end='', flush=True)
    wrapper = ['.text', '.even', 'entry:']
    for offset, count in ((4, 3), (36, 1), (40, 3), (44, 2)):
        wrapper += [f' lea handler{offset}(%pc),%a0', f' move.l %a0,{offset}(%a5)']
    wrapper += [' move.l #1000000,12(%a5)', ' move.l #0x811c9dc5,64(%a5)',
                ' bsr generated', ' move.l 64(%a5),%d0', ' rts']
    for offset, count in ((4, 3), (36, 1), (40, 3), (44, 2)):
        wrapper += [f'handler{offset}:', ' movem.l %d0-%d2,-(%sp)',
                    ' move.l 64(%a5),%d0', ' rol.l #5,%d0', f' eori.l #{offset},%d0']
        for arg in range(count):
            wrapper += [' rol.l #5,%d0', f' move.l {arg*4}(%sp),%d1', ' eor.l %d1,%d0']
        wrapper += [' move.l %d0,64(%a5)', ' lea 12(%sp),%sp',
                    ' move.l #0xdead0000,%d0', ' move.l #0xdead0001,%d1',
                    ' move.l #0xdead0002,%d2', ' movea.l #0xdead0003,%a0',
                    ' movea.l #0xdead0004,%a1', ' rts']
    wrapper += ['.balign 4', 'generated:', ' .incbin CODE_FILE']
    source = out / 'entry.S'
    source.write_text('\n'.join(wrapper)+'\n')
    for mode in range(3):
        native = out / f'mode{mode}.bin'
        obj = out / f'mode{mode}.o'
        binary = out / f'mode{mode}-assembly.bin'
        run(assembler, '-m68020', str(out / f'mode{mode}.s'), '-o', str(obj))
        run(objcopy, '-O', 'binary', '-j', '.text', str(obj), str(binary))
        # O0 textual emission omits fallthrough jumps; its direct encoder does not.
        if mode != 0 and native.read_bytes() != binary.read_bytes():
            raise RuntimeError(f'mode {mode}: native/GNU bytes differ')
        for variant, code in (('direct', native), ('gnu', binary)):
            image = out / f'mode{mode}-{variant}-runtime.bin'
            run(cc, '-c', '-m68020', f'-DCODE_FILE="{code}"', str(source), '-o', str(obj))
            run(objcopy, '-O', 'binary', '-j', '.text', str(obj), str(image))
            run(runner, '--case', str(image), f'drawing-mode{mode}-{variant}', '0', '0', '0',
                '0x'+values['trace'])
    header = root / 'build/generated/drawing_test_data.h'
    header.parent.mkdir(parents=True, exist_ok=True)
    header.write_text('/* Generated from tests/runtime/layers.lua by test-drawing.py. */\n' +
                      '#define DRAW_TEST_PIXEL UINT32_C(0x'+values['pixel']+')\n' +
                      '#define DRAW_TEST_PLANAR UINT32_C(0x'+values['planar']+')\n' +
                      '#define DRAW_TEST_LINES '+values['planar_lines']+'U\n' +
                      'static const char draw_test_source[] =\n' +
                      '\n'.join(json.dumps(line) for line in fixture.read_text().splitlines(True))+';\n')
    print('PASS  CPU pixels, clipping, O0/O1/guarded graphics ABI and call order', flush=True)


if __name__ == '__main__':
    main()
