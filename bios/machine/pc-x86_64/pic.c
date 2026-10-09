/*
 * pic.c - x86-64 legacy 8259 PIC (programmable interrupt controller)
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

#include "portab.h"
#include "io.h"
#include "pic.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

#define ICW1_INIT 0x10   /* start the initialization sequence */
#define ICW1_ICW4 0x01   /* ICW4 will be sent */
#define ICW4_8086 0x01   /* 8086/88 mode, not the obsolete 8080 one */

#define PIC_EOI 0x20     /* non-specific end-of-interrupt command */

static UBYTE mask1 = 0xFF;  /* PIC1's own IMR shadow -- both chips start
                             * fully masked, mirroring idt.c's own
                             * mask_legacy_pic() this supersedes */
static UBYTE mask2 = 0xFF;

void x86_64_pic_remap(void)
{
    /*
     * The full 4-ICW initialization sequence (Intel 8259A datasheet):
     * ICW1 (start init, ICW4 follows) -> ICW2 (vector base) -> ICW3
     * (cascade wiring: PIC1 is told which of its inputs the slave is on,
     * PIC2 is told its own cascade identity) -> ICW4 (8086 mode). Real
     * hardware and every PC emulator alike require all four in this
     * order; skipping or reordering any of them leaves the chip in an
     * undefined state.
     */
    x86_64_outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    x86_64_outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);

    x86_64_outb(PIC1_DATA, X86_64_PIC1_VECTOR_BASE);
    x86_64_outb(PIC2_DATA, X86_64_PIC2_VECTOR_BASE);

    x86_64_outb(PIC1_DATA, 1 << X86_64_IRQ_CASCADE);  /* slave is on IRQ2 */
    x86_64_outb(PIC2_DATA, X86_64_IRQ_CASCADE);        /* slave's own cascade identity */

    x86_64_outb(PIC1_DATA, ICW4_8086);
    x86_64_outb(PIC2_DATA, ICW4_8086);

    mask1 = 0xFF;
    mask2 = 0xFF;
    x86_64_outb(PIC1_DATA, mask1);
    x86_64_outb(PIC2_DATA, mask2);
}

void x86_64_pic_unmask(int irq)
{
    if (irq < 8) {
        mask1 &= ~(UBYTE)(1 << irq);
        x86_64_outb(PIC1_DATA, mask1);
    } else {
        mask2 &= ~(UBYTE)(1 << (irq - 8));
        x86_64_outb(PIC2_DATA, mask2);
        /*
         * Any PIC2 line reaching the CPU at all depends on PIC1's own
         * IRQ2 (the cascade input) staying unmasked -- do so here rather
         * than requiring every PIC2-line caller to remember it
         * separately.
         */
        mask1 &= ~(UBYTE)(1 << X86_64_IRQ_CASCADE);
        x86_64_outb(PIC1_DATA, mask1);
    }
}

void x86_64_pic_send_eoi(int vector)
{
    if (vector >= X86_64_PIC2_VECTOR_BASE)
        x86_64_outb(PIC2_COMMAND, PIC_EOI);
    x86_64_outb(PIC1_COMMAND, PIC_EOI);
}
