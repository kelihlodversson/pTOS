/*
 * pmem32.c - the physical page allocator of the 32-bit MMU configurations
 *
 * A bitmap over the pages the machine hands to pmem32_init(): one bit per
 * 4 KiB page, 1 GiB at most (the physical-memory window,
 * doc/mmu-address-space.txt).  First fit from a rotating hint, so that a
 * burst of one-page requests (page-table pages, kernel heap slabs) does not
 * scan the same full prefix each time.  See include/pmem32.h.
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"

#if CONF_WITH_USER_ASPACE && !defined(__x86_64__)

#include "portab.h"
#include "string.h"
#include "pmem32.h"

#define MAX_PAGES   (0x40000000UL / PMEM32_PAGE)        /* the 1 GiB of the window */

static UBYTE used[MAX_PAGES / 8];       /* bit set: not free (or not ours) */
static PADDR first_phys;                /* the first page managed */
static ULONG npages;                    /* pages managed */
static ULONG nfree;
static ULONG hint;                      /* where the last search ended */

static BOOL page_used(ULONG i)
{
    return (used[i >> 3] >> (i & 7)) & 1;
}

static void set_page(ULONG i, BOOL value)
{
    if (value)
        used[i >> 3] |= (UBYTE)(1 << (i & 7));
    else
        used[i >> 3] &= (UBYTE)~(1 << (i & 7));
}

void pmem32_init(PADDR start, PADDR end)
{
    PADDR s = (start + PMEM32_PAGE - 1) & ~(PMEM32_PAGE - 1);
    PADDR e = end & ~(PMEM32_PAGE - 1);
    ULONG i;

    memset(used, 0xff, sizeof(used));
    first_phys = s;
    npages = 0;
    nfree = 0;
    hint = 0;
    if (e <= s)
        return;
    npages = (ULONG)((e - s) / PMEM32_PAGE);
    if (npages > MAX_PAGES)
        npages = MAX_PAGES;
    for (i = 0; i < npages; i++)
        set_page(i, FALSE);
    nfree = npages;
}

/* the smallest page index >= i whose physical page number is a multiple of align */
static ULONG align_up(ULONG i, ULONG align)
{
    ULONG pfn = (ULONG)(first_phys / PMEM32_PAGE) + i;

    return i + ((align - (pfn & (align - 1))) & (align - 1));
}

PADDR pmem32_alloc(ULONG pages, ULONG align_pages)
{
    ULONG i, k, j, pass;

    if (!pages || pages > nfree)
        return PMEM32_NONE;
    if (!align_pages)
        align_pages = 1;

    for (pass = 0; pass < 2; pass++) {
        i = align_up(pass == 0 ? hint : 0, align_pages);
        while (i + pages <= npages) {
            for (k = 0; k < pages && !page_used(i + k); k++)
                ;
            if (k == pages) {
                for (j = i; j < i + pages; j++)
                    set_page(j, TRUE);
                nfree -= pages;
                hint = i + pages >= npages ? 0 : i + pages;
                return first_phys + (PADDR)i * PMEM32_PAGE;
            }
            i = align_up(i + k + 1, align_pages);
        }
    }
    return PMEM32_NONE;
}

void pmem32_free(PADDR phys, ULONG pages)
{
    ULONG i, first;

    if (phys < first_phys || (phys & (PMEM32_PAGE - 1)))
        return;
    first = (ULONG)((phys - first_phys) / PMEM32_PAGE);
    for (i = 0; i < pages && first + i < npages; i++) {
        if (page_used(first + i)) {
            set_page(first + i, FALSE);
            nfree++;
        }
    }
    if (first < hint)
        hint = first;
}

BOOL pmem32_is_allocated(PADDR phys)
{
    ULONG i;

    if (phys < first_phys)
        return FALSE;
    i = (ULONG)((phys - first_phys) / PMEM32_PAGE);
    return i < npages && page_used(i);
}

ULONG pmem32_free_bytes(void)
{
    return nfree * (ULONG)PMEM32_PAGE;
}

ULONG pmem32_managed_bytes(void)
{
    return npages * (ULONG)PMEM32_PAGE;
}

PADDR pmem32_highest_addr(void)
{
    return first_phys + (PADDR)npages * PMEM32_PAGE;
}

#endif /* CONF_WITH_USER_ASPACE */
