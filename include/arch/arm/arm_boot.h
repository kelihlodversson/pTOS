/*
 * arm_boot.h - the boot loader registers as ARM startup code found them
 *
 * Copyright (C) 2026 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef ARM_BOOT_H
#define ARM_BOOT_H

#if defined(MACHINE_RPI) || defined(MACHINE_VIRT_ARM)

#include "portab.h"

/*
 * Registers as they were handed to us by the boot loader: r0 is zero, r1
 * is the ARM Linux machine type (or 0xffffffff, meaning "ignore this, use
 * the device tree instead") and r2 points at the ATAG list or at a
 * flattened device tree, per the standard 32-bit ARM Linux boot protocol.
 * Startup code saves them within the first few instructions after entry,
 * before the registers are needed for anything else -- see
 * bios/machine/raspi/startup.S and bios/machine/virt-arm/startup.S.
 *
 * QEMU started with -bios (the invocation that actually boots this OS)
 * does not set these up at all, so a reader must not assume the pointer
 * is valid; a bare-metal boot loader or real hardware may not either.
 */
typedef struct {
    ULONG r0;
    ULONG machine_type;
    ULONG atags;
} arm_boot_regs_t;

extern arm_boot_regs_t arm_boot_regs;

#endif /* MACHINE_RPI || MACHINE_VIRT_ARM */
#endif /* ARM_BOOT_H */
