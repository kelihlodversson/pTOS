/*
 * goldfish_pic_isr.S - CPU exception-vector entry points for Goldfish PIC
 * instances 1-4 (virtio-mmio), on QEMU's m68k 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * Unlike goldfish_rtc_isr.S, these don't need to tail-jump into
 * int_timerc: virtio interrupts have nothing to do with EmuTOS's system
 * timer tick, so a plain register-save / dispatch / restore / rte is
 * enough.
 *
 * void goldfish_pic_dispatch(WORD pic_index) takes one scalar argument.
 * Under -mfastcall that goes in d0, not on the stack: pushing it and
 * calling through a plain jsr (the pre-fastcall convention below) left
 * the compiled dispatch() reading pic_index out of d0 -- whatever the
 * interrupted code happened to leave there -- since d0's incoming value
 * here is only ever the interrupted code's own, never touched by this
 * ISR before the call. dispatch() then uses that garbage index into
 * pic_handlers[pic_index][bit] and calls whatever pointer it finds
 * there: a wild call, firing asynchronously and unpredictably depending
 * on what the interrupted code was doing with d0. Set d0 to the real
 * pic_index (after saving the caller's own d0 alongside d1/a0-a1, so it
 * comes back unchanged on return) instead of pushing it.
 */

#include "asmdefs.h"

        .globl  _goldfish_pic_isr1
        .globl  _goldfish_pic_isr2
        .globl  _goldfish_pic_isr3
        .globl  _goldfish_pic_isr4
        .extern _goldfish_pic_dispatch

        .text

_goldfish_pic_isr1:
        movem.l d0-d1/a0-a1,-(sp)
#if CONF_WITH_MFASTCALL
        moveq   #1,d0
        jsr     _goldfish_pic_dispatch
#else
        move.w  #1,-(sp)
        jsr     _goldfish_pic_dispatch
        addq.l  #2,sp
#endif
        movem.l (sp)+,d0-d1/a0-a1
        rte

_goldfish_pic_isr2:
        movem.l d0-d1/a0-a1,-(sp)
#if CONF_WITH_MFASTCALL
        moveq   #2,d0
        jsr     _goldfish_pic_dispatch
#else
        move.w  #2,-(sp)
        jsr     _goldfish_pic_dispatch
        addq.l  #2,sp
#endif
        movem.l (sp)+,d0-d1/a0-a1
        rte

_goldfish_pic_isr3:
        movem.l d0-d1/a0-a1,-(sp)
#if CONF_WITH_MFASTCALL
        moveq   #3,d0
        jsr     _goldfish_pic_dispatch
#else
        move.w  #3,-(sp)
        jsr     _goldfish_pic_dispatch
        addq.l  #2,sp
#endif
        movem.l (sp)+,d0-d1/a0-a1
        rte

_goldfish_pic_isr4:
        movem.l d0-d1/a0-a1,-(sp)
#if CONF_WITH_MFASTCALL
        moveq   #4,d0
        jsr     _goldfish_pic_dispatch
#else
        move.w  #4,-(sp)
        jsr     _goldfish_pic_dispatch
        addq.l  #2,sp
#endif
        movem.l (sp)+,d0-d1/a0-a1
        rte
