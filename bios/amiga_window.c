/*
 * amiga_window.c - the Amiga ROM that runs in the upper half: the translation
 * of addresses (the tree is the static one of bios/arch/m68k/amiga_mmu.S)
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "emutos.h"
#include "amiga_mmu.h"

#if CONF_AMIGA_HIGHER_HALF

ULONG virt_to_phys(const void *va)
{
    ULONG a = (ULONG)(uintptr_t)va;

    if (a >= AMIGA_WINDOW_VBASE && a < AMIGA_WINDOW_VBASE + 0x01000000UL)
        return a - AMIGA_WINDOW_VBASE;
    if (a < 0x01000000UL)               /* the pool and the low memory */
        return a;

    KDEBUG(("virt_to_phys(%p): not RAM\n", va));
    return 0;
}

UBYTE *virt_pool_start(UBYTE *kernel_end)
{
    return kernel_end - AMIGA_WINDOW_VBASE;
}

#endif /* CONF_AMIGA_HIGHER_HALF */
