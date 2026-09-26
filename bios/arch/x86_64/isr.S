/*
 * isr.S - x86-64 exception entry stubs
 *
 * One tiny entry stub per CPU exception vector (installed into the IDT by
 * idt.c), all funnelling into one common trampoline that saves a full
 * register frame and calls into C (panic.c).
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"

        .text

/*
 * On entry to a stub, the CPU has already pushed (low to high address,
 * i.e. what %rsp points at going up): [error code, only for the vectors
 * listed below] RIP, CS, RFLAGS, RSP, SS. RSP and SS are part of every
 * vector's frame in IA-32e (long) mode, not just ones that switch
 * privilege level or use IST (Intel SDM Vol 3A 6.14.2) -- unlike legacy
 * protected mode, where they are only pushed on a privilege change.
 *
 * isr_noerr pushes a dummy 0 error code first, so that after either
 * macro the stack shape is identical: [vector, error code, RIP, CS,
 * RFLAGS, RSP, SS] -- which is what lets one common trampoline, and one
 * C struct (x86_64_exception_frame_t, panic.h), handle every vector.
 *
 * Only 8, 10, 11, 12, 13, 14, 17 and 21 push a hardware error code
 * (Intel SDM Vol 3A 6.15, Table 6-1); every other vector uses isr_noerr.
 */
.macro isr_noerr num
        .globl  C_SYM(x86_64_isr\num)
C_SYM(x86_64_isr\num):
        push    $0
        push    $\num
        jmp     x86_64_isr_common
.endm

.macro isr_err num
        .globl  C_SYM(x86_64_isr\num)
C_SYM(x86_64_isr\num):
        push    $\num
        jmp     x86_64_isr_common
.endm

        isr_noerr 0     /* #DE  Divide Error */
        isr_noerr 1     /* #DB  Debug */
        isr_noerr 2     /*      NMI */
        isr_noerr 3     /* #BP  Breakpoint */
        isr_noerr 4     /* #OF  Overflow */
        isr_noerr 5     /* #BR  BOUND Range Exceeded */
        isr_noerr 6     /* #UD  Invalid Opcode */
        isr_noerr 7     /* #NM  Device Not Available */
        isr_err   8     /* #DF  Double Fault (error code always 0) */
        isr_noerr 9     /*      Coprocessor Segment Overrun (reserved) */
        isr_err   10    /* #TS  Invalid TSS */
        isr_err   11    /* #NP  Segment Not Present */
        isr_err   12    /* #SS  Stack-Segment Fault */
        isr_err   13    /* #GP  General Protection Fault */
        isr_err   14    /* #PF  Page Fault */
        isr_noerr 15    /*      reserved */
        isr_noerr 16    /* #MF  x87 FPU Floating-Point Error */
        isr_err   17    /* #AC  Alignment Check */
        isr_noerr 18    /* #MC  Machine Check */
        isr_noerr 19    /* #XM  SIMD Floating-Point Exception */
        isr_noerr 20    /* #VE  Virtualization Exception */
        isr_err   21    /* #CP  Control Protection Exception */
        isr_noerr 22
        isr_noerr 23
        isr_noerr 24
        isr_noerr 25
        isr_noerr 26
        isr_noerr 27
        isr_noerr 28
        isr_noerr 29
        isr_noerr 30
        isr_noerr 31

/*
 * Common trampoline every stub above jumps (not calls) into. Pushes the
 * remaining general-purpose registers, in a fixed order that
 * x86_64_exception_frame_t (panic.h) mirrors field-for-field, then calls
 * x86_64_exception_dispatch(%rsp) per the SysV AMD64 ABI (first integer
 * argument in %rdi).
 *
 * Stack alignment: the CPU aligns %rsp to 16 bytes before pushing its own
 * frame (Intel SDM Vol 3A 6.14.2), so at the point a stub's first push
 * executes, %rsp % 16 is 8 for a no-error vector (RIP/CS/RFLAGS/RSP/SS =
 * 40 bytes already pushed) or 0 for an error-code vector (error
 * code/RIP/CS/RFLAGS/RSP/SS = 48 bytes). isr_noerr's dummy push brings
 * the no-error case to 0 too, matching the error-code case exactly from
 * that point on. The vector push (8 bytes) and the 15 register pushes
 * below (120 bytes) total 128 bytes -- a multiple of 16 -- so %rsp % 16
 * == 0 again right before the call below, exactly what the ABI requires
 * there.
 */
x86_64_isr_common:
        push    %rax
        push    %rbx
        push    %rcx
        push    %rdx
        push    %rsi
        push    %rdi
        push    %rbp
        push    %r8
        push    %r9
        push    %r10
        push    %r11
        push    %r12
        push    %r13
        push    %r14
        push    %r15

        /*
         * The SysV AMD64 ABI requires DF (the direction flag) clear at
         * any C function boundary, but an exception can interrupt code
         * that had set it (e.g. a backwards `std`-prefixed string op);
         * RFLAGS is not restored until iretq, which this trampoline
         * never reaches. Without this, x86_64_exception_dispatch() (and
         * anything it calls) would inherit a possibly-set DF and any
         * string instruction in the C handler could walk memory
         * backwards instead of forwards.
         */
        cld

        mov     %rsp, %rdi
        call    C_SYM(x86_64_exception_dispatch)

        /* Not reached: x86_64_exception_dispatch() is NORETURN -- this
         * milestone has no recoverable fault handling, every exception
         * is fatal. Safety net in case that ever changes without this
         * trampoline being revisited. */
.Lhang:
        cli
        hlt
        jmp     .Lhang
