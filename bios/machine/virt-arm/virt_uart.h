/*
 * virt_uart.h - access to the PL011 UART on QEMU's ARM 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef VIRT_UART_H
#define VIRT_UART_H

#ifdef MACHINE_VIRT_ARM

void virt_uart0_init(void);
BOOL virt_uart0_can_write(void);
void virt_uart0_write_byte(UBYTE b);
BOOL virt_uart0_can_read(void);
UBYTE virt_uart0_read_byte(void);

#endif /* MACHINE_VIRT_ARM */

#endif /* VIRT_UART_H */
