/*
 * pit.h - x86-64 legacy 8253/8254 PIT (programmable interval timer)
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef PC_X86_64_PIT_H
#define PC_X86_64_PIT_H

/*
 * Programs PIT channel 0 (wired to IRQ0 on every legacy PC chipset, real
 * or emulated) for a periodic (mode 2, rate generator) interrupt at
 * `hz` Hz, driving bios/arch/x86_64/vectors.c's vector_5ms() -- the same
 * machine-independent 200 Hz system tick raspi's own hardware timer
 * drives (bios/raspi_int.c). Does not itself unmask or enable the
 * resulting IRQ0 -- that is bios/machine/pc-x86_64/irq.c's job, once its
 * own IDT gate and PIC remap are in place, so no interrupt can arrive
 * before something is ready to handle it.
 */
void x86_64_pit_init(UWORD hz);

#endif /* PC_X86_64_PIT_H */
