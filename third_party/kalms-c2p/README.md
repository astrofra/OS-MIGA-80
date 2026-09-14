# Kalms C2P provenance

Upstream: https://github.com/Kalmalyzer/kalms-c2p
Revision: `d8ecf79a3325615305dd800ae7704b518e0d9dda`

`c2p1x1_4_c5_bm.s` is an unchanged copy of `bitmap/c2p1x1_4_c5_bm.s`.
`readme.txt` is the unchanged upstream notice, declaring these files Public Domain.

`src/graphics/c2p4_kalms.S` adapts the non-modulo 32-pixel loop to GNU
assembler syntax and the project's C ABI, with four explicit destination
pointers and source/destination row strides. The transpose and pipelined
stores retain upstream instruction ordering. This is an adaptation, not a
benchmark of the unmodified AmigaOS BitMap entry point. Input colors must
be 0..15; the original implementation does not mask arbitrary high nibbles.
