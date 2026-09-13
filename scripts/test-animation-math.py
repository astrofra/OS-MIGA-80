#!/usr/bin/env python3
"""Execute direct and GNU 68020 math on signed boundaries and varied operands."""
import pathlib
import random
import subprocess
import sys


def run(*args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, text=True, **kwargs)


def signed(n):
    return (n + 2**31) % 2**32 - 2**31


def quotient(a, b):
    return (-1 if (a < 0) != (b < 0) else 1) * (abs(a) // abs(b))


def main():
    encoder, runner, assembler, objcopy = sys.argv[1:]
    out = pathlib.Path('build/host/animation/math')
    out.mkdir(parents=True, exist_ok=True)
    rng = random.Random(68020)
    cases = [(a, b) for a in (0, 1, -1, 65535, -65537, 2**31-1, -2**31)
             for b in (1, -1, 65536, -65536, 2**31-1, -2**31)]
    cases += [(signed(rng.getrandbits(32)), signed(rng.getrandbits(32)) or 1)
              for _ in range(40)]
    forms = (
        ('signed-div', 'a:i32,b:i32,c:i32', 'i32', 'a/b', lambda a,b: quotient(a,b)),
        ('unsigned-div', 'a:u16,b:u16,c:u16', 'u16', 'a/b', lambda a,b: (a&0xffff)//(b&0xffff)),
        ('fixed-div', 'a:fix,b:fix,c:fix', 'fix', 'a/b', lambda a,b: quotient(a*65536,b)),
        ('fixed-to-int', 'a:fix,b:fix,c:fix', 'i32', 'i32(a)', lambda a,b: quotient(a,65536)),
        ('int-to-fixed', 'a:i32,b:i32,c:i32', 'fix', 'fix(a)', lambda a,b: a*65536),
        ('roundtrip', 'a:fix,b:fix,c:fix', 'fix', 'fix(i32(a))', lambda a,b: quotient(a,65536)*65536),
        ('constant-div', 'a:i32,b:i32,c:i32', 'i32', 'a/4', lambda a,b: quotient(a,4)),
        ('constant-fixed-div', 'a:fix,b:fix,c:fix', 'fix', 'a/6.0', lambda a,b: quotient(a,6)),
        ('live-fixed-div', 'a:fix,b:fix,c:fix', 'fix', '(a/b)+(a/c)+(b/c)',
         lambda a,b: quotient(a*65536,b)+quotient(a*65536,196608)+quotient(b*65536,196608)),
    )
    count = 0
    for name, args, result, expr, reference in forms:
        source = out / (name+'.lua')
        source.write_text(f'function main({args}): {result}\n return {expr}\nend\n')
        direct, assembly, obj, gnu = (out/(name+ext) for ext in ('.bin','.s','.o','-gnu.bin'))
        run(encoder, '--guarded', source, direct, assembly, stdout=subprocess.DEVNULL)
        run(assembler, '-m68020', assembly, '-o', obj)
        run(objcopy, '-O', 'binary', '-j', '.text', obj, gnu)
        operands = cases if name != 'int-to-fixed' else [(a,1) for a in (-32768,-32767,-1,0,1,32766,32767)]
        if name == 'unsigned-div':
            operands = [(a & 65535, (b & 65535) or 1) for a,b in cases]
        for variant, code in (('direct',direct), ('gnu',gnu)):
            for a,b in operands:
                expected = reference(a,b) & 0xffffffff
                run(runner, '--case', code, f'{name}-{variant}', hex(a&0xffffffff),
                    hex(b&0xffffffff), '196608', hex(expected), stdout=subprocess.DEVNULL)
                count += 1
            if name in ('signed-div','unsigned-div','fixed-div','live-fixed-div'):
                run(runner, '--fault-case', code, f'{name}-{variant}-zero', '65536','0','196608',
                    '1','2',str(source.read_text().splitlines()[1].index('/')+1), stdout=subprocess.DEVNULL)
            if name == 'int-to-fixed':
                for a in (-2**31,-32769,32768,2**31-1):
                    run(runner, '--fault-case', code, name+'-range', hex(a&0xffffffff),'1','1',
                        '2','2','9', stdout=subprocess.DEVNULL)
        print(f'PASS  {name}: direct/GNU results and controlled faults', flush=True)
    print(f'PASS  {count} native math boundary/random cases', flush=True)


if __name__ == '__main__':
    main()
