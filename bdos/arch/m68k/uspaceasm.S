/*
 * uspaceasm.S - GEMDOS entry and user-mode entry of a process in an address
 *               space of its own (68040, 68060)
 *
 * With CONF_WITH_USER_ASPACE (doc/mmu-address-space.txt) a GEMDOS call from a
 * process runs on the supervisor stack of that process, the one the CPU
 * switches to for an exception from user mode (gouser() in uspace.c leaves
 * it at the top of the kernel stack of the process it enters), and returns
 * with the registers of its caller: not another process whose frame was left
 * on a shared stack, as the classic rwa.S does it.  A call that starts a
 * process (Pexec()) waits in gouser() on its own stack, and the process that
 * ends returns to it by longjmp().
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"

#if CONF_WITH_USER_ASPACE

        .globl  C_SYM(enter)
        .globl  C_SYM(bdos_trap2)
        .globl  C_SYM(m68k_enter_user)

        .extern C_SYM(m68k_gemdos_dispatch)
        .extern C_SYM(old_trap2)
        .extern C_SYM(uenter)

        .text

/*
 * TRAP #1.  Only a process in user mode makes GEMDOS calls here: the kernel
 * calls the implementations directly (CONF_WITH_KCALL, bdos/kcall.c), so a
 * call from supervisor mode is refused.
 *
 * The frame handed to m68k_gemdos_dispatch() (struct uframe in uspace.c): the
 * user stack pointer, d0-d7 and a0-a6 of the caller.  All of them are
 * restored, so that nothing of the kernel's stays in a register.
 */
C_SYM(enter):
        btst    #5,(sp)                 // high byte of the status: S
        jne     enter_refuse

        movem.l d0-d7/a0-a6,-(sp)
        move.l  usp,a0
        move.l  a0,-(sp)
#if CONF_WITH_MFASTCALL
        move.l  sp,a0
#else
        move.l  sp,-(sp)
#endif
        jsr     C_SYM(m68k_gemdos_dispatch)
#if !CONF_WITH_MFASTCALL
        addq.l  #4,sp
#endif
        move.l  (sp)+,a0
        move.l  a0,usp
        movem.l (sp)+,d0-d7/a0-a6
        rte

enter_refuse:
        moveq   #-32,d0                 // EINVFN
        rte

/*
 * TRAP #2 as GEMDOS hooks it: a process cannot call the VDI and the AES yet
 * (doc/mmu-address-space.txt), the kernel's own calls go on to the handler
 * that was there.
 */
C_SYM(bdos_trap2):
        btst    #5,(sp)
        jeq     trap2_refuse
        move.l  C_SYM(old_trap2),-(sp)
        rts
trap2_refuse:
        moveq   #-1,d0                  // refused
        rte

/*
 * void m68k_enter_user(void);
 *
 * Leaves the kernel for a process that has not run yet, and does not return.
 * What it needs is in uenter (uspace.c): the entry point, the user stack
 * pointer and the top of the kernel stack of the process.  The kernel stack
 * becomes the (empty) supervisor stack, every register is cleared, and the
 * process starts in user mode with interrupts enabled.  The address space is
 * loaded by the caller.
 */
C_SYM(m68k_enter_user):
        move.w  #0x2700,sr
        lea     C_SYM(uenter),a0
        move.l  8(a0),a1                // the top of the kernel stack
        move.l  4(a0),d1                // the user stack pointer
        move.l  (a0),d0                 // the entry point
        move.l  d1,a2
        move.l  a2,usp
        move.l  a1,sp
        clr.w   -(sp)                   // format 0, vector 0
        move.l  d0,-(sp)                // pc
        clr.w   -(sp)                   // sr: user mode, interrupts enabled
        moveq   #0,d0
        moveq   #0,d1
        moveq   #0,d2
        moveq   #0,d3
        moveq   #0,d4
        moveq   #0,d5
        moveq   #0,d6
        moveq   #0,d7
        sub.l   a0,a0
        sub.l   a1,a1
        sub.l   a2,a2
        sub.l   a3,a3
        sub.l   a4,a4
        sub.l   a5,a5
        sub.l   a6,a6
        rte

#endif /* CONF_WITH_USER_ASPACE */
