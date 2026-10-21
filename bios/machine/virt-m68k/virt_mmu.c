/*
 * virt_mmu.c - the higher-half kernel of QEMU's m68k 'virt' machine: the
 * translation of addresses (the tables are built by virt_mmu_early.S)
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "kprint.h"
#include "virt_mmu.h"

#if CONF_WITH_HIGHER_HALF

ULONG virt_to_phys(const void *va)
{
    ULONG a = (ULONG)(uintptr_t)va;

    if (a >= VIRT_WINDOW_VBASE && a - VIRT_WINDOW_VBASE < VIRT_MAX_RAM_SIZE)
        return a - VIRT_WINDOW_VBASE;
    if (a < VIRT_MAX_RAM_SIZE)          /* the pool and the low memory */
        return a;

    KDEBUG(("virt_to_phys(%p): not RAM\n", va));
    return 0;
}

UBYTE *virt_pool_start(UBYTE *kernel_end)
{
    return kernel_end - VIRT_WINDOW_VBASE;
}

#endif /* CONF_WITH_HIGHER_HALF */
