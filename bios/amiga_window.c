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
#if CONF_WITH_PROCESS_ASPACES
    if (a < 0x8000UL)                   /* the low memory, where it is (the disk buffer) */
        return a;
#else
    if (a < 0x01000000UL)               /* the pool and the low memory */
        return a;
#endif

    KDEBUG(("virt_to_phys(%p): not RAM\n", va));
    return 0;
}

UBYTE *virt_pool_start(UBYTE *kernel_end)
{
#if CONF_WITH_PROCESS_ASPACES
    return kernel_end;                  /* the memory of the kernel is all in the window */
#else
    return kernel_end - AMIGA_WINDOW_VBASE;
#endif
}

#if CONF_WITH_PROCESS_ASPACES && !CONF_AMIGA_HIGHER_HALF_68040
extern const ULONG amiga_mmu_crp[2];    /* amiga_mmu.S */

/*
 * A reset comes back to startup.S, which goes to the low alias of the code:
 * the static tree maps it, and the trees of the processes do not.  Load it
 * again (and flush) before the jump.
 */
void machine_reset_prepare(void)
{
    __asm__ volatile ("move.l %0,%%a0\n\t.long 0xf0104c00\n\t.long 0xf0002400"
                      : : "g" (&amiga_mmu_crp[0]) : "a0", "memory");    /* pmove (a0),crp; pflusha */
}
#endif

#endif /* CONF_AMIGA_HIGHER_HALF */
