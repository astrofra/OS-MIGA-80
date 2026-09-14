#!/usr/bin/env python3
"""Native O0/O1/guarded music API with hostile callees and live return values."""
import pathlib
import subprocess
import sys

def run(*args):
    subprocess.run([str(a) for a in args],check=True)

def main():
    host,runner,cc,assembler,objcopy=sys.argv[1:]
    out=pathlib.Path('build/host/music');out.mkdir(parents=True,exist_ok=True)
    run(host,'works/mods/93_10_12_A_SYNTH_1.mod',out)
    s=['.text','.even','entry:']
    for offset in (80,84,88,92):
        s += [f' lea handler{offset}(%pc),%a0',f' move.l %a0,{offset}(%a5)']
    s += [' clr.l 128(%a5)',' move.l #1000000,12(%a5)',' bsr generated',
          ' cmpi.l #5,128(%a5)',' beq.s done',' moveq #-1,%d0','done:',' rts']
    trash=[' move.l #0xdead0001,%d1',' move.l #0xdead0002,%d2',
           ' movea.l #0xdead0003,%a0',' movea.l #0xdead0004,%a1',' rts']
    for offset,step in ((80,1),(92,2),(84,3)):
        s += [f'handler{offset}:',f' cmpi.l #{step},128(%a5)',' bne.w error']
        if offset in (80,92):
            s += [f' cmpi.l #{0 if offset==80 else 3},%d0',' bne.w error']
        s += [' addq.l #1,128(%a5)',' move.l #0xdead0000,%d0']+trash
    s += ['handler88:',' moveq #41,%d0',' tst.l 128(%a5)',' beq.s position_ok',
          ' cmpi.l #4,128(%a5)',' bne.w error',' moveq #42,%d0',
          'position_ok:',' addq.l #1,128(%a5)']+trash
    s += ['error:',' move.l #-100,128(%a5)',' moveq #-1,%d0',' rts',
          '.balign 4','generated:',' .incbin CODE_FILE']
    source=out/'entry.S';source.write_text('\n'.join(s)+'\n')
    for mode in (0,1,2):
        obj=out/'entry.o';gnu=out/f'api{mode}-gnu.bin'
        run(assembler,'-m68020',out/f'api{mode}.s','-o',obj)
        run(objcopy,'-O','binary','-j','.text',obj,gnu)
        for name,code in (('direct',out/f'api{mode}.bin'),('gnu',gnu)):
            image=out/f'api{mode}-{name}-run.bin'
            run(cc,'-c','-m68020',f'-DCODE_FILE="{code}"',source,'-o',obj)
            run(objcopy,'-O','binary','-j','.text',obj,image)
            run(runner,'--case',image,f'music-api-{mode}-{name}','1','2','3','89')
    print('PASS music API: six native encodings, literal resource, stop, mute, position, clobbered registers')
if __name__=='__main__':main()
