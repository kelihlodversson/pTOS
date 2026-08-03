/*
 * goldfish_pic.h - Goldfish PIC interrupt routing for QEMU's m68k
 * 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef GOLDFISH_PIC_H
#define GOLDFISH_PIC_H

#ifdef MACHINE_VIRT_M68K

void goldfish_pic_init(void);
void goldfish_pic_enable(WORD pic, WORD bit);

/* Registers handler for (pic_index, bit) and lazily installs that PIC
 * instance's autovector ISR stub on first use. pic_index must be 1-4
 * (instance 0 carries the TTY, instance 5 the RTC -- both already claimed).
 * Mirrors virt_connect_irq()'s shape on the ARM side. Returns the
 * previously registered handler, or 0. */
PFVOID goldfish_pic_connect_irq(WORD pic_index, WORD bit, PFVOID handler);

/* Called from goldfish_pic_isr1..4 (goldfish_pic_isr.S) once the CPU has
 * taken the matching autovector level. Reads which bits are pending on
 * that PIC instance and calls their registered handlers. */
void goldfish_pic_dispatch(WORD pic_index);

#endif /* MACHINE_VIRT_M68K */

#endif /* GOLDFISH_PIC_H */
