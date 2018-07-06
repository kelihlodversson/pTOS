/*
 * rwa.s - GEMDOS assembler interface
 *
 * Copyright (C) 1999 by Caldera Inc.
 * Copyright (C) 1985, 2001 by Authors (see below)
 * Copyright (C) 2017 The EmuTOS development team
 *
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */


#include "asmdefs.h"
#include "asmpd.h"

// ==== Definitions ==========================================================

        .globl  _termuser
        .globl  _gouser
        .globl  _enter
        .globl  _old_trap2
        .globl  _bdos_trap2

// ==== References ===========================================================

        .extern _osif
        .extern _run

//==== bdos_trap2 - trap 2 entry point =======================================
//
// This minimal interface connects the entry point for a system where the AES
// and VDI are already in memory.
//

_bdos_trap2:
        teq     r0, #0
        beq     do_xterm
        ldr     ip, =_old_trap2
        ldr     ip, [ip]
        bx      ip
do_xterm:
        mov     r7, #0x4c
        b       1f

        .bss
        .even


_old_trap2:
        .ds.l   1                       // old TRAP #2 handler

        .text



//==== _enter - Front end of TRAP #1 handler =================================

_enter:

        teq     r0, #0x20       // toggle/inquire state?
        beq     x20

1:      // Not Super()
        ldr     ip, =_run
        ldr     ip, [ip]        // ip -> current basepage
        add     ip, ip, #P_REGSAVE
        stmia   ip, {r0,r8-r11}

        // Test if caller is in user state
        mrs     r10, spsr
        and     r10, r10, #0x1f

        teq     r10, #0x10       // user mode?
        teqne   r10, #0x1f       // or system mode (also uses user mode stack)
        bne     2f               // if in svc state, don't use user stack

        mov     r8, sp           // save super stack pointer
        mov     r9, lr
        cps     #0x1f
        mov     r10, sp
        cps     #0x13

        srsfd   sp!, #0x1f       // save LR and SPSR on user stack
        cps     #0x1f            // system mode: sp is now user stack pointer
        b       3f

        // he was using his own sp_SVC
2:
        cps     #0x1f            // dip into system mode to read user stack pointer
        mov     r8, sp           // r8 <- user stack
        cps     #0x13            // back into SVC mode
        srsfd   sp!, #0x13       // save LR and SPSR

3:
        stmfd   sp!, {r0-r8}     // Arguments (and other stack pointer) to current stack
        mov     r0, sp           // Argument 0 points to the stacked arguments
        str     sp, [ip, #4*5]   // save updated stack pointer to basepage struct
        cps     #0x13            // if we were using the user stack pointer, return to svc mode

        // now switch to fs stack
        ldr     sp,=fstrt
        bl      _osif

//...fall into gouser....

//==== _gouser ===============================================================

_gouser:
        ldr     ip, =_run
        ldr     ip, [ip]        // ip -> current basepage
        add     ip, ip, #P_REGSAVE
        str     r0, [ip]        // save result register
ggusr:  ldr     r8, [ip, #4*5]  // saved stack pointer (maybe usp, maybe ssp)
        ldmfd   r8!, {r0-r7, r9} // argument registers, other stack pointer in r9

        ldr     r10, [r8, #4]    // peek at stacked SPSR register
        and     r10, r10, #0x1f
        teq     r10, #0x10       // user mode?
        teqne   r10, #0x1f       // or system mode (also uses user mode stack)
        bne     1f

        // r8 is user stack and r9 is system stack
        mov     sp, r9
        cps     #0x1f
        mov     sp, r8
        //  Note we'll stay in super mode until we return as this is the stack that contains the saved LR and SPSR
        b       2f
1:
        // r8 is system stack and r9 is user stack
        cps     #0x1f
        mov     sp, r9
        cps     #0x13           // back into SVC mode
        mov     sp, r8
2:
        ldmia   ip, {r0,r8-r11}
        rfefd   sp!             /* load pc and CPSR from stack */

        // attempting to fix a problem where return codes provided by
        // xterm [ 0x4C ] aren't correctly passed back to the parent.
        // this is entered from C code as termuser()
        // aug 2005, RCL
_termuser:
        ldr     ip, =_run
        ldr     ip, [ip]        // ip -> current basepage
        add     ip, ip, #P_REGSAVE
        b       ggusr

        .bss
        .even

        .ds.w   1000
fstrt:
        .ds.l   1

        .text

//==== function 0x20 handler - toggle/inquire processor/stack state ==========
x20:
        mrs     ip, spsr
        and     ip, ip, #0x1f
        teq     ip, #0x1f       // system mode?
        beq     x20_system

        teq     ip, #0x10       // user mode?
        bne     x20_sup
x20_usr:                        // user mode x20

        teq     r1, #1          // check for +1L entry
        moveq   r0, #0          // If so return 0
        moveqs  pc, lr

        teq     r1, #0          // arg1 == 0?
        bne     1f

#if 1
        // Reuse user sp as the new ssp
        cps     #0x1f
        mov     r1, sp
        cps     #0x13
#else
        mrs     ip, spsr        // modify spsr so we are in SYSTEM mode when we return
        orr     ip, ip, #0xf
        msr     spsr_c, ip
        mvn     r0, #0xff       // return 0xffffff00 as a dummy save SSP (we will ignore it when returning from SYSTEM mode)
        movs    pc, lr          // return to caller in SYSTEM mode
#endif
1:
        mrs     ip, spsr        // modify spsr so we are in SVC mode when we return
        orr     ip, ip, #0x3
        msr     spsr_c, ip

        // swap new and old ssp
        mov     r0, sp
        mov     sp, r1
        movs    pc, lr          // This will return to the caller and restore cpsr from our modified spsr


// system mode gemdos(20) entry point
// Used when the caller passed 0 as the new stack pointer, indicating he wanted to use the user stack while in supervior mode.
// (Since the arm has the special SYSTEM mode that shares the stack pointer with user mode, returning from it is trivial.)
x20_system:
        teq     r1, #1          // check for +1L entry
        mvneq   r0, #0          // return -1 if inquiry
        moveqs  pc, lr

        // Since we are in system mode, we know that the caller wanted to stay on the user stack and can ignore
        // the dummy save SSP argument
        b       1f

// supervisor mode gemdos(20) entry point
// Used when the caller provided an explicit SSP to be used.
x20_sup:
        teq     r1, #1          // check for +1L entry
        mvneq   r0, #0          // return -1 if inquiry
        moveqs  pc, lr

        teq     r1, #0
        beq     2f              // go handle 0L entry

        //      r1 is the new SSP
        mov     sp, r1          // update SSP
1:
        mrs     ip, spsr        // modify spsr so we are in USER mode when we return
        bic     ip, ip, #0xF
        msr     spsr_c, ip
        movs    pc, lr          // This will return to the caller and restore cpsr from our modified spsr

2:
        // This is potentially bad: caller wants to exit super mode without passing a backed up SSP??!
        mov     r1, sp
        add     r1, #8          // TODO: does this even make sense?!
        cps     #0x1f           // update USP
        mov     sp, r1
        cps     #0x13
        b       1b
