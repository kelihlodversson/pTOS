/*
 * uspaceasm.S - GEMDOS entry and user-mode entry of a process in an address
 *               space of its own (ARM)
 *
 * With CONF_WITH_USER_ASPACE (doc/mmu-address-space.txt) a GEMDOS call from a
 * process is made on the kernel stack of that process (the SVC stack, which
 * gouser() in uspace.c points at the stack of the process it enters), and
 * what a call returns to is only ever the registers of its caller, restored
 * from the frame: not another process whose frame was left on a shared
 * stack, as the classic bdos/arch/arm/rwa.S does it.  A call that starts a
 * process (Pexec()) waits in uspace.c's gouser() on its own stack, and the
 * process that ends returns to it by longjmp().
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"
#include "arch/arm/fpusave.h"
#include "arch/arm/entry.h"

#if CONF_WITH_USER_ASPACE

        .globl  C_SYM(enter)
        .globl  C_SYM(bdos_trap2)
        .globl  C_SYM(arm_enter_user)

        .extern C_SYM(arm_gemdos_dispatch)
        .extern C_SYM(old_trap2)

        .text

/*
 * The frame of a GEMDOS call, from the lowest address (struct uframe in
 * uspace.c):  the FPU state of the caller, its user sp and lr, r0-r12, a
 * pad word that keeps the stack 8-byte aligned, and the return address and
 * status that srs stored.
 */

/*
 * TRAP #1: arm_dispatch_svc jumps here in SVC mode with the return address
 * in lr and the caller's status in spsr.  Only a process in user mode makes
 * GEMDOS calls here: the kernel calls the implementations directly
 * (CONF_WITH_KCALL, bdos/kcall.c), so a call from the SVC mode is refused.
 */
C_SYM(enter):
        srsfd   sp!, #0x13              // return address and status
        sub     sp, sp, #4              // (pad)
        stmfd   sp!, {r0-r12}
        ldr     r1, [sp, #4*13 + 4 + 4] // the saved spsr
        and     r1, r1, #0x1f
        cmp     r1, #0x10
        bne     1f                      // not a user process: no GEMDOS from the kernel

        sub     sp, sp, #8
        stmia   sp, {sp, lr}^           // the user stack pointer and link register
        FPU_PUSH_ALL r4                 // the FPU state: the call may start another process
        mov     r0, sp                  // the frame
        bl      C_SYM(arm_gemdos_dispatch)
        FPU_POP_ALL r4
        ldmia   sp, {sp, lr}^
        add     sp, sp, #8
        ldmfd   sp!, {r0-r12}           // all of them: nothing of the kernel's stays in a register
        add     sp, sp, #4
        rfefd   sp!

1:      ldmfd   sp!, {r0-r12}
        add     sp, sp, #4
        mvn     r0, #31                 // EINVFN (-32)
        rfefd   sp!

/*
 * TRAP #2 as GEMDOS hooks it (bdosmain.c: only to catch the old Pterm() by
 * trap #2 with 0 in r0, which no process of this configuration makes): a
 * process cannot call the VDI and the AES yet (doc/mmu-address-space.txt),
 * the kernel's own calls go on to the handler that was there.
 */
C_SYM(bdos_trap2):
        mrs     ip, spsr
        and     ip, ip, #0x1f
        cmp     ip, #0x10
        beq     1f
        ldr     ip, =C_SYM(old_trap2)
        ldr     ip, [ip]
        bx      ip
1:      mvn     r0, #0                  // -1: refused
        movs    pc, lr

/*
 * void arm_enter_user(ULONG entry, ULONG user_sp, ULONG basepage, ULONG kstack_top);
 *
 * Leaves the kernel for a process that has not run yet, and does not return:
 * the kernel stack becomes the (empty) one of the process, every register is
 * cleared but r0, which holds the basepage, and the FPU state is the reset
 * one, so nothing of the kernel or of the process that ran before is left
 * to read.  The address space is loaded by the caller.
 */
C_SYM(arm_enter_user):
        mov     r4, r0                  // the entry point
        mov     r5, r1                  // the user stack pointer
        mov     r6, r2                  // the basepage
        mov     sp, r3                  // the kernel stack of the process, empty
#if FPU_ALL_BYTES
        mov     r0, #0
        mov     r1, #FPU_ALL_BYTES
2:      subs    r1, r1, #4
        sub     sp, sp, #4
        str     r0, [sp]
        bne     2b
        FPU_POP_ALL r7                  // FPSCR 0 and all the registers 0
#endif
        mov     r0, #0x10               // user mode, interrupts enabled
        sub     sp, sp, #8
        str     r4, [sp]                // pc
        str     r0, [sp, #4]            // cpsr
        cps     #0x1f                   // system mode: the registers of user mode
        mov     sp, r5
        mov     lr, #0
        cps     #0x13
        mov     r0, r6                  // the basepage
        mov     r1, #ARM_ENTRY_PROGRAM
        mov     r2, #0
        mov     r3, #0
        mov     r4, #0
        mov     r5, #0
        mov     r6, #0
        mov     r7, #0
        mov     r8, #0
        mov     r9, #0
        mov     r10, #0
        mov     r11, #0
        mov     r12, #0
        rfefd   sp!

#endif /* CONF_WITH_USER_ASPACE */
