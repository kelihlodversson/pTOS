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
#if CONF_WITH_PROCESS_ASPACES
    if (a < 0x8000UL)                   /* the low memory, where it is (the disk buffer) */
        return a;
#else
    if (a < VIRT_MAX_RAM_SIZE)          /* the pool and the low memory */
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
    return kernel_end - VIRT_WINDOW_VBASE;
#endif
}

#if CONF_WITH_PROCESS_ASPACES
extern ULONG boot_root;                 /* startup.S */

/*
 * A reset comes back to startup.S, which goes to the physical alias of the
 * code: the tree the MMU was first switched on with maps it, and the trees of
 * the processes do not.  Load it again (and flush) before the jump.
 */
void machine_reset_prepare(void)
{
    __asm__ volatile ("move.l %0,%%d0\n\t.long 0x4e7b0806\n\t.long 0x4e7b0807\n\t.word 0xf518"
                      : : "g" (boot_root) : "d0", "memory");        /* movec d0,urp/srp, pflusha */
}
#endif

#endif /* CONF_WITH_HIGHER_HALF */
