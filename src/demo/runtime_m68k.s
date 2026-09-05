        .text
        .even

        .globl  _miga80_execute_generated
_miga80_execute_generated:
        move.l  %d2,-(%a7)
        move.l  %a5,-(%a7)
        movea.l  12(%a7),%a0
        movea.l  16(%a7),%a5
        jsr     (%a0)
        movea.l (%a7)+,%a5
        move.l  (%a7)+,%d2
        rts

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
        movea.l 8(%a5),%a0
        move.b  %d2,(%a0,%d0.l)
.L_pset_done:
        rts
