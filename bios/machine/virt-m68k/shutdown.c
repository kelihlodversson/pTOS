/*
 * shutdown.c - restart and power off of QEMU's m68k 'virt' machine, for
 * Shutdown()
 *
 * The virt-ctrl device: one command register, and a command each for reset
 * and halt (QEMU leaves on a halt, there is nothing to switch off).
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_VIRT_M68K
#error This file must only be compiled for the QEMU m68k virt target
#endif

#include "portab.h"
#include "biosext.h"

#define VIRT_CTRL_BASE      0xff009000UL
#define VIRT_CTRL_CMD       (*(volatile ULONG *)(VIRT_CTRL_BASE + 0x04))
#define CMD_HALT            2UL
#define CMD_RESET           1UL

void machine_poweroff(void)
{
    VIRT_CTRL_CMD = CMD_HALT;
}

void machine_reset(BOOL cold)
{
    (void)cold;
    VIRT_CTRL_CMD = CMD_RESET;
}
