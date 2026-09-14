#!/usr/bin/env python3
"""Two bounded Chip-RAM idle-pointer adaptations; upstream stays untouched."""
import pathlib
import sys
source=pathlib.Path('third_party/ptplayer/ptplayer.asm').read_text()
changes={
    '\telse\n\tbne\tset_replen\n\tendc':
    '\telse\n\tcmp.w\t#1,d3\n\tbhi\tset_replen\n\tclr.w\td3\n\tendc',

    '; use the first two bytes from the first sample for empty samples\n\tmove.l\tmt_SampleStarts(a4),d2':
    '; MIGA-80: use our allocated Chip-RAM zero word for empty samples\n\tmove.l\t_miga80_pt_silence(pc),d2',
    '.1:\tmoveq\t#0,d2\t\t\t; expect two zero bytes at $0':
    '.1:\tmove.l\t_miga80_pt_silence(pc),d2 ; allocated Chip-RAM zero word',
}
for old,new in changes.items():
    assert source.count(old)==1,old
    source=source.replace(old,new)
assert source.rstrip().endswith("end")
source=source.rstrip()[:-3]+"; end is supplied by the including adapter\n"
pathlib.Path(sys.argv[1]).write_text(source)
