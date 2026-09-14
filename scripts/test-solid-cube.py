#!/usr/bin/env python3
"""Check cube geometry and native call/register survival against the host trace."""
import math
import pathlib
import subprocess
import sys


def run(*args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, text=True, **kwargs)


def geometry_check(out):
    actual = [[] for _ in range(250)]
    for row in (out/'edges.txt').read_text().splitlines():
        f,*values=map(int,row.split());actual[f].append(values)
    colors=set()
    for frame in range(250):
        angle=frame/25*.8; tilt=frame/25*.55+.65
        s,c=math.sin(angle),math.cos(angle);u,w=math.sin(tilt),math.cos(tilt)
        def rotate(x,y,z):
            x,z=x*c+z*s,z*c-x*s
            return x,y*w-z*u,y*u+z*w
        expected=[]
        for face in range(6):
            g=(face%2)*2-1
            normal=[(0,0,g),(g,0,0),(0,g,0)][face//2]
            nx,ny,nz=rotate(*normal)
            if nz>=-.167: continue
            shade=.2-nz*.7+ny*.2-nx*.2
            k=2
            while shade>.125: k+=1;shade-=.125
            vertices=[]
            for px,py in [(-1,-1),(-1,1),(1,1),(1,-1)]:
                p=[(px,py,g),(g,py,px),(px,g,py)][face//2]
                x,y,z=rotate(*p)
                vertices.append([128+int(x*410/(z+6)),128-int(y*410/(z+6))])
            for v in (2,3): expected.append(vertices[0]+vertices[v-1]+vertices[v]+[k])
        assert len(actual[frame])==len(expected),(frame,actual[frame],expected)
        for got,want in zip(actual[frame],expected):
            assert all(abs(a-b)<=1 for a,b in zip(got[:6],want[:6])),(frame,got,want)
            assert got[6]==want[6],(frame,got,want)
            colors.add(got[6])
    raw=(out/'frames.bin').read_bytes()
    assert len(raw)==250*65536
    for f in range(250):
        pixels=raw[f*65536:(f+1)*65536]
        assert sum(c!=0 for c in pixels)>10000
        # A convex cube silhouette has a single solid span per raster row.
        for y in range(256):
            row=pixels[y*256:(y+1)*256];lit=[x for x,c in enumerate(row) if c]
            if lit: assert 0 not in row[lit[0]:lit[-1]+1],(f,y)
    print(f'PASS 250 solid cube frames: normals, hidden faces, two-axis projection, lighting {sorted(colors)}, no raster cracks',flush=True)


def main():
    host, runner, cc, assembler, objcopy = sys.argv[1:6]
    source_path = sys.argv[6] if len(sys.argv)>6 else 'assets/demo/cube-solid.lua'
    out = pathlib.Path(sys.argv[7] if len(sys.argv)>7 else 'build/host/solid-cube')
    layer = sys.argv[8] if len(sys.argv)>8 else 'PLANAR'
    out.mkdir(parents=True, exist_ok=True)
    if layer == 'PIXEL':
        assert pathlib.Path(source_path).read_text() == pathlib.Path('assets/demo/cube-solid.lua').read_text().replace('layer(PLANAR)', 'layer(PIXEL)')
    report = run(host, source_path, out, layer, capture_output=True).stdout
    print(report, end='', flush=True)
    values = dict(line.split('=') for line in report.splitlines())
    geometry_check(out)
    # Caller-clobbered D0-D2/A0-A1 are deliberately destroyed by every mock.
    trash = [' move.l #0xdead0001,%d1', ' move.l #0xdead0002,%d2',
             ' movea.l #0xdead0003,%a0', ' movea.l #0xdead0004,%a1', ' rts']
    wrapper = ['.text','.even','entry:']
    for offset in (36,40,44,52,56,60,64,68,72,76,80):
        wrapper += [f' lea handler{offset}(%pc),%a0',f' move.l %a0,{offset}(%a5)']
    wrapper += [' move.l #1000000,12(%a5)', ' move.l #0x811c9dc5,128(%a5)',
                ' clr.l 132(%a5)', ' clr.l 136(%a5)', ' bsr generated',
                ' move.l 128(%a5),%d0',' tst.l 136(%a5)',' beq.s success',
                ' moveq #-1,%d0','success:',' rts']
    for offset,count in ((36,1),(40,3),(44,2),(64,1),(68,0),(72,2),(76,2),(80,1)):
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
