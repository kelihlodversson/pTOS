/*
 * goldfish_pic.c - Goldfish PIC interrupt routing for QEMU's m68k
 * 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_VIRT_M68K
#error This file must only be compiled for the QEMU m68k virt target
#endif

#include "portab.h"
#include "goldfish_pic.h"

/*
 * 6 goldfish-pic instances at 0xff000000, 0x1000 bytes apart (see
 * hw/m68k/virt.c). Each one is wired by QEMU's m68k IRQ controller
 * (hw/intc/m68k_irqc.c) to one CPU autovector level: PIC index n (0-5)
 * drives CPU level n+1, so its interrupts are taken at vector n+25.
 */
#define GOLDFISH_PIC_BASE(n)    (0xff000000UL + (ULONG)(n) * 0x1000UL)

#define PIC_IRQ_DISABLE_ALL(n)  (*(volatile ULONG*)(GOLDFISH_PIC_BASE(n) + 0x08))
#define PIC_ENABLE(n)           (*(volatile ULONG*)(GOLDFISH_PIC_BASE(n) + 0x10))

#define GOLDFISH_PIC_COUNT  6

void goldfish_pic_init(void)
{
    WORD n;

    for (n = 0; n < GOLDFISH_PIC_COUNT; n++)
        PIC_IRQ_DISABLE_ALL(n) = 1;   /* value is ignored; any write disables all 32 lines */
}

void goldfish_pic_enable(WORD pic, WORD bit)
{
    PIC_ENABLE(pic) = (1UL << bit);
}
