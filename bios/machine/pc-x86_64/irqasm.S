/*
 * irqasm.S - x86-64 device-IRQ entry stubs (#335)
 *
 * Mirrors bios/arch/x86_64/isr.S's own stub/trampoline shape (see its
 * detailed comments on the stack layout and alignment invariants), but
 * for the three PIC-remapped device-IRQ vectors this machine actually
 * handles (bios/machine/pc-x86_64/pic.h) rather than the 32 CPU
 * exceptions -- and, unlike that file's trampoline, this one returns:
 * a device IRQ is routine, expected, and recoverable, not a fatal
 * condition to dump and hang on.
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_PC_X86_64
#error This file must only be compiled for the pc-x86_64 machine
#endif

#include "asmdefs.h"

        .text

/*
 * Hardware interrupts never push an error code (Intel SDM Vol 3A 6.15),
 * so every stub here pushes a dummy 0 first -- exactly isr_noerr's own
 * idiom (isr.S) -- then the vector number, before falling into the
 * common trampoline. The CPU's own pushes (RIP, CS, RFLAGS, RSP, SS --
 * 40 bytes, always, per isr.S's own comment on IA-32e mode) leave
 * %rsp % 16 == 8 at the point this stub's first push executes; the dummy
 * and the vector (16 bytes together) leave that unchanged at 8, which is
 * what lets the trampoline's own 15 register pushes (120 bytes, itself
 * only 8 mod 16) land on 16-byte alignment for its call below -- see
 * isr.S's own comment on the identical arithmetic. A stub that pushed
 * only the vector (8 bytes, as this one first shipped) would instead
 * flip that same starting point to 16-byte-aligned *before* the 120-byte
 * push, landing the call 8 bytes short of what the ABI requires there --
 * invisible to plain GP-register code, but a #GP the moment any callee
 * (kprintf's variadic prologue spilling %xmm0-7 via movaps, in
 * particular) executes a 16-byte-aligned SSE store relative to it. The
 * dummy is discarded, along with the vector, once the trampoline is done
 * (see its own tail comment).
 */
.macro irq_stub num
        .globl  C_SYM(x86_64_irq\num)
C_SYM(x86_64_irq\num):
        push    $0
        push    $\num
        jmp     x86_64_irq_common
.endm

        irq_stub 32     /* IRQ0  timer (PIT channel 0) */
        irq_stub 33     /* IRQ1  PS/2 keyboard */
        irq_stub 44     /* IRQ12 PS/2 mouse (aux port) */

/*
 * Common trampoline: save every GP register the C ABI doesn't already
 * guarantee callee-saved (same 15-register set isr.S's own trampoline
 * saves, for the same reason -- an IRQ can interrupt absolutely any
 * code, including mid-computation kernel code with live values in any
 * of them), call x86_64_pc_irq_dispatch(vector) per the SysV AMD64 ABI
 * (first integer argument in %rdi), then unwind and iretq back to
 * wherever this interrupted -- unlike isr.S's own trampoline, this one
 * is expected to return, every time.
 *
 * Stack alignment: see irq_stub's own comment -- its dummy+vector pushes
 * leave %rsp % 16 == 8, and the 15 register pushes below (120 bytes, itself
 * 8 mod 16) bring that to %rsp % 16 == 0 at the call, exactly what the ABI
 * requires there.
 */
x86_64_irq_common:
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

        cld     /* see isr.S's own comment: the ABI requires DF clear at
                 * any C call boundary, and an IRQ can interrupt code
                 * that had set it */

        mov     120(%rsp), %rdi   /* the vector number, pushed by the stub,
                                   * now 15 registers (120 bytes) below the
                                   * current %rsp */
        call    C_SYM(x86_64_pc_irq_dispatch)

        pop     %r15
        pop     %r14
        pop     %r13
        pop     %r12
        pop     %r11
        pop     %r10
        pop     %r9
        pop     %r8
        pop     %rbp
        pop     %rdi
        pop     %rsi
        pop     %rdx
        pop     %rcx
        pop     %rbx
        pop     %rax

        add     $16, %rsp  /* discard the vector number and irq_stub's
                             * dummy padding push */
        iretq
