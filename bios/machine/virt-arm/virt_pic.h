/*
 * virt_pic.h - GICv2 interrupt controller driver for QEMU's ARM 'virt'
 * machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef VIRT_PIC_H
#define VIRT_PIC_H

#ifdef MACHINE_VIRT_ARM

#define VIRT_IRQ_LINES  32     /* only PPIs (16-31) are used by this port; no SPI needed for v1 */

void virt_pic_init(void);
PFVOID virt_connect_irq(int irq, PFVOID handler);
void virt_int_handler(void);

#endif /* MACHINE_VIRT_ARM */

#endif /* VIRT_PIC_H */
