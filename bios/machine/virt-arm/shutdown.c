/*
 * shutdown.c - restart and power off of QEMU's ARM 'virt' machine, for
 * Shutdown()
 *
 * PSCI (the Power State Coordination Interface), SYSTEM_OFF and
 * SYSTEM_RESET, through an HVC: QEMU's 'virt' board without virtualization
 * or secure state uses the hypervisor call conduit.
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_VIRT_ARM
#error This file must only be compiled for the QEMU ARM virt target
#endif

#include "portab.h"
#include "biosext.h"

#define PSCI_SYSTEM_OFF     0x84000008UL
#define PSCI_SYSTEM_RESET   0x84000009UL

static void psci(ULONG function)
{
    register ULONG r0 __asm__("r0") = function;

    __asm__ volatile (
        ".arch_extension virt\n\t"
        "hvc #0"
        : "+r"(r0) : : "r1", "r2", "r3", "memory");
}

void machine_poweroff(void)
{
    psci(PSCI_SYSTEM_OFF);
    /* still here: no PSCI */
}

void machine_reset(BOOL cold)
{
    (void)cold;
    psci(PSCI_SYSTEM_RESET);
}
