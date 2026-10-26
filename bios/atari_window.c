/*
 * atari_window.c - the Atari ROM that runs in the upper half: the translation
 * of addresses (the tree is the static one of bios/arch/m68k/atari_mmu.S)
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "emutos.h"
#include "atari_mmu.h"

#if CONF_ATARI_HIGHER_HALF

ULONG virt_to_phys(const void *va)
{
    ULONG a = (ULONG)(uintptr_t)va;

    if (a >= ATARI_WINDOW_VBASE && a < ATARI_WINDOW_VBASE + 0x20000000UL)
        return a - ATARI_WINDOW_VBASE;
    if (a < 0x20000000UL)               /* the pool and the low memory */
        return a;

    KDEBUG(("virt_to_phys(%p): not RAM\n", va));
    return 0;
}

UBYTE *virt_pool_start(UBYTE *kernel_end)
{
    return kernel_end - ATARI_WINDOW_VBASE;
}

#endif /* CONF_ATARI_HIGHER_HALF */
