/*
 * panicasm.S - support routines for panic debug messages
 *
 * Copyright (C) 2001-2017 The EmuTOS development team
 *
 * Authors:
 *  LVL     Laurent Vogel
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"


        .text

        .globl  _panic
        .globl  _halt
        .globl  _kill_program
        .globl  _warm_reset
        .globl  _cold_reset


/*
 * void panic(const char *fmt, ...);
 * save the registers, state and stack in 0x380, then
 * print the message on the console, and finally halt.
 * note: the stack dump will contain the attributes.
 * the exception number proc_enum will be zero.
 */

_panic:
        // svc mode, no interrupts
        cpsid   aif, #0x13

        push    {ip}

        // dump the registers including USP
        ldr     ip, =_proc_dregs
        stmia   ip!, {r0-r11} // save registers up to r11
        pop     {r11} // read pushed ip into r11
        stmia   ip,  {r11,r13,r14} // save old value of ip (now in r11) plus sp and ir
        ldr     ip, =_proc_usp
        stmia   ip, {sp}^   // dump user stack pointer as well

        // tell dopanic() that there are arguments following
        ldr     ip, =_proc_enum
        mov     r0, #0
        str     r0, [ip]

        // dump 8 longs from the stack
        ldmia   sp, {r0-r7}
        ldr     ip, =_proc_stk
        stmia   ip, {r0-r7}

        // magic value
        ldr     ip,=_proc_lives
        ldr     r0,=0x12345678
        str     r0, [ip]

        // at this time, we have not altered the stack, so we can jump to the
        // C routine directly in kprint.c
        b     _dopanic

/* void halt(void)
 * simply halt the machine.
 */
_halt:
haltloop:
        // endless loop
        b     haltloop

/* void kill_program(void)
 * try to terminate via Pterm(); if Pterm() returns here (!),
 * we do a warm reset
 */
_kill_program:
        mov     r0,#-1              // error code for program killed by OS
        mov     r7,#0x4C            // Pterm
        svc     1
        // drop thru into warm reset

/* void warm_reset(void)
 * Restart this OS
 */
_cold_reset:
_warm_reset:
        bl      _flush_data_cache
        b       _main
