/*
 * intmask.c - saving and restoring the interrupt mask
 *
 * Copyright 2002-2017, The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*
 * These lived in aes/arch/arm/gemdosifc.c while the AES was their only
 * caller.  The VDI calls them as well now, in vdi_misc.c and vdi_mouse.c,
 * where they took the place of set_sr() because set_sr() manipulates the
 * m68k status register and has no meaning on ARM.  The VDI is built even in
 * the configurations that leave the AES out, such as the diagnostic
 * cartridge, so the definitions have to come from something always built:
 * the BIOS.
 *
 * There is a single save slot, so these do not nest.
 */

#include "config.h"
#include "portab.h"
#include "asm.h"

static ULONG save_cpsr;

static inline ULONG read_cpsr(void)
{
    unsigned int res;
    asm volatile (
        "mrs     %0, cpsr"
        : "=r"(res)
    );
    return res;
}

static inline void write_cpsr(ULONG cpsr)
{
    asm volatile (
        "msr     cpsr_c, %0"
        :
        : "r"(cpsr)
    );
}

/* disable interrupts */
ULONG disable_interrupts(void)
{
    save_cpsr = read_cpsr();
    asm volatile ("cpsid if");
    return save_cpsr;
}

/* restore interrupt mask as it was before disable_interrupts() */
void enable_interrupts(void)
{
    write_cpsr(save_cpsr);
}
