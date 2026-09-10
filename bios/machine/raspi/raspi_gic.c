/*
 * raspi_gic.c - BCM2711 GICv2 interrupt controller driver
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef TARGET_RPI4
#error This file must only be compiled for the Raspberry Pi 4 target
#endif

#include "portab.h"
#include "raspi_gic.h"
#include "kprint.h"

#define RASPI_GIC_DIST_BASE 0xff841000UL
#define RASPI_GIC_CPU_BASE  0xff842000UL
#define RASPI_GIC_SPURIOUS  1023UL

#define RASPI_GIC_IRQ_LINES 256

#define GICD_CTLR        (*(volatile ULONG *)(RASPI_GIC_DIST_BASE + 0x000))
#define GICD_TYPER       (*(volatile ULONG *)(RASPI_GIC_DIST_BASE + 0x004))
#define GICD_ISENABLER(n) (*(volatile ULONG *)(RASPI_GIC_DIST_BASE + 0x100 + 4 * ((n) / 32)))
#define GICD_ICENABLER(n) (*(volatile ULONG *)(RASPI_GIC_DIST_BASE + 0x180 + 4 * ((n) / 32)))
#define GICD_IPRIORITYR(n) (*(volatile UBYTE *)(RASPI_GIC_DIST_BASE + 0x400 + (n)))
#define GICD_ITARGETSR(n) (*(volatile UBYTE *)(RASPI_GIC_DIST_BASE + 0x800 + (n)))
#define GICC_CTLR        (*(volatile ULONG *)(RASPI_GIC_CPU_BASE + 0x000))
#define GICC_PMR         (*(volatile ULONG *)(RASPI_GIC_CPU_BASE + 0x004))
#define GICC_IAR         (*(volatile ULONG *)(RASPI_GIC_CPU_BASE + 0x00c))
#define GICC_EOIR        (*(volatile ULONG *)(RASPI_GIC_CPU_BASE + 0x010))

static PFVOID raspi_gic_irq_handlers[RASPI_GIC_IRQ_LINES];

void raspi_gic_init(void)
{
    int i;
    ULONG irq_lines;

    for (i = 0; i < RASPI_GIC_IRQ_LINES; i++)
        raspi_gic_irq_handlers[i] = 0;

    irq_lines = ((GICD_TYPER & 0x1fUL) + 1) * 32;
    for (i = 0; i < irq_lines; i += 32)
        GICD_ICENABLER(i) = 0xffffffffUL;
    GICD_CTLR = 1;

    GICC_PMR = 0xff;
    GICC_CTLR = 1;
}

PFVOID raspi_gic_connect_irq(int irq, PFVOID handler)
{
    PFVOID old;

    if ((irq < 0) || (irq >= RASPI_GIC_IRQ_LINES))
    {
        KDEBUG(("raspi_gic_connect_irq: IRQ %d out of range\n", irq));
        return 0;
    }

    old = raspi_gic_irq_handlers[irq];
    raspi_gic_irq_handlers[irq] = handler;
    GICD_IPRIORITYR(irq) = 0x80;
    if (irq >= 32)
        GICD_ITARGETSR(irq) = 0x01;

    if (handler)
        GICD_ISENABLER(irq) = 1UL << (irq % 32);
    else
        GICD_ICENABLER(irq) = 1UL << (irq % 32);

    return old;
}

void raspi_gic_handle_irq(void)
{
    ULONG iar;
    ULONG irq;

    iar = GICC_IAR;
    irq = iar & 0x3ffUL;
    if (irq == RASPI_GIC_SPURIOUS)
        return;

    if ((irq < RASPI_GIC_IRQ_LINES) && raspi_gic_irq_handlers[irq])
        raspi_gic_irq_handlers[irq]();
    else
        KDEBUG(("raspi_gic_handle_irq: unexpected IRQ %lu\n", irq));

    GICC_EOIR = iar;
}
