/*
 * trapasm.S - x86-64 GEMDOS/BIOS/XBIOS syscall entry stub
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"

        .text

/* Offsets into x86_64_percpu_t (trap.h) -- kept in sync by hand, since
 * this file (unlike trap.c) never includes that header: there is no
 * assembler-visible equivalent of a C struct layout here. */
#define PERCPU_KERNEL_RSP 0
#define PERCPU_USER_RSP   8

/*
 * Entry point for `syscall` (trap.h), installed as IA32_LSTAR by
 * x86_64_trap_init(). Unlike an exception or `int` vector, `syscall`
 * pushes nothing and does not consult the TSS: RIP is loaded from LSTAR
 * directly, the return address arrives in %rcx and the pre-syscall
 * RFLAGS in %r11 (both clobbered by the instruction itself), and %rsp is
 * left completely unchanged -- still whatever the caller was using, which
 * once real ring-3 callers exist (#334) is a user stack this code must
 * not touch before switching off of it.
 *
 * This is reached *only* by a genuine ring-3 caller (or, until #334
 * exists, a throwaway in-kernel harness that IRETQs into CPL3 first,
 * purely to exercise this path): every kernel-mode caller -- bios/bdos
 * calling Super()/Setexc()/... the way m68k's "trap #1"/ARM's "svc 1" are
 * called when already supervisor -- goes through x86_64_kernel_trap()
 * (trap.c) instead, an ordinary C function call with no privilege
 * transition at all. An earlier version of this file let both share this
 * entry point, telling them apart by testing whether the interrupted RSP
 * looked like a kernel address; that was unsafe, since `syscall` never
 * validates RSP, so a real ring-3 caller could put any 64-bit value there
 * -- including a higher-half one -- and get treated as kernel-mode,
 * skipping `swapgs`/the stack switch below and pushing (and later reading
 * back) attacker-influenced register state at CPL0 wherever it chose.
 * Splitting the two mechanisms removes the ambiguity that bug depended
 * on: nothing legitimate reaches this stub except through a real
 * privilege transition, so it can simply always do the full swapgs/
 * stack-switch/sysretq sequence below.
 *
 * `swapgs` exchanges the live GS-segment base with IA32_KERNEL_GS_BASE
 * (x86_64_trap_init() points the latter at this CPU's x86_64_percpu_t),
 * giving a way to reach known-good kernel state -- via %gs-relative
 * addressing -- before anything here dares assume %rsp points at a valid,
 * writable stack. PERCPU_USER_RSP saves the interrupted %rsp so the exit
 * path can restore it; PERCPU_KERNEL_RSP supplies a stack this code
 * knows is safe to push registers onto.
 */
        .globl  C_SYM(x86_64_syscall_entry)
C_SYM(x86_64_syscall_entry):
        swapgs
        movq    %rsp, %gs:PERCPU_USER_RSP
        movq    %gs:PERCPU_KERNEL_RSP, %rsp

        /*
         * percpu.kernel_rsp (trap.c) is initialized to (top of stack) - 8,
         * i.e. %rsp %16 == 8 here, exactly as if a return address had just
         * been pushed (the same convention a normally-`call`ed function
         * relies on). The 15 register pushes below (120 bytes) bring that
         * to %rsp %16 == 0 immediately before the call, exactly what the
         * SysV AMD64 ABI requires there -- the same arithmetic as isr.S's
         * isr_noerr case, coincidentally starting from the same residue.
         */
        push    %rcx    /* return RIP, from `syscall` */
        push    %r11    /* saved RFLAGS, from `syscall` */
        push    %rax
        push    %rbx
        push    %rdx
        push    %rsi
        push    %rdi
        push    %rbp
        push    %r8
        push    %r9
        push    %r10
        push    %r12
        push    %r13
        push    %r14
        push    %r15

        /* See isr.S's identical comment: the SysV AMD64 ABI requires DF
         * clear at any C function boundary, and this can interrupt code
         * that left it set. */
        cld

        mov     %rsp, %rdi
        call    C_SYM(x86_64_trap_dispatch)

        /*
         * Restore every register (rax now holds x86_64_trap_dispatch()'s
         * result, written into frame->rax in place), in exact reverse of
         * the push sequence above.
         */
        pop     %r15
        pop     %r14
        pop     %r13
        pop     %r12
        pop     %r10
        pop     %r9
        pop     %r8
        pop     %rbp
        pop     %rdi
        pop     %rsi
        pop     %rdx
        pop     %rbx
        pop     %rax
        pop     %r11
        pop     %rcx

        /*
         * Mirror image of entry: restore the caller's own %rsp (saved
         * above) before swapping GS back, so a fault between here and
         * `sysretq` -- there is none, but the ordering is what makes that
         * true -- would find %gs already pointing at the *caller's*
         * value, not this CPU's kernel state. Neither instruction touches
         * %rax/%rcx/%r11, which is why the pops above happen first:
         * `sysretq` needs %rcx (target RIP) and %r11 (target RFLAGS)
         * exactly as `syscall` provided them, and forces CPL=3 on the
         * selectors gdt.h's SYSRET triple names (Intel SDM Vol 2B
         * "SYSRET") -- correct for the only kind of caller that ever
         * reaches this stub now.
         */
        movq    %gs:PERCPU_USER_RSP, %rsp
        swapgs
        sysretq
