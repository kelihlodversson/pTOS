/*
 * irq.c - x86-64 device-IRQ setup and dispatch for pc-x86_64 (#335)
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
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "ps2.h"
#include "irq.h"
#include "vectors.h"

/* The 200 Hz rate every other pTOS machine's own hardware timer drives
 * vector_5ms()/int_timerc() at (see bios/arch/x86_64/vectors.c's own
 * comment) -- not this file's own invention. */
#define SYSTEM_TICK_HZ 200

/* irqasm.S's own entry stubs, numbered by IDT vector (32 = PIC1 IRQ0, 33 =
 * PIC1 IRQ1, 44 = PIC2 IRQ4 i.e. IRQ12). */
extern void x86_64_irq32(void);
extern void x86_64_irq33(void);
extern void x86_64_irq44(void);

/* Called from irqasm.S's common trampoline for every device IRQ this
 * machine handles. Always sends EOI, even for a vector with no case
 * below (unreachable in practice: the PIC is never unmasked for a line
 * without a case here -- see x86_64_irq_init()), so a stray/spurious IRQ
 * can never leave a chip believing itself still "in service" and unable
 * to raise another. */
void x86_64_pc_irq_dispatch(int vector)
{
    switch (vector) {
    case X86_64_PIC1_VECTOR_BASE + X86_64_IRQ_TIMER:
        vector_5ms();
        break;
    case X86_64_PIC1_VECTOR_BASE + X86_64_IRQ_KEYBOARD:
        x86_64_ps2_keyboard_irq();
        break;
    case X86_64_PIC2_VECTOR_BASE + (X86_64_IRQ_MOUSE - 8):
        x86_64_ps2_mouse_irq();
        break;
    }

    x86_64_pic_send_eoi(vector);
}

void x86_64_irq_init(void)
{
    x86_64_pic_remap();

    x86_64_idt_set_gate(X86_64_PIC1_VECTOR_BASE + X86_64_IRQ_TIMER, x86_64_irq32);
    x86_64_idt_set_gate(X86_64_PIC1_VECTOR_BASE + X86_64_IRQ_KEYBOARD, x86_64_irq33);
    x86_64_idt_set_gate(X86_64_PIC2_VECTOR_BASE + (X86_64_IRQ_MOUSE - 8), x86_64_irq44);

    x86_64_pit_init(SYSTEM_TICK_HZ);
    x86_64_ps2_init();

    x86_64_pic_unmask(X86_64_IRQ_TIMER);
    x86_64_pic_unmask(X86_64_IRQ_KEYBOARD);
    x86_64_pic_unmask(X86_64_IRQ_MOUSE);
}
