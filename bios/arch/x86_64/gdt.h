/*
 * gdt.h - x86-64 kernel GDT and TSS
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef X86_64_GDT_H
#define X86_64_GDT_H

/* Segment selectors into the GDT x86_64_gdt_init() builds.  The IDT
 * entries (idt.c) reference X86_64_KERNEL_CODE_SEL as the segment every
 * exception handler runs in. */
#define X86_64_KERNEL_CODE_SEL 0x08
#define X86_64_KERNEL_DATA_SEL 0x10
#define X86_64_TSS_SEL         0x18

#ifndef __ASSEMBLER__

/*
 * Builds a flat kernel-mode GDT (null, code64, data, TSS) and a matching
 * TSS, loads them with lgdt/ltr, and reloads every segment register --
 * including CS, via a far return -- so nothing depends on whatever
 * descriptors UEFI firmware left in place.  Must run before
 * x86_64_idt_init(), since the IDT's interrupt gates reference
 * X86_64_KERNEL_CODE_SEL.
 */
void x86_64_gdt_init(void);

#endif /* __ASSEMBLER__ */

#endif /* X86_64_GDT_H */
