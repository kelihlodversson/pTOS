/*
 * virt_pic.c - GICv2 interrupt controller driver for QEMU's ARM 'virt'
 * machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_VIRT_ARM
#error This file must only be compiled for the QEMU ARM virt target
#endif

#include "portab.h"
#include "virt_memmap.h"
#include "virt_pic.h"
#include "kprint.h"

#define GICD_CTLR        (*(volatile ULONG*)(VIRT_GIC_DIST_BASE + 0x000))
#define GICD_ISENABLER(n) (*(volatile ULONG*)(VIRT_GIC_DIST_BASE + 0x100 + 4*((n)/32)))
#define GICD_ICENABLER(n) (*(volatile ULONG*)(VIRT_GIC_DIST_BASE + 0x180 + 4*((n)/32)))
#define GICD_IPRIORITYR(n) (*(volatile UBYTE*)(VIRT_GIC_DIST_BASE + 0x400 + (n)))
#define GICD_ITARGETSR(n) (*(volatile UBYTE*)(VIRT_GIC_DIST_BASE + 0x800 + (n)))
#define GICC_CTLR  (*(volatile ULONG*)(VIRT_GIC_CPU_BASE + 0x000))
#define GICC_PMR   (*(volatile ULONG*)(VIRT_GIC_CPU_BASE + 0x004))
#define GICC_IAR   (*(volatile ULONG*)(VIRT_GIC_CPU_BASE + 0x00C))
#define GICC_EOIR  (*(volatile ULONG*)(VIRT_GIC_CPU_BASE + 0x010))

static PFVOID virt_irq_handlers[VIRT_IRQ_LINES];

void virt_pic_init(void)
{
    int i;

    for (i = 0; i < VIRT_IRQ_LINES; i++)
        virt_irq_handlers[i] = 0;

    for (i = 0; i < VIRT_IRQ_LINES; i += 32)
        GICD_ICENABLER(i) = 0xffffffffUL;   /* disable everything to start from a known state */
    GICD_CTLR = 1;                          /* enable distributor */

    GICC_PMR = 0xff;                        /* let every priority through */
    GICC_CTLR = 1;                          /* enable this CPU's interface */
}

PFVOID virt_connect_irq(int irq, PFVOID handler)
{
    PFVOID old;

    if (irq < 0 || irq >= VIRT_IRQ_LINES)
    {
        KDEBUG(("virt_connect_irq: IRQ %d out of range\n", irq));
        return 0;
    }

    old = virt_irq_handlers[irq];

    virt_irq_handlers[irq] = handler;
    GICD_IPRIORITYR(irq) = 0x80;
    if (irq >= 32)
        GICD_ITARGETSR(irq) = 0x01;    /* SPIs need explicit CPU targeting; PPIs are implicitly per-CPU */
    GICD_ISENABLER(irq) = (1UL << (irq % 32));
    return old;
}

void virt_int_handler(void)
{
    ULONG iar = GICC_IAR;
    ULONG irq = iar & 0x3ffUL;

    /* 1023 is the GICv2 spurious interrupt ID: the acknowledge did not
     * hand us a real interrupt, and the architecture says not to write
     * EOIR for it. Just return. */
    if (irq == 1023)
        return;

    if (irq < VIRT_IRQ_LINES && virt_irq_handlers[irq])
        ((void (*)(void))virt_irq_handlers[irq])();
    else
        KDEBUG(("virt_int_handler: unexpected IRQ %lu\n", irq));

    GICC_EOIR = iar;
}
