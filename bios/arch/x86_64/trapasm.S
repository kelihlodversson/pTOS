/*
 * trapasm.S - x86-64 GEMDOS/BIOS/XBIOS syscall entry stub
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"
#include "pgtable.h"    /* X86_64_KERNEL_VIRT_BASE */

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
 * left completely unchanged.
 *
 * Two different kinds of caller reach this same entry point: this
 * milestone's own kernel-mode code -- bios/bdos calling
 * Super()/Setexc()/Drvmap()/... via a plain `syscall`, exactly the way
 * m68k's "trap #1" and ARM's "svc 1" are called when already supervisor,
 * including one such call from *inside* another one's own C-level
 * handler (e.g. bdos/fsmain.c's xsetdrv() calling Drvmap()) -- and, once
 * #334 gives this port real ring-3 processes, a genuine user-mode one.
 * They need different handling on both ends:
 *
 *   - A ring-3 caller's %rsp is untrusted (could be small, unmapped for
 *     kernel execution, or hostile) and its GS is whatever that process's
 *     own ambient value is, so this code must swapgs to known-good kernel
 *     state and switch to a stack it knows is safe (PERCPU_KERNEL_RSP)
 *     before touching anything, then swapgs back and `sysretq` (which
 *     unconditionally forces CPL=3) to return.
 *
 *   - A kernel-mode caller is, by definition, already running on a valid
 *     kernel stack with GS already in its ambient (non-swapped) state --
 *     swapgs-ing or switching to the single fixed PERCPU_KERNEL_RSP
 *     buffer would be actively wrong here: swapgs is a stateless toggle,
 *     so a *nested* kernel-mode call (the xsetdrv()/Drvmap() case above)
 *     would undo the outer call's swapgs, and resetting %rsp to the same
 *     fixed top-of-buffer address on every entry would clobber the outer
 *     call's still-live stack frame out from under it. Simply staying on
 *     the current stack with GS untouched -- exactly like an ordinary
 *     function call -- sidesteps both: nesting to any depth then works
 *     for free, the same way ordinary recursive C calls do, bounded only
 *     by however much of the caller's own (already-adequate) kernel stack
 *     is left. `sysretq` must not be used to return, for the same reason
 *     #343 gave for a different mechanism: it would force CPL=3, and the
 *     very next instruction fetch on a supervisor-only kernel page at
 *     CPL=3 takes a protection-violation #PF.
 *
 * The two are told apart the same way on both ends: a higher-half %rsp
 * (>= X86_64_KERNEL_VIRT_BASE) can only be this kernel's own stack, never
 * a ring-3 process's -- every mapping this port ever builds for
 * user-reachable memory sits below that canonical boundary.
 */
        .globl  C_SYM(x86_64_syscall_entry)
C_SYM(x86_64_syscall_entry):
        cmpq    $X86_64_KERNEL_VIRT_BASE, %rsp
        jb      1f

        /* Kernel-mode caller: keep using its own stack and GS as-is. */
        jmp     2f

        /*
         * Ring-3 caller (or none yet, but this is what #334 will use):
         * `swapgs` exchanges the live GS-segment base with
         * IA32_KERNEL_GS_BASE (x86_64_trap_init() points the latter at
         * this CPU's x86_64_percpu_t), giving a way to reach known-good
         * kernel state -- via %gs-relative addressing -- before anything
         * here dares assume %rsp points at a valid, writable stack.
         */
1:      swapgs
        movq    %rsp, %gs:PERCPU_USER_RSP
        movq    %gs:PERCPU_KERNEL_RSP, %rsp

2:
        /*
         * %rsp % 16 == 8 here on both paths: for the ring-3 path,
         * percpu.kernel_rsp (trap.c) is initialized to (top of stack) - 8
         * expressly to match; for the kernel-mode path, trap1()/Setexc()/
         * ...'s own callers already leave %rsp in exactly that state
         * before `syscall` (the same invariant an ordinary `call` leaves
         * for any SysV-ABI-following function, all the way up whatever
         * chain of nested kernel-mode calls led here). The 15 register
         * pushes below (120 bytes) bring that to %rsp % 16 == 0
         * immediately before the call, exactly what the SysV AMD64 ABI
         * requires there -- the same arithmetic as isr.S's isr_noerr
         * case, coincidentally starting from the same residue.
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
         * %rsp is now back to exactly its value on entry to this stub
         * (15 pushes, 15 pops): the kernel-mode caller's own %rsp
         * unchanged, or PERCPU_KERNEL_RSP for the ring-3 path -- either
         * way, still a kernel address, so re-testing it here (rather than
         * consulting PERCPU_USER_RSP, meaningless for the kernel-mode
         * path that never wrote it) makes this exit symmetric with entry.
         */
        cmpq    $X86_64_KERNEL_VIRT_BASE, %rsp
        jb      3f

        /*
         * Kernel-mode caller: stay at CPL0 instead of forcing CPL3, and
         * GS was never swapped on the way in, so it must not be swapped
         * back out either. %rcx/%r11 already hold the return RIP/RFLAGS
         * `syscall` itself provided (restored by the pops above) --
         * exactly what `sysretq` would have consumed, reused here for a
         * plain indirect jump instead.
         */
        push    %r11
        popfq
        jmp     *%rcx

        /*
         * Ring-3 caller: restore its own %rsp (saved above) before
         * swapping GS back, so a fault between here and `sysretq` --
         * there is none, but the ordering is what makes that true --
         * would find %gs already pointing at the *caller's* value, not
         * this CPU's kernel state.
         */
3:      movq    %gs:PERCPU_USER_RSP, %rsp
        swapgs
        sysretq
