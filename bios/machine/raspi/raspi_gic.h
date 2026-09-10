/*
 * raspi_gic.h - BCM2711 GICv2 interrupt controller interface
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef RASPI_GIC_H
#define RASPI_GIC_H

#ifdef TARGET_RPI4

void raspi_gic_init(void);
PFVOID raspi_gic_connect_irq(int irq, PFVOID handler);
void raspi_gic_handle_irq(void);

#endif /* TARGET_RPI4 */

#endif /* RASPI_GIC_H */
