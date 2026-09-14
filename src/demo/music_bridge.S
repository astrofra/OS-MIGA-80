#include "compiler/abi/runtime.h"
        .text
        .even
        .globl _miga80_runtime_music_play
_miga80_runtime_music_play:
        move.l %d0,-(%sp)
        move.l MIGA80_ABI_RUNTIME_MUSIC_STATE_OFFSET(%a5),-(%sp)
        jsr _miga80_music_play
        addq.l #8,%sp
        rts
        .globl _miga80_runtime_music_stop
_miga80_runtime_music_stop:
        move.l MIGA80_ABI_RUNTIME_MUSIC_STATE_OFFSET(%a5),-(%sp)
        jsr _miga80_music_stop
        addq.l #4,%sp
        rts
        .globl _miga80_runtime_music_mute
_miga80_runtime_music_mute:
        move.l %d0,-(%sp)
        move.l MIGA80_ABI_RUNTIME_MUSIC_STATE_OFFSET(%a5),-(%sp)
        jsr _miga80_music_mute
        addq.l #8,%sp
        rts
        .globl _miga80_runtime_music_position
_miga80_runtime_music_position:
        move.l MIGA80_ABI_RUNTIME_MUSIC_STATE_OFFSET(%a5),-(%sp)
        jsr _miga80_music_position
        addq.l #4,%sp
        rts
