/*
 * shutdown.c - restart and power off of a PC, for Shutdown()
 *
 * Power off: the ACPI sleep control register of the emulators (QEMU's PIIX4
 * and ICH9 at 0x604, Bochs and older QEMU at 0xb004), written with
 * SLP_TYP S5 | SLP_EN.  Real hardware has that register somewhere else, which
 * the FADT of the firmware's ACPI tables says; reading it is not done yet, so
 * such a machine just stops with the message.
 *
 * Restart: a reset pulse through the keyboard controller, and where that does
 * nothing a triple fault (an interrupt with an empty IDT).
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
#include "biosext.h"
#include "io.h"
#include "port_io.h"

#define ACPI_SLP_S5_EN      0x2000      /* SLP_EN | SLP_TYP(0) of QEMU's S5 */
#define PM1A_CNT_QEMU       0x604
#define PM1A_CNT_BOCHS      0xb004

void machine_poweroff(void)
{
    x86_64_cli();
    port_outw(PM1A_CNT_QEMU, ACPI_SLP_S5_EN);
    port_outw(PM1A_CNT_BOCHS, ACPI_SLP_S5_EN);
    /* still here: no such register */
}

void machine_reset(BOOL cold)
{
    struct { UWORD limit; UQUAD base; } __attribute__((packed)) null_idt = { 0, 0 };

    (void)cold;                 /* a PC keeps no warm boot marker */
    x86_64_cli();
    port_outb(0x64, 0xfe);      /* pulse the CPU reset line */
    __asm__ volatile ("lidt %0\n\tint3" : : "m"(null_idt));
    /* still here: neither worked */
}
