/*
 * miscasm.S - x86-64 GEMDOS caller-side trap glue
 *
 * Copyright (C) 2001 by following authors
 * Copyright (C) 2013-2022 by The EmuTOS development team.
 * Copyright (C) 2018-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"

        .globl  C_SYM(trap1)          // call GEMDOS
        .globl  C_SYM(trap1_pexec)    // Reentrant Pexec call
        .extern C_SYM(x86_64_kernel_trap)

/*
 * trap1 - GEMDOS entry point: long trap1(int fn, ...), matching ARM's own
 * miscasm.S (max 3 real arguments -- every GEMDOS call except Pexec fits
 * that budget, see bdos/bdosmain.c's funcs[] table).
 *
 * Calls x86_64_kernel_trap() (bios/arch/x86_64/trap.c) directly rather
 * than issuing `syscall`: every caller of trap1() is kernel-mode code
 * invoking it as a plain internal API, the same way m68k's "trap #1" and
 * ARM's "svc 1" behave when already supervisor, and some of that code
 * calls into itself this way too (bdos/fsmain.c's xsetdrv() calling
 * Drvmap(), for one) -- see trap.h's own comment on why a shared,
 * privilege-inferring `syscall` entry point cannot safely serve both this
 * and a genuine ring-3 caller, and why an ordinary (trivially reentrant)
 * C call is what every current caller actually needs instead.
 *
 * fn arrives in %edi (SysV integer arg 1, variadic doesn't change this),
 * up to 3 real args in %esi/%edx/%ecx -- already exactly where
 * x86_64_kernel_trap()'s 2nd/3rd/4th parameters (SysV) want them, so only
 * %edi (the opcode, reshuffled into `rax`'s class|function encoding and
 * moved to the 1st parameter register) and the unused 5th parameter (r10,
 * only meaningful for Pexec's 4th argument) need touching at all.
 */
        .text
C_SYM(trap1):
        /*
         * SysV AMD64 ABI: %rsp % 16 == 0 immediately before any `call`,
         * so that the callee sees %rsp % 16 == 8 right after its own
         * return-address push (the same invariant trapasm.S's own
         * entry comment relies on). trap1() is entered the same way,
         * i.e. %rsp % 16 == 8 here already -- but unlike a normal C
         * function it pushes nothing of its own before making its own
         * nested call below, so without this adjustment that call would
         * happen one push short of 16-byte alignment. x86_64_kernel_trap()
         * neither notices nor corrects this itself: it assumes the ABI's
         * invariant already holds, the same as any other callee, and
         * duly misaligns its own locals (and whatever it calls) by 8
         * bytes -- surfacing far downstream as a #GP on the first
         * 16-byte-aligned SSE store the compiler happens to emit
         * (x86_64_trap_dispatch()'s `movaps` building bdosmain.c's
         * argument array), not as an obviously-stack-related fault here.
         */
        subq    $8, %rsp
        movl    %edi, %eax      /* fn */
        btsq    $32, %rax       /* | (X86_64_TRAP_GEMDOS << 32) */
        movq    %rax, %rdi      /* x86_64_kernel_trap()'s "rax" param */
        /* %rsi/%rdx/%rcx already hold arg1/arg2/arg3, exactly matching
         * x86_64_kernel_trap()'s "rdi"/"rsi"/"rdx" params. */
        xorl    %r8d, %r8d      /* "r10" param: unused outside Pexec */
        call    C_SYM(x86_64_kernel_trap)
        addq    $8, %rsp
        ret

/*
 * trap1_pexec - long trap1_pexec(short mode, const char *path,
 * const char *tail, const char *env): Pexec is GEMDOS's one call needing
 * a 4th real argument (trap.c's dispatcher reads it from R10, per
 * trap.h's calling convention). mode/path/tail/env arrive in
 * %edi/%rsi/%rdx/%rcx (SysV args 1-4); x86_64_kernel_trap()'s own
 * parameters want them one slot further along (its "rdi"/"rsi"/"rdx"/
 * "r10" are SysV args 2-5), so each is moved out low-to-high before the
 * next overwrites its source register, then the opcode fills the now-free
 * 1st slot last.
 */
        .text
C_SYM(trap1_pexec):
        subq    $8, %rsp        /* see trap1()'s own comment on why */
        movq    %rcx, %r8       /* env -> "r10" param */
        movq    %rdx, %rcx      /* tail -> "rdx" param */
        movq    %rsi, %rdx      /* path -> "rsi" param */
        movq    %rdi, %rsi      /* mode -> "rdi" param */
        movl    $0x4b, %eax     /* GEMDOS Pexec opcode */
        btsq    $32, %rax       /* | (X86_64_TRAP_GEMDOS << 32) */
        movq    %rax, %rdi      /* -> "rax" param */
        call    C_SYM(x86_64_kernel_trap)
        addq    $8, %rsp
        ret
