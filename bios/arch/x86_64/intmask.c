/*
 * intmask.c - saving and restoring the interrupt mask
 *
 * Copyright 2002-2017, The EmuTOS development team
 * Copyright (C) 2018-2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*
 * Mirrors bios/arch/arm/intmask.c: a single, non-nesting save slot,
 * needed by the VDI (vdi_misc.c, vdi_mouse.c) in place of set_sr(),
 * which has no meaning here any more than it does on ARM. RFLAGS.IF is
 * the x86 equivalent of ARM's CPSR interrupt-mask bits.
 */

#include "config.h"
#include "portab.h"
#include "asm.h"

static ULONG save_rflags;

/* disable interrupts */
ULONG disable_interrupts(void)
{
    UQUAD rflags;

    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(rflags) :: "memory");
    save_rflags = (ULONG)rflags;
    return save_rflags;
}

/* restore interrupt mask as it was before disable_interrupts() */
void enable_interrupts(void)
{
    __asm__ volatile ("pushq %0; popfq" :: "r"((UQUAD)save_rflags) : "memory", "cc");
}
