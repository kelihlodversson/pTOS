/*
 * raspi_uart.c - Serial device access on Raspberry PI
 *
 * Copyright (C) 2013-2018 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_RPI
#error This file must only be compiled for raspberry PI targets
#endif

#include "portab.h"
#include "raspi_io.h"

#include "raspi_uart.h"
#include "tosvars.h"
#include "ikbd.h"
#include "string.h"
#include "kprint.h"
#include "delay.h"
#include "asm.h"
#include "raspi_mbox.h"
#include "raspi_int.h"

#if CONF_WITH_RASPI_UART0

#define GPFSEL1     (*(volatile ULONG*)(ARM_IO_BASE + 0x200004))
#define GPSET0      (*(volatile ULONG*)(ARM_IO_BASE + 0x20001C))
#define GPCLR0      (*(volatile ULONG*)(ARM_IO_BASE + 0x200028))
#define GPPUD       (*(volatile ULONG*)(ARM_IO_BASE + 0x200094))
#define GPPUDCLK0   (*(volatile ULONG*)(ARM_IO_BASE + 0x200098))

#define UART0_BASE   (ARM_IO_BASE + 0x201000)
#define UART0_DR     (*(volatile ULONG*)(UART0_BASE+0x00))
#define UART0_RSRECR (*(volatile ULONG*)(UART0_BASE+0x04))
#define UART0_FR     (*(volatile ULONG*)(UART0_BASE+0x18))
#define UART0_ILPR   (*(volatile ULONG*)(UART0_BASE+0x20))
#define UART0_IBRD   (*(volatile ULONG*)(UART0_BASE+0x24))
#define UART0_FBRD   (*(volatile ULONG*)(UART0_BASE+0x28))
#define UART0_LCRH   (*(volatile ULONG*)(UART0_BASE+0x2C))
#define UART0_CR     (*(volatile ULONG*)(UART0_BASE+0x30))
#define UART0_IFLS   (*(volatile ULONG*)(UART0_BASE+0x34))
#define UART0_IMSC   (*(volatile ULONG*)(UART0_BASE+0x38))
#define UART0_RIS    (*(volatile ULONG*)(UART0_BASE+0x3C))
#define UART0_MIS    (*(volatile ULONG*)(UART0_BASE+0x40))
#define UART0_ICR    (*(volatile ULONG*)(UART0_BASE+0x44))
#define UART0_DMACR  (*(volatile ULONG*)(UART0_BASE+0x48))
#define UART0_ITCR   (*(volatile ULONG*)(UART0_BASE+0x80))
#define UART0_ITIP   (*(volatile ULONG*)(UART0_BASE+0x84))
#define UART0_ITOP   (*(volatile ULONG*)(UART0_BASE+0x88))
#define UART0_TDR    (*(volatile ULONG*)(UART0_BASE+0x8C))

#define BAUDRATE 115200
static ULONG get_base_clock(void);

// Get the current base clock rate in Hz
static ULONG get_base_clock(void)
{
    prop_tag_2u32_t tag_clock_rate;
    tag_clock_rate.value1 = CLOCK_ID_UART;
    if (!raspi_prop_get_tag(PROPTAG_GET_CLOCK_RATE, &tag_clock_rate, sizeof tag_clock_rate, sizeof(ULONG)*2))
    {
        KINFO(("Cannot get clock rate\n"));

        tag_clock_rate.value2 = 0;
    }
    return tag_clock_rate.value2;
}

void raspi_uart0_init(void)
{
    ULONG val;
    ULONG clock_rate = get_base_clock();
	ULONG baud16 = BAUDRATE * 16;
	ULONG int_div = clock_rate / baud16;
	ULONG fractdiv2 = (clock_rate % baud16) * 8 / BAUDRATE;
	ULONG fractdiv = fractdiv2 / 2 + fractdiv2 % 2;

    UART0_CR = 0;

    val = GPFSEL1;

    // Clear gpio 14 and 15
    val &= ~(((7<<12)|(7<<15)));

    // Select alt0 for both
    val |=   ((4<<12)|(4<<15));

    GPFSEL1 = val;

    /*GPPUD = 0;
    delay_loop(loopcount_1_msec*3);
    GPPUDCLK0 = ((1<<14)|(1<<15));
    delay_loop(loopcount_1_msec*3);
    GPPUDCLK0 = 0;
    */
    UART0_IMSC = 0;
    UART0_ICR = 0x7FF;
    UART0_IBRD = int_div;
    UART0_FBRD = fractdiv;
    UART0_LCRH = (3 << 5);
    UART0_CR = 0x301;
}


BOOL raspi_uart0_can_write(void)
{
    /* Check if space is available in the FIFO */
    return (UART0_FR & 0x20) == 0;
}

void raspi_uart0_write_byte(UBYTE b)
{
    while (!raspi_uart0_can_write())
    {
        /* Wait */
    }

    /* Send the byte */
    UART0_DR = b;
}

BOOL raspi_uart0_can_read(void)
{
    /* Wait until a byte is received */
    return (UART0_FR & 0x10) == 0;
}

UBYTE raspi_uart0_read_byte(void)
{
    /* Wait until character has been received */
    while (!raspi_uart0_can_read())
    {
        /* Wait */
    }

    /* Read the received byte */
    return (UBYTE) UART0_DR;
}

#endif /* CONF_WITH_COLDFIRE_RS232 */
