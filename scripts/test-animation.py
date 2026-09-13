#!/usr/bin/env python3
"""Check cube geometry and native call/register survival against the host trace."""
import math
import pathlib
import subprocess
import sys


def run(*args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, text=True, **kwargs)


def main():
    host, runner, cc, assembler, objcopy = sys.argv[1:]
    out = pathlib.Path('build/host/animation')
    report = run(host, 'assets/demo/cube.lua', out, capture_output=True).stdout
    print(report, end='', flush=True)
    values = dict(line.split('=') for line in report.splitlines())
    colors = set()
    # Independent floating-point matrix/perspective oracle, allowing one pixel
    # for Q16.16 coefficient/trig rounding near an integer boundary.
    for row in (out/'edges.txt').read_text().splitlines():
        frame, edge, x0,y0,x1,y1,color = map(int,row.split())
        a = edge % 4
        theta = frame / 25 * .8
        s,c = math.sin(theta),math.cos(theta)
        tilt = .65 + frame / 25 * .55
        sx,cx = math.sin(tilt),math.cos(tilt)
        projected, depths = [], []
        for v in (-1,1):
            p,q = (a//2)*2-1,(a%2)*2-1
            x,y,z = ((p,q,v),(v,q,p),(p,v,q))[edge//4]
            rx,rz = c*x+s*z,-s*x+c*z
            ry,rz = cx*y-sx*rz,sx*y+cx*rz
            projected += [128+int(rx*410/(rz+6)),128-int(ry*410/(rz+6))]
            depths.append(rz)
        assert all(abs(a-b)<=1 for a,b in zip(projected,(x0,y0,x1,y1))), row
        depth = sum(depths)
        if min(abs(depth-k) for k in (-1,0,1))>.003:
            assert color == (2 if depth>1 else 4 if depth>0 else 6 if depth> -1 else 8), row
        colors.add(color)
    assert colors == {2,4,6,8}
    print('PASS  3000 projected edges and all four depth bands', flush=True)
    # Caller-clobbered D0-D2/A0-A1 are deliberately destroyed by every mock.
    trash = [' move.l #0xdead0001,%d1', ' move.l #0xdead0002,%d2',
             ' movea.l #0xdead0003,%a0', ' movea.l #0xdead0004,%a1', ' rts']
    wrapper = ['.text','.even','entry:']
    for offset in (36,40,44,52,56,60,64,68):
        wrapper += [f' lea handler{offset}(%pc),%a0',f' move.l %a0,{offset}(%a5)']
    wrapper += [' move.l #1000000,12(%a5)', ' move.l #0x811c9dc5,128(%a5)',
                ' clr.l 132(%a5)', ' clr.l 136(%a5)', ' bsr generated',
                ' move.l 128(%a5),%d0',' tst.l 136(%a5)',' beq.s success',
                ' moveq #-1,%d0','success:',' rts']
    for offset,count in ((36,1),(40,3),(44,2),(64,1),(68,0)):
        wrapper += [f'handler{offset}:',' movem.l %d0-%d2,-(%sp)',
                    ' move.l 128(%a5),%d0',' rol.l #5,%d0',f' eori.l #{offset},%d0']
        for arg in range(count):
            wrapper += [' rol.l #5,%d0',f' move.l {arg*4}(%sp),%d1',' eor.l %d1,%d0']
        wrapper += [' move.l %d0,128(%a5)',' lea 12(%sp),%sp']
        if offset == 68:
            wrapper += [' addq.l #1,132(%a5)']
        wrapper += [' move.l #0xdead0000,%d0'] + trash
    for offset,table_offset in ((52,4),(56,8)):
        wrapper += [f'handler{offset}:',' move.l 132(%a5),%d1',' mulu.l #24,%d1',
                    ' lea trig(%pc),%a0',' adda.l %d1,%a0',' cmp.l (%a0),%d0',
                    f' beq.s angle_ok{offset}',' adda.l #12,%a0',' cmp.l (%a0),%d0',
                    f' beq.s angle_ok{offset}',' moveq #1,%d1',' move.l %d1,136(%a5)',
                    f'angle_ok{offset}:',f' move.l {table_offset}(%a0),%d0'] + trash
    wrapper += ['handler60:',' move.l 132(%a5),%d0',' swap %d0',' clr.w %d0',
                ' divu.l #25,%d0'] + trash
    wrapper += ['.balign 4','trig:',(out/'trig-table.s').read_text(),
                '.balign 4','generated:',' .incbin CODE_FILE']
    source = out/'entry.S'
    source.write_text('\n'.join(wrapper)+'\n')
    for mode in (1,2):
        obj,gnu = out/f'mode{mode}.o',out/f'mode{mode}-gnu.bin'
        run(assembler,'-m68020',out/f'mode{mode}.s','-o',obj)
        run(objcopy,'-O','binary','-j','.text',obj,gnu)
        for variant,code in (('direct',out/f'mode{mode}.bin'),('gnu',gnu)):
            image = out/f'cube{mode}-{variant}.bin'
            run(cc,'-c','-m68020',f'-DCODE_FILE="{code}"',source,'-o',obj)
            run(objcopy,'-O','binary','-j','.text',obj,image)
            run(runner,'--pset-case',image,f'cube{mode}-{variant}','0','0','0','0x'+values['trace'],'0')
    # An O0/O1 straight-line fixture keeps input values live across void calls
    # and chains returned fixed-point values across hostile callees.
    stub = ['.text','.even','entry:']
    for offset in (52,56,60,64,68):
        stub += [f' lea handler{offset}(%pc),%a0',f' move.l %a0,{offset}(%a5)']
    stub += [' bsr generated',' rts']
    for offset in (52,56,60,64,68):
        stub += [f'handler{offset}:', ' moveq #7,%d0' if offset==60 else ' add.l #65536,%d0'] + trash
    stub += ['.balign 4','generated:',' .incbin CODE_FILE']
    source.write_text('\n'.join(stub)+'\n')
    for mode in (0,1,2):
        obj,image = out/'calls.o',out/f'calls{mode}-runtime.bin'
        run(cc,'-c','-m68020',f'-DCODE_FILE="{out}/calls{mode}.bin"',source,'-o',obj)
        run(objcopy,'-O','binary','-j','.text',obj,image)
        run(runner,'--case',image,f'returning-calls-mode{mode}','1','2','3',str(131088))


if __name__ == '__main__':
    main()
