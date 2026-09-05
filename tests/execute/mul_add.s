/* MIGA Lua native ABI 0.6: a=D0, b=D1, result=D0, D2 caller-saved. */

        .text
        .globl  mul_add
mul_add:
        move.l  %d0,%d2
        add.l   %d0,%d0
        add.l   %d2,%d0
        add.l   %d1,%d0
        rts
