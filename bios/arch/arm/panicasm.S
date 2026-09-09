/*
 * panicasm_arm.S - support routines for panic debug messages
 *
 * Copyright (C) 2001-2022 The EmuTOS development team
 * Copyright (C) 2018-2026 The pTOS development team
 *
 * Authors:
 *  LVL     Laurent Vogel
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"


        .text

        .globl  C_SYM(panic)
        .globl  C_SYM(halt)
        .globl  C_SYM(kill_program)
        .globl  C_SYM(warm_reset)
        .globl  C_SYM(cold_reset)


/*
 * void panic(const char *fmt, ...);
 * save the registers, state and stack in 0x380, then
 * print the message on the console, and finally halt.
 * note: the stack dump will contain the attributes.
 * the exception number proc_enum will be zero.
 */

C_SYM(panic):
        // svc mode, no interrupts
        cpsid   aif, #0x13

        push    {ip}

        // dump the registers including USP
        ldr     ip, =C_SYM(proc_dregs)
        stmia   ip!, {r0-r11} // save registers up to r11
        pop     {r11} // read pushed ip into r11
        stmia   ip!,  {r11,r13,r14} // save old value of ip (now in r11) plus sp and ir
        ldr     ip, =C_SYM(proc_usp)
        cps     #0x1f
        str     sp, [ip]   // dump user stack pointer as well
        cps     #0x13

        // tell dopanic() that there are arguments following
        ldr     ip, =C_SYM(proc_enum)
        mov     r0, #0
        str     r0, [ip]

        // dump 8 longs from the stack
        ldmia   sp, {r0-r7}
        ldr     ip, =C_SYM(proc_stk)
        stmia   ip, {r0-r7}

        // magic value
        ldr     ip,=C_SYM(proc_lives)
        ldr     r0,=0x12345678
        str     r0, [ip]

        // at this time, we have not altered the stack, so we can jump to the
        // C routine directly in kprint.c
        b     C_SYM(dopanic)

/* void halt(void)
 * simply halt the machine.
 */
C_SYM(halt):
haltloop:
        // endless loop
        b     haltloop

/* void kill_program(void)
 * try to terminate via Pterm(); if Pterm() returns here (!),
 * we do a warm reset
 */
C_SYM(kill_program):
        mov     r1,#-1              // error code for program killed by OS
        mov     r0,#0x4C            // Pterm
        svc     1
        // drop thru into warm reset

/* void warm_reset(void)
 * Restart this OS
 */
C_SYM(cold_reset):
C_SYM(warm_reset):
        bl      C_SYM(flush_data_cache)
        b       C_SYM(main)
