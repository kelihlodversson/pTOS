/*
 * goldfish_tty.c - Goldfish TTY driver for QEMU's m68k 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_VIRT_M68K
#error This file must only be compiled for the QEMU m68k virt target
#endif

#include "portab.h"
#include "goldfish_tty.h"

#define GOLDFISH_TTY_BASE  0xff008000UL

#define TTY_PUT_CHAR      (*(volatile ULONG*)(GOLDFISH_TTY_BASE + 0x00))
#define TTY_BYTES_READY   (*(volatile ULONG*)(GOLDFISH_TTY_BASE + 0x04))
#define TTY_CMD           (*(volatile ULONG*)(GOLDFISH_TTY_BASE + 0x08))
#define TTY_DATA_PTR      (*(volatile ULONG*)(GOLDFISH_TTY_BASE + 0x10))
#define TTY_DATA_LEN      (*(volatile ULONG*)(GOLDFISH_TTY_BASE + 0x14))

#define TTY_CMD_READ_BUFFER  3

/* Scratch buffer for the DMA-style single-byte read below: the Goldfish
 * TTY has no direct "get char" register, only PUT_CHAR for output; input
 * always goes through CMD_READ_BUFFER copying into a RAM address we
 * supply via DATA_PTR/DATA_LEN. RAM sits 1:1 at its physical address on
 * this board (no MMU), so a plain address-of is already correct. */
static UBYTE goldfish_tty_rx_byte;

BOOL goldfish_tty_can_write(void)
{
    return TRUE;    /* PUT_CHAR always accepts a byte immediately on this virtual device */
}

void goldfish_tty_write_byte(UBYTE b)
{
    TTY_PUT_CHAR = b;
}

BOOL goldfish_tty_can_read(void)
{
    return TTY_BYTES_READY != 0;
}

UBYTE goldfish_tty_read_byte(void)
{
    while (!goldfish_tty_can_read())
        ;

    TTY_DATA_PTR = (ULONG)&goldfish_tty_rx_byte;
    TTY_DATA_LEN = 1;
    TTY_CMD = TTY_CMD_READ_BUFFER;

    return goldfish_tty_rx_byte;
}
