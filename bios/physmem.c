/*
 * physmem.c - how much physical memory there is, for Sysconf()
 *
 * The default: the machine's memory top (phystop) in pages, and no knowledge of
 * what is free, because the memory pools of the TPA are not a count of pages.
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
    return (LONG)(((ULONG)(uintptr_t)phystop - (ULONG)PMEM32_WINDOW_BASE) / 4096UL);
#else
    return (LONG)((ULONG)(uintptr_t)phystop / 4096UL);
#endif
}

LONG phys_pages_free(void)
{
    return -1;
}
