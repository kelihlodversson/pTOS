/*
 * procmem.c - tracked process memory window (32-bit MMU configurations)
 *
 * See include/procmem.h for the contract.  The window is PROCMEM_WINDOW_BYTES
 * of virtual address space at PROCMEM_WINDOW_BASE (doc/mmu-address-space.txt:
 * 0x00200000 - 0x003fffff of the user half), below USER_VA_LIMIT, so any
 * address in it fits the 32-bit GEMDOS ABI fields.  Its physical backing is
 * one contiguous block of pages taken from the page allocator wherever it
 * happens to lie; aspace.c maps it (supervisor-only) into the kernel's own
 * address space, and a process's address space maps the blocks it owns.
 * (The x86-64 kernel has its own, bios/machine/pc-x86_64/procmem.c: found
 * first through vpath.)
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"

#if CONF_WITH_PROCMEM && !defined(__x86_64__)

#include "portab.h"
#include "string.h"
#include "kheap.h"
#include "procmem.h"
#include "pmem32.h"
#include "aspace32.h"

#define PAGE            PMEM32_PAGE
#define WINDOW_PAGES    (PROCMEM_WINDOW_BYTES / PAGE)
#define WORDS           ((WINDOW_PAGES + 31) / 32)

/* The whole window must be representable in a 32-bit ABI field. */
typedef char window_is_below_limit[
    (PROCMEM_WINDOW_BASE + PROCMEM_WINDOW_BYTES <= USER_VA_LIMIT) ? 1 : -1];

/* The documented program image region starts past the process window and
 * the stack ends at the top of the user half. */
typedef char image_follows_window[
    (USER_IMAGE_BASE >= PROCMEM_WINDOW_BASE + PROCMEM_WINDOW_BYTES) ? 1 : -1];
typedef char stack_below_limit[
    (USER_STACK_TOP <= USER_VA_LIMIT &&
     USER_STACK_SIZE <= USER_STACK_TOP &&   /* before subtracting: no wrap */
     USER_IMAGE_BASE + USER_IMAGE_SIZE <= USER_STACK_TOP - USER_STACK_SIZE) ? 1 : -1];

struct alloc {
    struct alloc *next;
    ULONG va;
    ULONG pages;
    const void *owner;          /* identity of the owning process, or NULL */
    ULONG pins;                 /* live address-space mappings of this block */
    UQUAD gen;                  /* unique per allocation, never reused */
};

static UQUAD next_gen = 1;

static PADDR window_phys;       /* physical base of the backing block */
static ULONG used[WORDS];       /* bit per window page: in a live allocation */
static struct alloc *allocs;
static ULONG live_allocs;
static ULONG free_pages;
static ULONG bad_frees;

static BOOL page_used(ULONG i)
{
    return (used[i / 32] >> (i % 32)) & 1;
}

static void set_pages(ULONG first, ULONG n, BOOL value)
{
    while (n--) {
        if (value)
            used[first / 32] |= 1UL << (first % 32);
        else
            used[first / 32] &= ~(1UL << (first % 32));
        first++;
    }
}

/* Takes the backing pages; aspace.c maps them into the kernel's address space. */
BOOL procmem_window_init(PADDR *phys)
{
    PADDR p = pmem32_alloc(WINDOW_PAGES, 1);

    if (p == PMEM32_NONE)
        return FALSE;
    window_phys = p;
    memset(pmem32_virt(p), 0, PROCMEM_WINDOW_BYTES);
    memset(used, 0, sizeof(used));
    free_pages = WINDOW_PAGES;
    *phys = p;
    return TRUE;
}

BOOL procmem_contains(const void *p)
{
    ULONG a = (ULONG)(uintptr_t)p;

    return a >= PROCMEM_WINDOW_BASE &&
           a < PROCMEM_WINDOW_BASE + PROCMEM_WINDOW_BYTES;
}

PADDR procmem_phys_of(VADDR va)
{
    return window_phys + (va - PROCMEM_WINDOW_BASE);
}

/* First-fit search for `pages` consecutive free window pages. */
static BOOL find_run(ULONG pages, ULONG *first)
{
    ULONG i, run = 0;

    for (i = 0; i < WINDOW_PAGES; i++) {
        if (page_used(i)) {
            run = 0;
            continue;
        }
        if (++run == pages) {
            *first = i + 1 - pages;
            return TRUE;
        }
    }
    return FALSE;
}

void *procmem_alloc(ULONG bytes, UWORD flags)
{
    struct alloc *a;
    ULONG pages, first;
    ULONG va;

    if (!bytes || !window_phys ||
        bytes > PROCMEM_WINDOW_BYTES)       /* also rules out overflow below */
        return NULL;
    pages = (bytes + PAGE - 1) / PAGE;
    if (pages > free_pages || !find_run(pages, &first))
        return NULL;

    a = kalloc(sizeof(*a));
    if (!a)
        return NULL;

    va = PROCMEM_WINDOW_BASE + first * PAGE;
    a->va = va;
    a->pages = pages;
    a->owner = NULL;
    a->pins = 0;
    a->gen = next_gen++;
    a->next = allocs;
    allocs = a;

    set_pages(first, pages, TRUE);
    free_pages -= pages;
    live_allocs++;

    /* Zero through the window of the kernel half: unlike the low alias, it is
     * present whichever address space is currently loaded. */
    if (flags & PROCMEM_ZERO)
        memset(pmem32_virt(procmem_phys_of(va)), 0, pages * PAGE);
    return (void *)(uintptr_t)va;
}

static struct alloc **find_link(const void *p)
{
    struct alloc **link;

    for (link = &allocs; *link; link = &(*link)->next)
        if ((*link)->va == (ULONG)(uintptr_t)p)
            return link;
    return NULL;
}

static void release(struct alloc **link)
{
    struct alloc *a = *link;

    *link = a->next;
    set_pages((ULONG)((a->va - PROCMEM_WINDOW_BASE) / PAGE),
              a->pages, FALSE);
    free_pages += a->pages;
    live_allocs--;
    kfree(a);
}

BOOL procmem_free(void *p)
{
    struct alloc **link = find_link(p);

    if (!link) {                /* not a live allocation base */
        bad_frees++;
        return FALSE;
    }
    if ((*link)->pins)          /* still mapped into a live address space */
        return FALSE;
    release(link);
    return TRUE;
}

UQUAD procmem_next_gen(void)
{
    return next_gen;
}

ULONG procmem_free_ownerless_since(UQUAD gen)
{
    ULONG freed = 0;
    struct alloc **link = &allocs;

    while (*link) {
        if ((*link)->gen >= gen && !(*link)->owner && !(*link)->pins) {
            release(link);              /* unlinks, so *link is the next one */
            freed++;
        } else {
            link = &(*link)->next;
        }
    }
    return freed;
}

BOOL procmem_pinned(const void *p)
{
    struct alloc **link = find_link(p);

    return link && (*link)->pins;
}

ULONG procmem_pins(const void *p)
{
    struct alloc **link = find_link(p);

    return link ? (*link)->pins : 0;
}

void procmem_pin(VADDR va, VADDR bytes, int delta)
{
    struct alloc *a;

    for (a = allocs; a; a = a->next)
        if (a->va < va + bytes && va < a->va + (ULONG)a->pages * PAGE) {
            if (delta > 0)
                a->pins++;
            else if (a->pins)
                a->pins--;
        }
}

UQUAD procmem_gen(const void *p)
{
    struct alloc **link = find_link(p);

    return link ? (*link)->gen : 0;
}

ULONG procmem_size(const void *p)
{
    struct alloc **link = find_link(p);

    return link ? (*link)->pages * (ULONG)PAGE : 0;
}

BOOL procmem_range_live(VADDR va, VADDR bytes)
{
    ULONG first, last, i;

    if (!bytes || va < PROCMEM_WINDOW_BASE ||
        va + bytes < va ||
        va + bytes > PROCMEM_WINDOW_BASE + PROCMEM_WINDOW_BYTES)
        return FALSE;
    first = (va - PROCMEM_WINDOW_BASE) / PAGE;
    last = (va + bytes - 1 - PROCMEM_WINDOW_BASE) / PAGE;
    for (i = first; i <= last; i++)
        if (!page_used((ULONG)i))
            return FALSE;
    return TRUE;
}

const void *procmem_owner(const void *p)
{
    struct alloc **link = find_link(p);

    return link ? (*link)->owner : NULL;
}

void procmem_set_owner(const void *p, const void *owner)
{
    struct alloc **link = find_link(p);

    if (link)
        (*link)->owner = owner;
}

void procmem_free_owned(const void *owner, void (*pre_free)(void *base))
{
    struct alloc **link;
    struct alloc *a;

    if (!owner)                 /* NULL means "permanent", never "any" */
        return;

    /* First let the caller drop whatever is keyed by each block (a basepage's
     * KPROC record, and with it that process's address space and the pins
     * it holds), so the second pass sees which blocks are really free to go. */
    if (pre_free)
        for (a = allocs; a; a = a->next)
            if (a->owner == owner)
                pre_free((void *)(uintptr_t)a->va);

    /* A block still mapped into some other live address space stays
     * allocated (and owned): reusing it would hand that process's view of
     * it to a new owner. */
    link = &allocs;
    while (*link) {
        if ((*link)->owner == owner && !(*link)->pins)
            release(link);      /* unlinks: *link is now the next one */
        else
            link = &(*link)->next;
    }
}

void procmem_keep(const void *owner, ULONG keep_bytes, void (*pre_keep)(void *base))
{
    struct alloc *a;

    if (!owner)
        return;
    for (a = allocs; a; a = a->next) {
        if (a->owner != owner)
            continue;
        if (a->va == (ULONG)(uintptr_t)owner) {
            /* the owner's own block: keep only the requested prefix, in
             * whole pages, and give the tail back -- unless a live address
             * space can still reach it */
            ULONG keep = (ULONG)((keep_bytes + PAGE - 1) / PAGE);

            if (keep_bytes && keep && keep < a->pages && !a->pins) {
                set_pages((ULONG)((a->va - PROCMEM_WINDOW_BASE) / PAGE) + keep,
                          a->pages - keep, FALSE);
                free_pages += a->pages - keep;
                a->pages = keep;
            }
        } else if (pre_keep) {
            pre_keep((void *)(uintptr_t)a->va);
        }
        a->owner = NULL;
    }
}

void procmem_stats(PROCMEM_STATS *stats)
{
    stats->live_allocs = live_allocs;
    stats->total_pages = WINDOW_PAGES;
    stats->free_pages = free_pages;
    stats->bad_frees = bad_frees;
}

#endif /* CONF_WITH_PROCMEM */
