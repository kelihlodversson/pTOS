/*
 * panic.h - x86-64 exception panic dump
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef X86_64_PANIC_H
#define X86_64_PANIC_H

#include "portab.h"

/*
 * The register frame isr.S's common trampoline builds on the stack before
 * calling x86_64_exception_dispatch(), overlaid as a struct.  Field order
 * matches memory order low-to-high address, i.e. the reverse of the
 * trampoline's push sequence (last pushed = lowest address = first
 * field), followed by the fields the CPU itself pushes (or, for vector
 * and error_code, the trampoline's stand-ins for vectors with no hardware
 * error code -- see isr.S).  There is deliberately no rsp/ss: this
 * milestone never uses the TSS's IST mechanism (see gdt.c) and always
 * runs at CPL0, so the CPU never pushes them.
 */
typedef struct {
    UQUAD r15, r14, r13, r12, r11, r10, r9, r8;
    UQUAD rbp, rdi, rsi, rdx, rcx, rbx, rax;
    UQUAD vector;
    UQUAD error_code;
    UQUAD rip;
    UQUAD cs;
    UQUAD rflags;
} PACKED x86_64_exception_frame_t;

/* Called from isr.S's common trampoline for every one of the 32 CPU
 * exception vectors (see idt.c).  Never returns: this milestone has no
 * recoverable fault handling, so every exception is fatal. */
void x86_64_exception_dispatch(x86_64_exception_frame_t *frame) NORETURN;

#endif /* X86_64_PANIC_H */
