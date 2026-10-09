/*
 * pit.c - x86-64 legacy 8253/8254 PIT (programmable interval timer)
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_PC_X86_64
#error This file must only be compiled for the pc-x86_64 machine
#endif

#include "portab.h"
#include "io.h"
#include "pit.h"

#define PIT_CHANNEL0_DATA 0x40
#define PIT_COMMAND       0x43

/* Channel 0, mode 2 (rate generator), lobyte/hibyte access, binary
 * (not BCD) counting -- the standard "periodic IRQ0 tick" setup every
 * PC BIOS/OS has used since the original IBM PC. */
#define PIT_CMD_CHANNEL0_MODE2_LOHI 0x34

/* The PIT's own fixed input clock (Intel 8254 datasheet, and the
 * documented frequency of the IBM PC/AT's crystal-derived clock every
 * PC-compatible chipset, real or emulated, still honours): 1.193182 MHz.
 * Truncated to a whole number for the reload-value division below --
 * the resulting ~0.0026% rate error is far smaller than this tick's own
 * consumers (key repeat timing, VBL emulation) need to be exact to. */
#define PIT_INPUT_HZ 1193182UL

void x86_64_pit_init(UWORD hz)
{
    ULONG divisor = PIT_INPUT_HZ / hz;

    x86_64_outb(PIT_COMMAND, PIT_CMD_CHANNEL0_MODE2_LOHI);
    x86_64_outb(PIT_CHANNEL0_DATA, (UBYTE)(divisor & 0xFF));
    x86_64_outb(PIT_CHANNEL0_DATA, (UBYTE)((divisor >> 8) & 0xFF));
}
