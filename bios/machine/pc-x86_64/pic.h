/*
 * pic.h - x86-64 legacy 8259 PIC (programmable interrupt controller)
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef PC_X86_64_PIC_H
#define PC_X86_64_PIC_H

/*
 * Every legacy PC chipset (real or emulated) still has this pair of 8259s
 * wired the IBM PC/AT way: PIC1 (ports 0x20/0x21) handles IRQ0-7, PIC2
 * (0xA0/0xA1) handles IRQ8-15, cascaded into PIC1's own IRQ2 input. This
 * is the vector base #335 remaps them to -- 32 (X86_64_PIC1_VECTOR_BASE)
 * for PIC1's IRQ0-7 and 40 for PIC2's IRQ8-15, both clear of the 32 CPU
 * exception vectors idt.c's own fixed range occupies. bios/machine/
 * pc-x86_64/irqasm.S's own ISR stubs are numbered to match.
 */
#define X86_64_PIC1_VECTOR_BASE 32
#define X86_64_PIC2_VECTOR_BASE 40

#define X86_64_IRQ_TIMER 0   /* PIT channel 0, vector 32 */
#define X86_64_IRQ_KEYBOARD 1  /* PS/2 port 1, vector 33 */
#define X86_64_IRQ_CASCADE 2   /* PIC1 input PIC2 is wired to -- never
                                * itself a real interrupt; must stay
                                * unmasked for any PIC2 line (mouse
                                * included) to ever reach the CPU. */
#define X86_64_IRQ_MOUSE 12    /* PS/2 port 2 (aux), vector 44 */

/*
 * Remaps both PICs to X86_64_PIC1_VECTOR_BASE/X86_64_PIC2_VECTOR_BASE and
 * masks every line (mirroring idt.c's own mask_legacy_pic(), which this
 * supersedes) -- the caller unmasks only the specific lines it actually
 * has a handler installed for, via x86_64_pic_unmask(). Must run before
 * any of this file's other functions, and before the IDT gates for the
 * remapped vectors are installed (a spurious IRQ landing on an absent
 * gate before the remap would take the *wrong* (pre-remap, exception-
 * range) vector number and panic misleadingly).
 */
void x86_64_pic_remap(void);

/* Unmasks one IRQ line (0-15) on whichever chip owns it, without
 * disturbing any other line's mask bit. */
void x86_64_pic_unmask(int irq);

/*
 * Signals end-of-interrupt for the given IDT vector (32-47) -- must be
 * called once, near the end of every device-IRQ handler, before iretq;
 * otherwise that chip (and, for a PIC2 line, PIC1 too, since the
 * cascaded interrupt itself needs acknowledging) never raises another
 * interrupt of any kind again. A PIC2-sourced vector (>= 40) needs EOI
 * sent to both chips, in slave-then-master order (Intel/8259A
 * datasheet); a PIC1-sourced one only to PIC1.
 */
void x86_64_pic_send_eoi(int vector);

#endif /* PC_X86_64_PIC_H */
