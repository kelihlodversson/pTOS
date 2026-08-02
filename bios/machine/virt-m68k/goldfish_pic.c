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
#include "vectors.h"
#include "kprint.h"
#include "goldfish_pic.h"

/*
 * 6 goldfish-pic instances at 0xff000000, 0x1000 bytes apart (see
 * hw/m68k/virt.c). Each one is wired by QEMU's m68k IRQ controller
 * (hw/intc/m68k_irqc.c) to one CPU autovector level: PIC index n (0-5)
 * drives CPU level n+1, so its interrupts are taken at vector n+25.
 */
#define GOLDFISH_PIC_BASE(n)    (0xff000000UL + (ULONG)(n) * 0x1000UL)

#define PIC_STATUS(n)           (*(volatile ULONG*)(GOLDFISH_PIC_BASE(n) + 0x00))
#define PIC_PENDING(n)          (*(volatile ULONG*)(GOLDFISH_PIC_BASE(n) + 0x04))
#define PIC_IRQ_DISABLE_ALL(n)  (*(volatile ULONG*)(GOLDFISH_PIC_BASE(n) + 0x08))
#define PIC_ENABLE(n)           (*(volatile ULONG*)(GOLDFISH_PIC_BASE(n) + 0x10))

#define GOLDFISH_PIC_COUNT  6

extern void goldfish_pic_isr1(void);   /* goldfish_pic_isr.S, PIC index 1 -> autovector level 2 */
extern void goldfish_pic_isr2(void);   /* PIC index 2 -> autovector level 3 */
extern void goldfish_pic_isr3(void);   /* PIC index 3 -> autovector level 4 */
extern void goldfish_pic_isr4(void);   /* PIC index 4 -> autovector level 5 */

static PFVOID pic_handlers[GOLDFISH_PIC_COUNT][32];
static BOOL pic_vector_installed[GOLDFISH_PIC_COUNT];

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

static void install_pic_vector(WORD pic_index)
{
    switch (pic_index)
    {
    case 1: VEC_LEVEL2 = goldfish_pic_isr1; break;
    case 2: VEC_LEVEL3 = goldfish_pic_isr2; break;
    case 3: VEC_LEVEL4 = goldfish_pic_isr3; break;
    case 4: VEC_LEVEL5 = goldfish_pic_isr4; break;
    }
}

PFVOID goldfish_pic_connect_irq(WORD pic_index, WORD bit, PFVOID handler)
{
    PFVOID old;

    if (pic_index < 1 || pic_index > 4 || bit < 0 || bit >= 32)
    {
        KDEBUG(("goldfish_pic_connect_irq: (%d,%d) out of range\n", pic_index, bit));
        return 0;
    }

    old = pic_handlers[pic_index][bit];
    pic_handlers[pic_index][bit] = handler;

    if (!pic_vector_installed[pic_index])
    {
        install_pic_vector(pic_index);
        pic_vector_installed[pic_index] = TRUE;
    }

    goldfish_pic_enable(pic_index, bit);
    return old;
}

void goldfish_pic_dispatch(WORD pic_index)
{
    ULONG pending = PIC_PENDING(pic_index);
    WORD bit;

    for (bit = 0; bit < 32; bit++)
    {
        if ((pending & (1UL << bit)) && pic_handlers[pic_index][bit])
            ((void (*)(void))pic_handlers[pic_index][bit])();
    }
}
