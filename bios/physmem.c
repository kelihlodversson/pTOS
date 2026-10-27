/*
 * physmem.c - how much physical memory there is, for Sysconf()
 *
 * The default: the machine's memory top (phystop) in pages, and no knowledge of
 * what is free, because the memory pools of the TPA are not a count of pages
 * (but with address spaces of their own the page allocator counts them).
 * A machine that has a page allocator says more in
 * bios/machine/<name>/physmem.c, the same two functions.
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "emutos.h"
#include "tosvars.h"
#include "biosext.h"
#if CONF_WITH_PROCESS_ASPACES
#include "pmem32.h"
#endif

LONG phys_pages_total(void)
{
#if CONF_WITH_PROCESS_ASPACES
    /* phystop is an address of the kernel window: the RAM starts at its base */
    ULONG bytes = (ULONG)(uintptr_t)phystop - (ULONG)PMEM32_WINDOW_BASE;

#if CONF_ATARI_HIGHER_HALF && !CONF_ATARI_HIGHER_HALF_68040 && CONF_WITH_TTRAM
    if (ramtop)                         /* the TT-RAM is pages of the allocator too */
        bytes += (ULONG)(uintptr_t)ramtop - (ULONG)(uintptr_t)TTRAM_START;
#endif
    return (LONG)(bytes / 4096UL);
#else
    return (LONG)((ULONG)(uintptr_t)phystop / 4096UL);
#endif
}

LONG phys_pages_free(void)
{
#if CONF_WITH_PROCESS_ASPACES
    /* the pages the allocator of the address spaces can still hand out */
    return (LONG)(pmem32_free_bytes() / 4096UL);
#else
    return -1;
#endif
}
