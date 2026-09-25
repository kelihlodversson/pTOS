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

/*
 * Entry point for `syscall` (trap.h), installed as IA32_LSTAR by
 * x86_64_trap_init(). Unlike an exception or `int` vector, `syscall`
 * pushes nothing: RIP is loaded from LSTAR directly, the return address
 * arrives in %rcx and the pre-syscall RFLAGS in %r11 (both clobbered by
 * the instruction itself), and %rsp is left completely unchanged -- still
 * whatever the caller (always kernel code today, see trap.h) was already
 * using, so this needs no stack switch.
 *
 * Stack alignment: trap1()/trap13()/trap14()-style callers (util/arch/
 * x86_64/miscasm.S) reach `syscall` as the very first instruction of a
 * normally-`call`ed function, before pushing anything of their own -- so
 * %rsp %16 == 8 here, exactly as if a return address had just been
 * pushed (which, in a sense, one has: the real return address is just in
 * %rcx instead of on the stack). The 15 register pushes below (120
 * bytes) bring that to %rsp %16 == 0 immediately before the call, exactly
 * what the SysV AMD64 ABI requires there -- the same arithmetic as
 * isr.S's isr_noerr case, coincidentally starting from the same residue.
 */
        .globl  C_SYM(x86_64_syscall_entry)
C_SYM(x86_64_syscall_entry):
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
         * Not `sysretq`: that unconditionally forces CPL3 on return
         * (Intel SDM Vol 2B "SYSRET"), which is wrong for a ring0-to-ring0
         * call and unusable regardless with no ring-3 GDT entries set up
         * yet (see trap.h). Since there was never an actual privilege
         * change, %rsp/%cs/%ss already never left ring0's own -- resuming
         * the caller only needs %rflags restored (the push/popfq pair is
         * the standard idiom for loading an arbitrary value into
         * %rflags, since there is no direct move into it) and a jump to
         * the return address `syscall` gave in %rcx, still there after
         * the pop above.
         */
        push    %r11
        popfq
        jmp     *%rcx
