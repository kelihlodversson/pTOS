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
        move.w  #1,-(sp)
        jsr     _goldfish_pic_dispatch
        addq.l  #2,sp
        movem.l (sp)+,d0-d1/a0-a1
        rte

_goldfish_pic_isr2:
        movem.l d0-d1/a0-a1,-(sp)
        move.w  #2,-(sp)
        jsr     _goldfish_pic_dispatch
        addq.l  #2,sp
        movem.l (sp)+,d0-d1/a0-a1
        rte

_goldfish_pic_isr3:
        movem.l d0-d1/a0-a1,-(sp)
        move.w  #3,-(sp)
        jsr     _goldfish_pic_dispatch
        addq.l  #2,sp
        movem.l (sp)+,d0-d1/a0-a1
        rte

_goldfish_pic_isr4:
        movem.l d0-d1/a0-a1,-(sp)
        move.w  #4,-(sp)
        jsr     _goldfish_pic_dispatch
        addq.l  #2,sp
        movem.l (sp)+,d0-d1/a0-a1
        rte
