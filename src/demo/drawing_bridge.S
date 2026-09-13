#include "compiler/abi/runtime.h"
        .text
        .even

        /* The C ABI preserves D2-D7/A2-A6, a superset of our runtime ABI.
         * Arguments are transient stack copies; the owner owns all data. */
        .globl _miga80_runtime_draw_pset
_miga80_runtime_draw_pset:
        movea.l MIGA80_ABI_RUNTIME_DRAWING_STATE_OFFSET(%a5),%a0
        tst.l   (%a0)
        beq.s   .L_planar_pset
        move.l  #1,4(%a0)
        jmp     _miga80_runtime_pset
.L_planar_pset:
        move.l  %d2,-(%a7)
        move.l  %d1,-(%a7)
        move.l  %d0,-(%a7)
        move.l  %a0,-(%a7)
        jsr     _miga80_draw_pset
        lea     16(%a7),%a7
        rts

        .globl _miga80_runtime_layer
_miga80_runtime_layer:
        move.l  %d0,-(%a7)
        move.l  MIGA80_ABI_RUNTIME_DRAWING_STATE_OFFSET(%a5),-(%a7)
        jsr     _miga80_draw_select
        addq.l  #8,%a7
        rts

        .globl _miga80_runtime_line_start
_miga80_runtime_line_start:
        move.l  %d2,-(%a7)
        move.l  %d1,-(%a7)
        move.l  %d0,-(%a7)
        move.l  MIGA80_ABI_RUNTIME_DRAWING_STATE_OFFSET(%a5),-(%a7)
        jsr     _miga80_draw_line_start
        lea     16(%a7),%a7
        rts

        .globl _miga80_runtime_line_end
_miga80_runtime_line_end:
        move.l  %d1,-(%a7)
        move.l  %d0,-(%a7)
        move.l  MIGA80_ABI_RUNTIME_DRAWING_STATE_OFFSET(%a5),-(%a7)
        jsr     _miga80_draw_line_end
        lea     12(%a7),%a7
        rts

        .globl _miga80_runtime_sin
_miga80_runtime_sin:
        move.l %d0,-(%a7)
        jsr _miga80_fix_sin
        addq.l #4,%a7
        rts

        .globl _miga80_runtime_cos
_miga80_runtime_cos:
        move.l %d0,-(%a7)
        jsr _miga80_fix_cos
        addq.l #4,%a7
        rts

        .globl _miga80_runtime_cls
_miga80_runtime_cls:
        move.l %d0,-(%a7)
        move.l MIGA80_ABI_RUNTIME_DRAWING_STATE_OFFSET(%a5),-(%a7)
        jsr _miga80_draw_clear
        addq.l #8,%a7
        rts

        .globl _miga80_runtime_flip
_miga80_runtime_flip:
        move.l MIGA80_ABI_RUNTIME_DRAWING_STATE_OFFSET(%a5),-(%a7)
        jsr _miga80_draw_flip
        addq.l #4,%a7
        rts

        .globl _miga80_runtime_time
_miga80_runtime_time:
        move.l MIGA80_ABI_RUNTIME_DRAWING_STATE_OFFSET(%a5),-(%a7)
        jsr _miga80_draw_time
        addq.l #4,%a7
        rts
