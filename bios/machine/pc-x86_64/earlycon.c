/*
 * earlycon.c - early PC COM1 debug console
 *
 * A minimal 16550-style UART driver used for boot-time diagnostics before
 * any real device or interrupt handling exists (see issue #330).  It talks
 * directly to I/O ports, which is permitted at CPL0 under UEFI just as it
 * is once pTOS owns the machine, so it works both before and after
 * ExitBootServices() and before and after the higher-half relocation.
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "portab.h"
#include "io.h"
#include "earlycon.h"

#define COM1_PORT 0x3f8

#define UART_THR 0 /* transmit holding register (write) */
#define UART_IER 1 /* interrupt enable register */
#define UART_FCR 2 /* FIFO control register (write) */
#define UART_LCR 3 /* line control register */
#define UART_MCR 4 /* modem control register */
#define UART_LSR 5 /* line status register */

#define UART_LSR_DR   0x01 /* data ready (a byte is available to read) */
#define UART_LSR_THRE 0x20 /* transmit holding register empty */

void earlycon_init(void)
{
    x86_64_outb(COM1_PORT + UART_IER, 0x00);   /* disable all UART interrupts */
    x86_64_outb(COM1_PORT + UART_LCR, 0x80);   /* enable DLAB to set the baud rate divisor */
    x86_64_outb(COM1_PORT + 0, 0x01);          /* divisor low byte: 115200 baud */
    x86_64_outb(COM1_PORT + UART_IER, 0x00);   /* divisor high byte */
    x86_64_outb(COM1_PORT + UART_LCR, 0x03);   /* 8 data bits, no parity, 1 stop bit; clears DLAB */
    x86_64_outb(COM1_PORT + UART_FCR, 0xC7);   /* enable and reset the transmit/receive FIFOs */
    x86_64_outb(COM1_PORT + UART_MCR, 0x0B);   /* assert DTR/RTS/OUT2 */
}

static void earlycon_putc(char c)
{
    while ((x86_64_inb(COM1_PORT + UART_LSR) & UART_LSR_THRE) == 0)
        ;
    x86_64_outb(COM1_PORT + UART_THR, (UBYTE)c);
}

void earlycon_puts(const char *s)
{
    while (*s) {
        if (*s == '\n')
            earlycon_putc('\r');
        earlycon_putc(*s++);
    }
}

BOOL earlycon_can_read(void)
{
    return (x86_64_inb(COM1_PORT + UART_LSR) & UART_LSR_DR) != 0;
}

UBYTE earlycon_read_byte(void)
{
    return x86_64_inb(COM1_PORT + UART_THR);
}

BOOL earlycon_can_write(void)
{
    return (x86_64_inb(COM1_PORT + UART_LSR) & UART_LSR_THRE) != 0;
}

void earlycon_write_byte(UBYTE b)
{
    x86_64_outb(COM1_PORT + UART_THR, b);
}

void earlycon_puthex(UQUAD value)
{
    static const char digits[] = "0123456789abcdef";
    int i;

    earlycon_putc('0');
    earlycon_putc('x');
    for (i = 60; i >= 0; i -= 4)
        earlycon_putc(digits[(value >> i) & 0xF]);
}
