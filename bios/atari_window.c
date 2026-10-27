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
#if CONF_WITH_PROCESS_ASPACES
    if (a < 0x8000UL)                   /* the low memory, where it is (the disk buffer) */
        return a;
#else
    if (a < 0x20000000UL)               /* the pool and the low memory */
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
    return kernel_end - ATARI_WINDOW_VBASE;
#endif
}

#if CONF_WITH_PROCESS_ASPACES
extern const ULONG atari_mmu_crp[2];    /* atari_mmu.S */

/*
 * A reset comes back to startup.S, which goes to the low alias of the code:
 * the static tree maps it, and the trees of the processes do not.  Load it
 * again (and flush) before the jump.
 */
void machine_reset_prepare(void)
{
    __asm__ volatile ("move.l %0,%%a0\n\t.long 0xf0104c00\n\t.long 0xf0002400"
                      : : "g" (&atari_mmu_crp[0]) : "a0", "memory");    /* pmove (a0),crp; pflusha */
}
#endif

#endif /* CONF_ATARI_HIGHER_HALF */
