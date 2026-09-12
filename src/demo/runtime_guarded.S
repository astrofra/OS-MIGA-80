#include "compiler/abi/runtime.h"

        .text
        .even

        .globl  _miga80_execute_generated
_miga80_execute_generated:
        /* A fault skips generated epilogues, so save the full Amiga C
           preserved set here, including D2 which the MIGA ABI may clobber. */
        movem.l %d2-%d7/%a2-%a6,-(%a7)
        movea.l 48(%a7),%a0
        movea.l 52(%a7),%a5
        move.l  %a7,MIGA80_ABI_RUNTIME_HOST_STACK_OFFSET(%a5)
        clr.l   MIGA80_ABI_RUNTIME_FAULT_CODE_OFFSET(%a5)
        clr.l   MIGA80_ABI_RUNTIME_FAULT_LINE_OFFSET(%a5)
        clr.l   MIGA80_ABI_RUNTIME_FAULT_COLUMN_OFFSET(%a5)
        movea.l MIGA80_ABI_RUNTIME_STACK_TOP_OFFSET(%a5),%a7
        jsr     (%a0)
        moveq   #0,%d0
.L_runtime_restore:
        movea.l MIGA80_ABI_RUNTIME_HOST_STACK_OFFSET(%a5),%a7
        movem.l (%a7)+,%d2-%d7/%a2-%a6
        rts

        .globl  _miga80_runtime_fault
_miga80_runtime_fault:
        move.l  %d0,MIGA80_ABI_RUNTIME_FAULT_CODE_OFFSET(%a5)
        move.l  %d1,MIGA80_ABI_RUNTIME_FAULT_LINE_OFFSET(%a5)
        move.l  %d2,MIGA80_ABI_RUNTIME_FAULT_COLUMN_OFFSET(%a5)
        bra.s   .L_runtime_restore

        /* Private SELFTEST service: force a fault through a real generated
           pset call, with an unbalanced frame and damaged saved registers. */
        .globl  _miga80_runtime_test_fault
_miga80_runtime_test_fault:
        link    %a6,#-64
        moveq   #0,%d3
        moveq   #0,%d7
        moveq   #MIGA80_ABI_FAULT_DIVISION_BY_ZERO,%d0
        moveq   #2,%d1
        moveq   #3,%d2
        bra.s   _miga80_runtime_fault

        .globl  _miga80_runtime_pset
_miga80_runtime_pset:
        cmp.l   #256,%d0
        bcc.s   .L_pset_done
        cmp.l   #256,%d1
        bcc.s   .L_pset_done
        cmp.l   #16,%d2
        bcc.s   .L_pset_done
        lsl.l   #8,%d1
        add.l   %d1,%d0
        movea.l MIGA80_ABI_RUNTIME_PIXEL_BUFFER_OFFSET(%a5),%a0
        move.b  %d2,(%a0,%d0.l)
.L_pset_done:
        rts
