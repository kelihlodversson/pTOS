/*
 * virt_uart.c - PL011 UART driver for QEMU's ARM 'virt' machine
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
#include "virt_uart.h"

#define UART0_DR     (*(volatile ULONG*)(VIRT_UART0_BASE+0x00))
#define UART0_FR     (*(volatile ULONG*)(VIRT_UART0_BASE+0x18))
#define UART0_IBRD   (*(volatile ULONG*)(VIRT_UART0_BASE+0x24))
#define UART0_FBRD   (*(volatile ULONG*)(VIRT_UART0_BASE+0x28))
#define UART0_LCRH   (*(volatile ULONG*)(VIRT_UART0_BASE+0x2C))
#define UART0_CR     (*(volatile ULONG*)(VIRT_UART0_BASE+0x30))
#define UART0_IMSC   (*(volatile ULONG*)(VIRT_UART0_BASE+0x38))
#define UART0_ICR    (*(volatile ULONG*)(VIRT_UART0_BASE+0x44))

/* Fixed PL011 input clock on the QEMU 'virt' board. QEMU's UART model
 * does not enforce real baud-rate timing (bytes are transferred as soon
 * as they are written), so this value only matters for documentation
 * purposes / a real terminal on the other end of -serial. */
#define UART_CLOCK  24000000UL
#define BAUDRATE    115200UL

void virt_uart0_init(void)
{
    ULONG baud16 = BAUDRATE * 16;
    ULONG int_div = UART_CLOCK / baud16;
    ULONG fractdiv2 = (UART_CLOCK % baud16) * 8 / BAUDRATE;
    ULONG fractdiv = fractdiv2 / 2 + fractdiv2 % 2;

    UART0_CR = 0;
    UART0_IMSC = 0;
    UART0_ICR = 0x7FF;
    UART0_IBRD = int_div;
    UART0_FBRD = fractdiv;
    UART0_LCRH = (3 << 5);     /* 8 bits, no parity, FIFOs enabled */
    UART0_CR = 0x301;          /* UARTEN | TXE | RXE */
}

BOOL virt_uart0_can_write(void)
{
    return (UART0_FR & 0x20) == 0;     /* TXFF clear */
}

void virt_uart0_write_byte(UBYTE b)
{
    while (!virt_uart0_can_write())
        ;
    UART0_DR = b;
}

BOOL virt_uart0_can_read(void)
{
    return (UART0_FR & 0x10) == 0;     /* RXFE clear */
}

UBYTE virt_uart0_read_byte(void)
{
    while (!virt_uart0_can_read())
        ;
    return (UBYTE) UART0_DR;
}
