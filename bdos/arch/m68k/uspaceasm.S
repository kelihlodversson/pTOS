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
#include "arch/m68k/userentry.h"

#if CONF_WITH_USER_ASPACE

        .globl  C_SYM(enter)
        .globl  C_SYM(bdos_trap2)
        .globl  C_SYM(m68k_enter_user)
        .extern C_SYM(fputype)

        .extern C_SYM(m68k_gemdos_dispatch)
        .extern C_SYM(old_trap2)

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
#if CONF_M68K_ROM_WINDOW && !CONF_M68K_ROM_WINDOW_040
        // the 68030's data cache is addressed logically: what the call wrote
        // into the memory of the process through the window is not what a
        // line of it under the address of the process holds
        move.l  d0,-(sp)
        MOVEC_CACR_D0
        or.l    #0x0800,d0              // CD: clear the data cache
        MOVEC_D0_CACR
        move.l  (sp)+,d0
#endif
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
 * void m68k_enter_user(const M68K_USER_ENTRY *entry);
 *
 * Leaves the kernel for a process that has not run yet, and does not return.
 * What it needs is in launcher-owned kernel arguments: entry point, user stack
 * pointer and the top of the kernel stack of the process.  The kernel stack
 * becomes the (empty) supervisor stack, every register is cleared, and the
 * process starts in user mode with interrupts enabled.  The address space is
 * loaded by the caller.
 */
C_SYM(m68k_enter_user):
        move.w  #0x2700,sr
#if !CONF_WITH_MFASTCALL
        move.l  4(sp),a0                // kernel-owned argument, still on launcher stack
#endif
        move.l  M68K_ENTRY_SSP(a0),a1                // the top of the kernel stack
        move.l  M68K_ENTRY_USP(a0),d1                // the user stack pointer
        move.l  M68K_ENTRY_PC(a0),d0                 // the entry point
        move.l  d1,a2
        move.l  a2,usp
        move.l  a1,sp
        clr.w   -(sp)                   // format 0, vector 0
        move.l  d0,-(sp)                // pc
        clr.w   -(sp)                   // sr: user mode, interrupts enabled
        move.l  M68K_ENTRY_ARG(a0),-(sp) // no argument pointer survives publication/RTE
        tst.l   C_SYM(fputype)
        jeq     no_fpu_reset
        /* the process starts with an FPU as after a reset, not with what the
         * process that ran before left in it */
        clr.l   -(sp)
        clr.l   -(sp)
        clr.l   -(sp)
        /* NULL consumes 4 bytes on 6888x/040, 12 on 060. Do not let
         * post-increment plus a fixed adjustment discard the RTE frame. */
        FRESTORE_SP
        lea     12(sp),sp               // release exactly our three longs
        jsr     C_SYM(m68k_fpu_init)
no_fpu_reset:
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
#if CONF_WITH_NATIVE_SCHED && CONF_WITH_MFASTCALL
        move.l  (sp)+,a0                // native register-argument ABI
#else
        addq.l  #4,sp
#endif
        rte

#endif /* CONF_WITH_USER_ASPACE */

#if CONF_WITH_USER_ASPACE && CONF_WITH_FORK_RETURN_TEST
        .text
        .globl C_SYM(m68k_enter_saved_user)
C_SYM(m68k_enter_saved_user):
        move.w #0x2700,sr
#if CONF_WITH_MFASTCALL
        move.l a0,a2
        move.l d0,a3
#else
        move.l 4(sp),a2
        move.l 8(sp),a3
#endif
        tst.l C_SYM(fputype)
        jeq 4f
        lea 72(a2),a0
#if !CONF_WITH_MFASTCALL
        move.l a0,-(sp)
#endif
        jsr C_SYM(m68k_fpu_restore_complete)
#if !CONF_WITH_MFASTCALL
        addq.l #4,sp
#endif
4:      move.l a3,sp
        /* Fresh format-0 RTE frame; no kernel continuation is copied. */
        move.w 70(a2),-(sp)
        move.l 66(a2),-(sp)
        move.w 64(a2),-(sp)
        move.l (a2),a0
        move.l a0,usp
        movem.l 4(a2),d0-d7/a0-a6
        rte
#endif
