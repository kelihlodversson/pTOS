/*
 * gemasm.S - x86-64 assembler part of GEM
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"
#include "asmstruct.h"

        .globl  C_SYM(psetup)
        .globl  C_SYM(gotopgm)
        .globl  C_SYM(dsptch)
        .globl  C_SYM(switchto)
        .globl  C_SYM(x86_64_call_on_stack)

        .extern C_SYM(rlr)
        .extern C_SYM(indisp)
        .extern C_SYM(disp)
        .extern C_SYM(panic)

        .text

/*
 * void psetup(AESPD *p, PFVOID codevalue)
 *
 * Arranges for codevalue to be the first thing switchto() resumes into
 * for process p, the next time it is scheduled -- this arch's equivalent
 * of every other arch's own psetup(), just built as a SysV coroutine
 * bootstrap rather than an "RTS stack frame": lay out p's own private
 * stack (p->p_uda->u_supstk, already allocated and pointed to by
 * u_spsuper -- see geminit.c's process-init loop) exactly the way
 * dsptch()'s own save below would leave it, with codevalue standing in
 * for dsptch()'s real return address and the callee-saved registers
 * zeroed (their contents don't matter: this is a fresh entry point, not
 * a real resume, so nothing downstream has any expectation about them).
 *
 * Alignment: switchto() always ends with 6 pops (48 bytes) then a ret
 * (8 more, 56 total) off of whatever rsp it was handed. dsptch()'s own
 * save leaves that value rsp%16==8 (it saved from inside a normally
 * `call`ed function, so 6 pushes -- still a multiple of 16 -- don't
 * change that), which is exactly right for switchto()'s own use there:
 * 56 more bytes consumed lands the *resuming* code back at rsp%16==0,
 * matching the ABI's own invariant for wherever a `call dsptch()`
 * itself returns to. codevalue, in contrast, needs to be *entered* the
 * way any normal C function is when actually `call`ed -- rsp%16==8 at
 * its own first instruction, not 0 -- so the rsp this function hands
 * switchto() must itself be rsp%16==0 instead (0 + 56 = 56, %16 == 8):
 * the opposite of dsptch()'s own case, and the reason this reserves 64
 * bytes below u_spsuper (itself 16-aligned -- geminit.c's own process-
 * init loop) rather than the 56 actually written; the extra 8 are
 * unused padding above codevalue's own slot, never read by switchto().
 *
 * rdi = p, rsi = codevalue
 */
C_SYM(psetup):
        mov     PD_UDA(%rdi), %rax      /* rax = p->p_uda */
        mov     UDA_SPSUPER(%rax), %rcx /* rcx = top of p's own stack */

        sub     $64, %rcx               /* see alignment comment above */
        movq    $0, 0(%rcx)             /* rbp */
        movq    $0, 8(%rcx)             /* rbx */
        movq    $0, 16(%rcx)            /* r12 */
        movq    $0, 24(%rcx)            /* r13 */
        movq    $0, 32(%rcx)            /* r14 */
        movq    $0, 40(%rcx)            /* r15 */
        mov     %rsi, 48(%rcx)          /* "return address" = codevalue */

        mov     %rcx, UDA_SPSUPER(%rax)
        ret

/* The trusted association was prepared before pstart published this task. */
C_SYM(gotopgm):
        jmp     C_SYM(x86_64_aes_enter)

/*
 * void dsptch(void)
 *
 * Called repeatedly to give the hand to another process -- a wrapper
 * around disp() (gemdisp.c). Unlike gotopgm()/switchto(), this is an
 * ordinary function as far as its own caller is concerned (aes/gemasm.h
 * declares it "extern void dsptch(void);", no NORETURN): the SysV ABI
 * already guarantees any caller-saved register the caller still needs
 * survives an ordinary call by being spilled at the call site itself,
 * exactly as for any other function call, so only the *callee-saved*
 * registers (rbx, rbp, r12-r15) plus rsp need saving here -- disp()'s
 * own later switchto() call restores exactly these before returning
 * control to whichever process resumes, satisfying the same ABI
 * contract from the far side of the gap.
 */
C_SYM(dsptch):
        cmpb    $0, C_SYM(indisp)(%rip)
        jne     1f

        movb    $1, C_SYM(indisp)(%rip)

        push    %rbp
        push    %rbx
        push    %r12
        push    %r13
        push    %r14
        push    %r15

        mov     C_SYM(rlr)(%rip), %rax  /* rax = rlr (AESPD*) */
        mov     PD_UDA(%rax), %rax      /* rax = rlr->p_uda */
        mov     %rsp, UDA_SPSUPER(%rax)

        sub     $8, %rsp
        call    C_SYM(x86_64_aes_save)

        lea     gemasm_stack(%rip), %rsp
        sub     $8, %rsp        /* land %rsp % 16 == 8, matching the SysV
                                 * ABI invariant any function normally
                                 * sees on entry right after a real `call`
                                 * pushed its own 8-byte return address --
                                 * this is a `jmp`, so nothing did that
                                 * here, and gemasm_stack itself is a
                                 * plain 16-aligned label (trap.c's own
                                 * percpu.kernel_rsp init uses the same
                                 * "-8" adjustment for the same reason) */
        /* GS and mappings are now normalized, on the dispatcher stack.
         * Idle tasks need timer/PS2 IRQs to become runnable again. */
        sti
        jmp     C_SYM(disp)     /* tail call: disp() always ends in
                                 * switchto(), never really returns, but
                                 * a tail jump means there's nothing to
                                 * handle even if it somehow did -- it
                                 * would just pop whatever dsptch()'s own
                                 * caller left on the stack, same as an
                                 * ordinary return from dsptch() itself */

1:      ret

/*
 * void switchto(UDA *puda) NORETURN
 *
 * Called by disp() to end a dsptch()...switchto() sequence: restores
 * the callee-saved registers and rsp dsptch() (or psetup(), for a
 * process's very first resume) saved in puda->u_spsuper, clears the
 * indisp reentrancy guard, and "returns" into whichever code is at the
 * top of that stack -- either back into dsptch()'s own caller (an
 * ordinary resume) or into a freshly psetup() process's own codevalue.
 *
 * rdi = puda
 */
C_SYM(switchto):
        push    %rdi
        call    C_SYM(x86_64_aes_restore)
        pop     %rdi
        movb    $0, C_SYM(indisp)(%rip)

        mov     UDA_SPSUPER(%rdi), %rsp

        /*
         * Exact reverse of dsptch()'s own push order (rbp,rbx,r12,r13,
         * r14,r15): the last register dsptch() pushed (r15) sits at the
         * lowest address/top of this saved frame, so it must be the
         * first one popped back off here -- popping in the SAME order
         * as dsptch()'s pushes (as an earlier version of this function
         * did) silently swaps every register with a different one
         * instead (e.g. r15's saved value loaded into rbp), corrupting
         * whichever of them the resuming code still had a live value in
         * without changing the return address itself (still read
         * correctly from the fixed offset below all six), which is
         * exactly why this bug produced a wild jump a short distance
         * after resuming rather than an immediate crash: found via a
         * QEMU boot trace once #375's dsptch()/switchto() were first
         * actually exercised for real (aes/geminit.c's iprocess()).
         */
        pop     %r15
        pop     %r14
        pop     %r13
        pop     %r12
        pop     %rbx
        pop     %rbp
        ret

/*
 * void x86_64_call_on_stack(void *new_rsp, void (*fn)(PD *), PD *arg) NORETURN
 *
 * Switches to new_rsp and tail-calls fn(arg) -- used by gemstart.c's
 * ui_start()/accdesk_start() to move execution onto AES process 0's own
 * UDA stack (D.g_int[0].a_uda.u_spsuper, the same stack dsptch()/
 * switchto() above save and restore) before running any of the rest of
 * AES's own C code, which assumes it is already running there. A plain
 * C function cannot safely do this to its own %rsp mid-execution (the
 * compiler has no idea the stack moved, and would still address any
 * spilled locals or its own saved return address relative to the old
 * one) -- hence this minimal, dedicated primitive, with nothing between
 * the switch and the call for anything to go wrong in.
 *
 * new_rsp is taken to mean the same thing it would right before an
 * ordinary `call`: 16-aligned (geminit.c's process-init code and
 * init_p0_stkptr() both hand out u_spsuper that way), with fn entered
 * exactly as if it had actually been called from there -- rsp%16==8 at
 * its own first instruction, same as psetup()'s own codevalue above and
 * for the same reason (fn's compiler-generated prologue assumes it, and
 * nothing else here ever pushes a real return address for a `jmp` to
 * land past).
 *
 * rdi = new_rsp, rsi = fn, rdx = arg
 */
C_SYM(x86_64_call_on_stack):
        mov     %rdi, %rsp
        sub     $8, %rsp
        mov     %rdx, %rdi
        jmp     *%rsi

        .bss
        .balign 16
        .space  0x1000
gemasm_stack:
        .space  8
