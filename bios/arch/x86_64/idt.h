/*
 * idt.h - x86-64 interrupt descriptor table
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef X86_64_IDT_H
#define X86_64_IDT_H

/*
 * Builds and loads an IDT covering the 32 CPU exception vectors
 * (x86_64_gdt_init() must have run first: every gate references
 * X86_64_KERNEL_CODE_SEL, and #DF's gate references X86_64_DF_IST's
 * dedicated stack), and masks every legacy PIC line so neither chip can
 * raise a device IRQ. Vectors 32-255 are deliberately left
 * absent (not present) in the IDT: nothing in this milestone raises a
 * software interrupt, and the masked PIC cannot raise a hardware one, so
 * the only way one of those vectors could ever be taken is a genuine
 * bug. Hitting an absent gate itself raises #GP (vector 13, WITH an
 * error code identifying the offending vector*8), which IS handled -- so
 * an unexpected vector 32-255 still produces a readable panic instead of
 * a silent triple fault, without needing 224 more stub entry points.
 */
void x86_64_idt_init(void);

#endif /* X86_64_IDT_H */
