/*
 * pmem32.c - the physical page allocator of the 32-bit MMU configurations
 *
 * A bitmap over the pages the machine hands to pmem32_init(): one bit per
 * 4 KiB page, 1 GiB at most (the physical-memory window,
 * doc/mmu-address-space.txt).  First fit from a rotating hint, so that a
 * burst of one-page requests (page-table pages, kernel heap slabs) does not
 * scan the same full prefix each time.  The pages from pmem32_prefer() up
 * (the TT-RAM of an Atari) are used first; the pages below them (the ST-RAM,
 * the only RAM the DMA of the machine reaches) when those are full, or when
 * the caller says it needs them.  See include/pmem32.h.
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
static ULONG pref;                      /* the first preferred page; 0: all alike */
static ULONG hint_pref, hint_low;       /* where the last search of each zone ended */

/* the end of what the window of the kernel holds: 1 GiB of RAM from its base */
#define WINDOW_END      ((PADDR)PMEM32_RAM_BASE + (PADDR)MAX_PAGES * PMEM32_PAGE)

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

    if (e > WINDOW_END)                 /* (MAX_PAGES counts from `start`, which is not the base) */
        e = WINDOW_END;
    memset(used, 0xff, sizeof(used));
    first_phys = s;
    npages = 0;
    nfree = 0;
    pref = 0;
    hint_pref = hint_low = 0;
    if (e <= s)
        return;
    npages = (ULONG)((e - s) / PMEM32_PAGE);
    if (npages > MAX_PAGES)
        npages = MAX_PAGES;
    for (i = 0; i < npages; i++)
        set_page(i, FALSE);
    nfree = npages;
}

void pmem32_add(PADDR start, PADDR end)
{
    PADDR s = (start + PMEM32_PAGE - 1) & ~(PMEM32_PAGE - 1);
    PADDR e = end & ~(PMEM32_PAGE - 1);
    ULONG first, last, i;

    if (e > WINDOW_END)
        e = WINDOW_END;
    if (s < first_phys + (PADDR)npages * PMEM32_PAGE || e <= s)
        return;                         /* only above what is managed already (or above where it begins, if it has nothing) */
    first = (ULONG)((s - first_phys) / PMEM32_PAGE);
    if (first >= MAX_PAGES)
        return;
    last = (ULONG)((e - first_phys) / PMEM32_PAGE);
    if (last > MAX_PAGES || e - first_phys > (PADDR)MAX_PAGES * PMEM32_PAGE)
        last = MAX_PAGES;
    for (i = first; i < last; i++)      /* (the pages between stay in use) */
        set_page(i, FALSE);
    nfree += last - first;
    npages = last;
}

void pmem32_prefer(PADDR start)
{
    PADDR s = (start + PMEM32_PAGE - 1) & ~(PMEM32_PAGE - 1);

    if (s <= first_phys)
        pref = 0;
    else if ((s - first_phys) / PMEM32_PAGE >= MAX_PAGES)
        pref = MAX_PAGES;
    else
        pref = (ULONG)((s - first_phys) / PMEM32_PAGE);
    hint_pref = pref;
}

/* the smallest page index >= i whose physical page number is a multiple of align */
static ULONG align_up(ULONG i, ULONG align)
{
    ULONG pfn = (ULONG)(first_phys / PMEM32_PAGE) + i;

    return i + ((align - (pfn & (align - 1))) & (align - 1));
}

/* first fit in the pages [lo, hi), starting at *hintp and wrapping once */
static PADDR find(ULONG lo, ULONG hi, ULONG pages, ULONG align_pages, ULONG *hintp)
{
    ULONG i, k, j, pass;

    if (hi > npages)
        hi = npages;
    for (pass = 0; pass < 2; pass++) {
        i = align_up(pass == 0 && *hintp >= lo ? *hintp : lo, align_pages);
        while (i + pages <= hi) {
            for (k = 0; k < pages && !page_used(i + k); k++)
                ;
            if (k == pages) {
                for (j = i; j < i + pages; j++)
                    set_page(j, TRUE);
                nfree -= pages;
                *hintp = i + pages >= hi ? lo : i + pages;
                return first_phys + (PADDR)i * PMEM32_PAGE;
            }
            i = align_up(i + k + 1, align_pages);
        }
    }
    return PMEM32_NONE;
}

PADDR pmem32_alloc_low(ULONG pages, ULONG align_pages)
{
    if (!pages || pages > nfree)
        return PMEM32_NONE;
    return find(0, pref ? pref : npages, pages, align_pages ? align_pages : 1,
                pref ? &hint_low : &hint_pref);
}

PADDR pmem32_alloc(ULONG pages, ULONG align_pages)
{
    PADDR p;

    if (!pages || pages > nfree)
        return PMEM32_NONE;
    if (!align_pages)
        align_pages = 1;
    p = find(pref, npages, pages, align_pages, &hint_pref);
    if (p == PMEM32_NONE && pref)
        p = find(0, pref, pages, align_pages, &hint_low);
    return p;
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
    if (first < pref) {
        if (first < hint_low)
            hint_low = first;
    } else if (first < hint_pref) {
        hint_pref = first;
    }
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
