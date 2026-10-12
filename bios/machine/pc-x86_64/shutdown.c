/*
 * shutdown.c - restart and power off of a PC, for Shutdown()
 *
 * Power off: the ACPI sleep control register of QEMU (PIIX4 and ICH9 at
 * 0x604, older machine types at 0xb004), written with SLP_TYP S5 | SLP_EN --
 * only when the CPU says it is QEMU's (the hypervisor bit and the TCG or KVM
 * signature), because on any other machine those ports can be something else
 * and a write is not harmless.  Real hardware has the register where the FADT
 * of the firmware's ACPI tables says; reading it is not done yet, so such a
 * machine, and every other hypervisor, just stops with the message.
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

static void cpuid(ULONG leaf, ULONG *r)
{
    __asm__ volatile ("cpuid"
              : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3])
              : "a"(leaf), "c"(0));
}

/* QEMU, by what its CPU models answer: a hypervisor, signed "TCGTCGTCGTCG"
 * (its own emulation) or "KVMKVMKVM" (KVM under it) */
static BOOL running_on_qemu(void)
{
    ULONG r[4];
    static const char tcg[12] = "TCGTCGTCGTCG";
    static const char kvm[12] = "KVMKVMKVM\0\0\0";
    char sig[12];
    int i;

    cpuid(1, r);
    if (!(r[2] & (1UL << 31)))
        return FALSE;               /* no hypervisor */
    cpuid(0x40000000UL, r);
    for (i = 0; i < 4; i++)
    {
        sig[i] = (char)(r[1] >> (8 * i));
        sig[4 + i] = (char)(r[2] >> (8 * i));
        sig[8 + i] = (char)(r[3] >> (8 * i));
    }
    for (i = 0; i < 12 && sig[i] == tcg[i]; i++)
        ;
    if (i == 12)
        return TRUE;
    for (i = 0; i < 12 && sig[i] == kvm[i]; i++)
        ;
    return i == 12;
}

void machine_poweroff(void)
{
    if (!running_on_qemu())
        return;
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
