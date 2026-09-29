/*
 * setjmp.S - EmuTOS's own copy of an ANSI standard feature
 *
 * Copyright (C) 2002-2018 The EmuTOS development team
 * Copyright (C) 2018-2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#include "asmdefs.h"

/*
 * structure of jmp_buf (include/setjmp.h: "long jmp_buf[13]" -- long is
 * 64-bit on this LP64 arch, so 13 slots is ample room for the 8 values
 * below):
 *   0: rbx, rbp, r12, r13, r14, r15 (SysV AMD64 callee-preserved GPRs)
 *   48: rsp, as it will be immediately after this call returns
 *   56: the return address (rip), read off the stack at entry
 */
        .globl  C_SYM(setjmp)
        .globl  C_SYM(longjmp)

        .text

/* --------------------------------------------------------------------
                 int setjmp (jmp_buf);
   -------------------------------------------------------------------- */
C_SYM(setjmp):
        movq    %rbx, 0(%rdi)
        movq    %rbp, 8(%rdi)
        movq    %r12, 16(%rdi)
        movq    %r13, 24(%rdi)
        movq    %r14, 32(%rdi)
        movq    %r15, 40(%rdi)
        leaq    8(%rsp), %rax   /* caller's own rsp, i.e. after this call's return address pops off */
        movq    %rax, 48(%rdi)
        movq    (%rsp), %rax    /* return address */
        movq    %rax, 56(%rdi)
        xorl    %eax, %eax
        ret

/* --------------------------------------------------------------------
        volatile void longjmp (jmp_buf, int);
   -------------------------------------------------------------------- */
C_SYM(longjmp):
        /* Put the return value into the integer result register. But if
         * it is zero then return 1 instead. */
        movl    %esi, %eax
        testl   %eax, %eax
        jnz     1f
        movl    $1, %eax
1:
        movq    0(%rdi), %rbx
        movq    8(%rdi), %rbp
        movq    16(%rdi), %r12
        movq    24(%rdi), %r13
        movq    32(%rdi), %r14
        movq    40(%rdi), %r15
        movq    56(%rdi), %rcx   /* return address, read before rsp is overwritten */
        movq    48(%rdi), %rsp
        jmp     *%rcx
