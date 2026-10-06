/*
 * irq.h - x86-64 device-IRQ setup for pc-x86_64 (#335)
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef PC_X86_64_IRQ_H
#define PC_X86_64_IRQ_H

/*
 * Remaps and configures the legacy PIC (bios/machine/pc-x86_64/pic.h),
 * installs this machine's three device-IRQ IDT gates (timer, keyboard,
 * mouse -- irqasm.S's own stubs), programs the PIT for a 200 Hz tick
 * (pit.h) and brings up the PS/2 controller (ps2.h), then unmasks all
 * three lines. Does not itself enable interrupts (no `sti`) -- CPU-wide
 * interrupt enable is bios/bios.c's own call, once every other
 * machine's platform-specific setup (this included) has already run, so
 * the very first interrupt that can arrive always has a real, installed
 * handler waiting for it. Must run after x86_64_idt_init() (bios/arch/
 * x86_64/idt.c) and x86_64_gdt_init() (the IDT and its gates must exist
 * before this adds more of them).
 */
void x86_64_irq_init(void);

#endif /* PC_X86_64_IRQ_H */
