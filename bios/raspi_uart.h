/*
 * raspi_uart.h Access to the raspberry pi UART port
 *
 * Copyright (C) 2018-2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef RASPI_UART_H
#define RASPI_UART_H

#ifdef MACHINE_RPI

#if CONF_WITH_RASPI_UART0
void raspi_uart0_init(void);
BOOL raspi_uart0_can_write(void);
void raspi_uart0_write_byte(UBYTE b);
BOOL raspi_uart0_can_read(void);
UBYTE raspi_uart0_read_byte(void);

#if CONF_SERIAL_CONSOLE
void raspi_uart0_poll_rx(void);
#endif
#endif


#endif /* MACHINE_RPI */

#endif /* RASPI_UART_H */
