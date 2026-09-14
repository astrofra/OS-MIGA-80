        .text
        .even
        /* C ABI: pixels at first row, left[], right[], row count, color4.
         * Align each span once, then write 32 bytes per unrolled iteration. */
        .globl _miga80_draw_chunky_spans_m68k
_miga80_draw_chunky_spans_m68k:
        movem.l %d2-%d4/%a2-%a3,-(%sp)
        movea.l 24(%sp),%a0
        movea.l 28(%sp),%a1
        movea.l 32(%sp),%a2
        move.l  36(%sp),%d2
        beq     .L_spans_done
        move.l  40(%sp),%d0
        mulu.w  #257,%d0
        move.w  %d0,%d1
        swap    %d0
        move.w  %d1,%d0
.L_span_row:
        moveq   #0,%d3
        move.w  (%a1)+,%d3
        moveq   #0,%d1
        move.w  (%a2)+,%d1
        sub.w   %d3,%d1
        ble.s   .L_span_next
        lea     (%a0,%d3.l),%a3
.L_span_align:
        move.l  %a3,%d4
        andi.w  #3,%d4
        beq.s   .L_span_blocks
        move.b  %d0,(%a3)+
        subq.w  #1,%d1
        beq.s   .L_span_next
        bra.s   .L_span_align
.L_span_blocks:
        cmpi.w  #32,%d1
        blt.s   .L_span_longs
        move.l  %d0,(%a3)+
        move.l  %d0,(%a3)+
        move.l  %d0,(%a3)+
        move.l  %d0,(%a3)+
        move.l  %d0,(%a3)+
        move.l  %d0,(%a3)+
        move.l  %d0,(%a3)+
        move.l  %d0,(%a3)+
        subi.w  #32,%d1
        bra.s   .L_span_blocks
.L_span_longs:
        cmpi.w  #4,%d1
        blt.s   .L_span_tail
        move.l  %d0,(%a3)+
        subq.w  #4,%d1
        bra.s   .L_span_longs
.L_span_tail:
        tst.w   %d1
        beq.s   .L_span_next
.L_span_bytes:
        move.b  %d0,(%a3)+
        subq.w  #1,%d1
        bne.s   .L_span_bytes
.L_span_next:
        adda.w  #256,%a0
        subq.l  #1,%d2
        bne     .L_span_row
.L_spans_done:
        movem.l (%sp)+,%d2-%d4/%a2-%a3
        rts
