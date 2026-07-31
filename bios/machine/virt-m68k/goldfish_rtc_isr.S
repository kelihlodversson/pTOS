/*
 * goldfish_rtc_isr.S - CPU exception-vector entry point for the Goldfish
 * RTC's periodic tick, on QEMU's m68k 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * This has to be hand-written assembly, unlike goldfish_rtc.c's other
 * logic: it runs directly off the CPU's autovector-30 exception, so it
 * alone is responsible for saving/restoring the registers it clobbers
 * and returning correctly with RTE.
 */

#include "asmdefs.h"

        .globl  _goldfish_rtc_isr
        .extern _goldfish_rtc_service
        .extern _vector_5ms

        .text

_goldfish_rtc_isr:
        movem.l d0-d1/a0-a1,-(sp)      // save the registers goldfish_rtc_service() clobbers
        jsr     _goldfish_rtc_service  // ack + re-arm the device (bios/machine/virt-m68k/goldfish_rtc.c)
        movem.l (sp)+,d0-d1/a0-a1      // restore them; sp is now back at the hardware exception frame

        // _vector_5ms is set to int_timerc (bios/arch/m68k/vectors.S) by
        // the shared bios/mfp.c: init_system_timer(), which runs later in
        // bios_init()'s sequence than goldfish_rtc_init() -- so the first
        // several ticks can legitimately arrive with it still NULL.
        move.l  _vector_5ms,a0
        tst.l   a0
        jeq     no_tick

        // Tail-jump, not jsr/rts: int_timerc ends with its own RTE, which
        // must pop the *original* hardware exception frame still sitting
        // where our own entry left it (see bios/arch/m68k/amiga2.S's
        // _amiga_int_ciaa_timer_b for the same pattern on another
        // non-Atari-hardware m68k machine).
        jmp     (a0)

no_tick:
        rte
