; C ABI adapters around the unchanged public-domain ptplayer 6.4.
; OS allocation only happens on the owner task. All music updates are CIA IRQs.
OSCOMPAT equ 1
MINIMAL equ 0
ENABLE_SAWRECT equ 1
NULL_IS_CLEARED equ 0
        include "build/amiga/source-view/ptplayer.inc"
        even

        xdef _miga80_pt_install
_miga80_pt_install:
        ; Never steal another application's channels (upstream defaults to 127).
        move.b #-128,mt_ioaudio+9
        clr.w mt_ioaudio+32           ; reset allocation key for repeated opens
        lea miga80_tick_irq(pc),a0
        move.l a0,mt_extern_timer_a_is+18
        clr.l _miga80_pt_dma_seen
        clr.l _miga80_pt_ticks
        bra _mt_install

miga80_tick_irq:
        tst.b _mt_Enable
        beq.s .idle
        addq.l #1,_miga80_pt_ticks
.idle:  bsr mt_cia_timer_a_code
        tst.b _mt_Enable
        beq.s .done
        moveq #15,d0
        and.w CUSTOM+2,d0            ; DMACONR: actual Paula DMA channel enables
        or.l d0,_miga80_pt_dma_seen
.done:  rts

        xdef _miga80_pt_dma_seen
_miga80_pt_dma_seen: dc.l 0
        xdef _miga80_pt_ticks
_miga80_pt_ticks: dc.l 0

        xdef _miga80_pt_start
_miga80_pt_start:
        movem.l a4/a6,-(sp)
        lea CUSTOM,a6
        lea mt_data(pc),a4
        ; Reset ALL channel effect memories, including vibrato and pattern loops.
        ; Upstream mt_end deliberately preserves some of those between songs.
        lea mt_data(pc),a0
        move.w #mt_Enable-1,d0
.clear: clr.b (a0)+
        dbf d0,.clear
        ; mt_timerval is part of the cleared data; derive the PAL/NTSC clock.
        move.l 4.w,a0
        move.l #1773447,d0
        cmp.b #50,531(a0)
        beq.s .pal
        move.l #1789773,d0
.pal:   move.l d0,mt_timerval(a4)
        move.l 12(sp),a0              ; module (validated mutable Chip allocation)
        move.l 16(sp),a1              ; explicit sample base
        move.l 20(sp),_miga80_pt_silence
        moveq #0,d0
        jsr _mt_init
        ; Notes/effects before the first instrument still need valid pointers.
        lea mt_data(pc),a0
        lea mt_Tuning0(pc),a1
        move.l _miga80_pt_silence(pc),d1
        moveq #3,d0
.voices:
        move.l a1,n_pertab(a0)
        move.l d1,n_start(a0)
        move.l d1,n_loopstart(a0)
        move.l d1,n_wavestart(a0)
        move.w #1,n_length(a0)
        move.w #1,n_reallength(a0)
        move.w #1,n_replen(a0)
        lea n_sizeof(a0),a0
        dbf d0,.voices
        clr.l _miga80_pt_dma_seen
        clr.l _miga80_pt_ticks
        ; Clear and enable both timers after the stop adapter disabled them.
        move.l 4.w,a6
        lea mt_ciab_name(pc),a1
        jsr _LVOOpenResource(a6)
        move.l d0,a6
        moveq #3,d0
        jsr -24(a6)                  ; SetICR: clear our pending timer interrupts
        move.l #$83,d0
        jsr _LVOAbleICR(a6)
        move.b #$11,CIAB+CIACRA
        st _mt_Enable
        movem.l (sp)+,a4/a6
        rts

        xdef _miga80_pt_stop
_miga80_pt_stop:
        move.l a6,-(sp)
        clr.b _mt_Enable
        move.l 4.w,a6
        lea mt_ciab_name(pc),a1
        jsr _LVOOpenResource(a6)
        move.l d0,a6
        moveq #3,d0
        jsr _LVOAbleICR(a6)           ; prevent delayed DMA-on after stopping
        clr.b CIAB+CIACRA
        clr.b CIAB+CIACRB
        moveq #3,d0
        jsr -24(a6)                  ; discard our pending timer interrupts
        clr.b TB_toggle
        move.w #$8000,mt_dmaon
        lea CUSTOM,a6
        jsr _mt_end
        move.l (sp)+,a6
        rts

        xdef _miga80_pt_position
_miga80_pt_position:
        moveq #-1,d0
        tst.b _mt_Enable
        beq.s .done
        ; Retry if the IRQ advanced the row between reading row and order.
.retry: moveq #0,d0
        move.w mt_data+mt_PatternPos(pc),d0
        moveq #0,d1
        move.b mt_data+mt_SongPos(pc),d1
        cmp.w mt_data+mt_PatternPos(pc),d0
        bne.s .retry
        lsr.w #4,d0
        lsl.w #6,d1
        add.w d1,d0                   ; next row: order*64 + row, zero-based
.done:  rts

        xdef _miga80_pt_mute
_miga80_pt_mute:
        move.l a6,-(sp)
        move.l 8(sp),d0
        not.b d0
        and.w #15,d0                 ; API set bit means muted
        lea CUSTOM,a6
        jsr _mt_channelmask
        move.l (sp)+,a6
        rts

_miga80_pt_silence: dc.l 0
