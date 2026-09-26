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

/*
 * Segment selectors into the GDT x86_64_gdt_init() builds.  The IDT
 * entries (idt.c) reference X86_64_KERNEL_CODE_SEL as the segment every
 * exception handler runs in.
 *
 * X86_64_USER32_CS_SEL_BASE/USER_DATA_SEL/USER_CODE_SEL exist for
 * `sysretq` (trap.c/trapasm.S): SYSRET computes its target CS/SS by
 * adding a fixed offset to IA32_STAR[63:48], not by reading a selector
 * value directly, so those three descriptors must sit at consecutive
 * slots in exactly this order (Intel SDM Vol 2B "SYSRET") --
 * USER32_CS_SEL_BASE+8 = USER_DATA_SEL, +16 = USER_CODE_SEL. The first
 * slot is never actually loaded by anything this port does (it exists
 * only because SYSRET's offset arithmetic requires *a* descriptor there,
 * not because this port ever runs 32-bit compatibility-mode code -- #334's
 * ILP32 processes execute genuine 64-bit long-mode instructions, per its
 * own x32-psABI rationale), so it is left as an unused placeholder rather
 * than a real 32-bit code descriptor. USER_DATA_SEL/USER_CODE_SEL already
 * include the RPL=3 bits SYSRET forces anyway, so they can be used
 * directly wherever a ring-3 selector value is needed (e.g. an IRETQ
 * frame's CS/SS).
 */
#define X86_64_KERNEL_CODE_SEL   0x08
#define X86_64_KERNEL_DATA_SEL   0x10
#define X86_64_USER32_CS_SEL_BASE 0x18
#define X86_64_USER_DATA_SEL     (0x20 | 3)
#define X86_64_USER_CODE_SEL     (0x28 | 3)
#define X86_64_TSS_SEL           0x30

/* IST index (1-7, 0 means "don't switch stacks") the #DF gate uses --
 * see idt.c and the dedicated stack x86_64_gdt_init() points the TSS's
 * ist1 slot at. */
#define X86_64_DF_IST 1

#ifndef __ASSEMBLER__

/*
 * Builds a flat kernel-mode GDT (null, code64, data, the ring-3 SYSRET
 * triple, TSS) and a matching TSS, loads them with lgdt/ltr, and reloads
 * every segment register -- including CS, via a far return -- so nothing
 * depends on whatever descriptors UEFI firmware left in place.  Must run
 * before x86_64_idt_init(), since the IDT's interrupt gates reference
 * X86_64_KERNEL_CODE_SEL.
 *
 * Also points the TSS's rsp0 at a dedicated stack: unlike `syscall`
 * (trap.c/trapasm.S switch stacks themselves via swapgs and a per-CPU
 * pointer, never touching the TSS), a ring 3 -> ring 0 transition through
 * an ordinary IDT gate -- any exception a ring-3 process takes -- always
 * loads RSP from here (Intel SDM Vol 3A 6.14.2), IST or not. Without it,
 * such a fault would carry on using whatever the interrupted ring-3 code's
 * own RSP was, corrupting user memory instead of producing a diagnosable
 * panic.
 */
void x86_64_gdt_init(void);

#endif /* __ASSEMBLER__ */

#endif /* X86_64_GDT_H */
