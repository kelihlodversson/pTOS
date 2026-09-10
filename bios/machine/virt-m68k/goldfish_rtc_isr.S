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
        .extern _longframe

        .text

_goldfish_rtc_isr:
        movem.l d0-d1/a0-a1,-(sp)      // save the registers goldfish_rtc_service() clobbers
        jsr     _goldfish_rtc_service  // ack + re-arm the device (bios/machine/virt-m68k/goldfish_rtc.c)

        // _vector_5ms is set to int_timerc (bios/arch/m68k/vectors.S) by
        // the shared bios/mfp.c: init_system_timer(), which runs later in
        // bios_init()'s sequence than goldfish_rtc_init() -- so the first
        // several ticks can legitimately arrive with it still NULL. Test
        // the sysvar directly (no register needed) so the saved d0-d1/a0-a1
        // stay untouched on the no-tick path below.
        tst.l   _vector_5ms
        jeq     no_tick

        // Tail-jump, not jsr/rts: int_timerc ends with its own RTE, which
        // must pop a genuine exception frame. It can't be the *original*
        // hardware frame directly underneath, though: d0-d1/a0-a1 are still
        // our saved copies at this point, not restored yet, and using a0 to
        // hold the jump target (below) means we cannot restore it first
        // either -- there is no register left to hold the target once every
        // register the interrupted code owns has to come back unchanged.
        // So a synthetic exception frame is built on top of the original
        // one, making int_timerc's RTE land at "restore" below instead of
        // directly at the interrupted code; only then do we restore the
        // real saved registers and issue our own RTE, which finally pops
        // the original frame. Same technique as bios/arch/m68k/amiga2.S's
        // _amiga_int_ciaa_timer_b: this board's shipped configs are always
        // 68020+ (see startup.S), but CONF_DEBUG_FORCE_MC68000 can force a
        // plain-68000 identity for QEMU testing (see Kconfig.debug), so
        // this still has to branch on _longframe like amiga2.S does --
        // hardcoding the 68010+ 8-byte frame (SR, PC, format/vector word)
        // left a real 68000's RTE popping only the 6-byte SR+PC frame it
        // actually understands, leaving the format word on the stack to
        // corrupt whatever the interrupted code finds there next.
        move.l  _vector_5ms,a0
        tst.w   _longframe
        jeq     rtc_nolong
        clr.w   -(sp)                   // format/vector word (68010+ only)
rtc_nolong:
        pea     restore(pc)             // synthetic "return PC"
        move.w  sr,-(sp)                // synthetic SR
        jmp     (a0)

restore:
        movem.l (sp)+,d0-d1/a0-a1
        rte

no_tick:
        movem.l (sp)+,d0-d1/a0-a1
        rte
