/*
 * shutdown.c - restart and power off of a Raspberry Pi, for Shutdown()
 *
 * Both through the power management watchdog: a reset is the watchdog going
 * off at once; the firmware's "halt" partition (written to PM_RSTS first)
 * makes the same reset stop at the bootloader, which is as close to a power
 * off as the board gets.
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_RPI
#error This file must only be compiled for the Raspberry Pi machines
#endif

#include "portab.h"
#include "biosext.h"
#include "raspi_io.h"

#define PM_BASE         (ARM_IO_BASE + 0x100000)
#define PM_RSTC         (*(volatile ULONG *)(PM_BASE + 0x1c))
#define PM_RSTS         (*(volatile ULONG *)(PM_BASE + 0x20))
#define PM_WDOG         (*(volatile ULONG *)(PM_BASE + 0x24))
#define PM_PASSWORD     0x5a000000UL
#define PM_RSTC_FULLRST 0x00000020UL
#define PM_RSTS_PARTITION_CLR   0xfffffaaaUL
#define PM_RSTS_HALT    0x00000555UL    /* partition 63: the bootloader halts */

static void watchdog_reset(void)
{
    PM_WDOG = PM_PASSWORD | 10;         /* a few ticks of 16 us */
    PM_RSTC = PM_PASSWORD | PM_RSTC_FULLRST;
}

void machine_poweroff(void)
{
    PM_RSTS = PM_PASSWORD | (PM_RSTS & PM_RSTS_PARTITION_CLR) | PM_RSTS_HALT;
    watchdog_reset();
}

void machine_reset(BOOL cold)
{
    (void)cold;
    watchdog_reset();
}
