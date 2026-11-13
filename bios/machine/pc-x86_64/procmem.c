/*
 * procmem.c - tracked process memory window (x86-64)
 *
 * See include/procmem.h for the contract.  The window is
 * X86_64_LOW_TPA_BYTES of virtual address space at X86_64_LOW_TPA_VIRT_BASE,
 * mapped virtual-equals-kernel-pointer in the kernel's own page tables so
 * the BDOS can dereference a process's basepage and TPA directly.  Its
 * physical backing is one contiguous, 2 MiB-aligned block of RAM taken from
 * the physical page allocator wherever it happens to lie: that address is
 * a physical placement decision, independent of the window's low VIRTUAL
 * address, so the backing may be above 4 GiB.
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_PC_X86_64
#error This file must only be compiled for the pc-x86_64 machine
#endif

#include "portab.h"
#include "string.h"
#include "kheap.h"
#include "procmem.h"
#include "pc_x86_64_memory.h"
#include "pgtable.h"
#include "pmem.h"

#define WINDOW_PAGES    (X86_64_LOW_TPA_BYTES / X86_64_PAGE_SIZE)
#define WORDS           ((WINDOW_PAGES + 63) / 64)

/* The whole window must be representable in a 32-bit ABI field. */
typedef char window_is_below_4gib[
    (X86_64_LOW_TPA_VIRT_BASE + X86_64_LOW_TPA_BYTES <= USER_VA_LIMIT) ? 1 : -1];

/* The documented program image region starts past the process window and
 * the stack ends at the 1 GiB mark, both well inside the 32-bit ABI limit. */
typedef char image_follows_window[
    (USER_IMAGE_BASE >= X86_64_LOW_TPA_VIRT_BASE + X86_64_LOW_TPA_BYTES) ? 1 : -1];
typedef char stack_below_limit[
    (USER_STACK_TOP <= USER_VA_LIMIT &&
     USER_STACK_SIZE <= USER_STACK_TOP &&   /* before subtracting: no wrap */
     USER_IMAGE_BASE + USER_IMAGE_SIZE <= USER_STACK_TOP - USER_STACK_SIZE) ? 1 : -1];

struct allocation_hold;
struct alloc {
    struct alloc *next;
    UQUAD va;
    ULONG pages;
    const void *owner;          /* identity of the owning process, or NULL */
    struct allocation_hold *holds;
    ULONG hold_count;
    BOOL cleanup_owned; /* one-way canonical disposal owner, never a user grant */
    ULONG pins;                 /* live address-space mappings of this block */
    void (*notify)(void *);
    void *notify_token;
    BOOL reclaim;               /* allocation itself owns deferred reclamation */
    UQUAD gen;                  /* unique per allocation, never reused */
};

static UQUAD next_gen = 1;

static UQUAD window_phys;       /* physical base of the 2 MiB backing block */
static UQUAD used[WORDS];       /* bit per window page: in a live allocation */
static struct alloc *allocs;
static ULONG live_allocs;
static ULONG free_pages;
static ULONG bad_frees;

static BOOL page_used(ULONG i)
{
    return (used[i / 64] >> (i % 64)) & 1;
}

static void set_pages(ULONG first, ULONG n, BOOL value)
{
    while (n--) {
        if (value)
            used[first / 64] |= 1ULL << (first % 64);
        else
            used[first / 64] &= ~(1ULL << (first % 64));
        first++;
    }
}

void x86_64_low_tpa_init(void)
{
    /*
     * x86_64_map_kernel_pages() requires a 2 MiB-aligned backing_phys,
     * while the page allocator only guarantees 4 KiB: take twice the size,
     * align inside it, and hand the slack on either side back.  Unlike the
     * earlier version there is no "below 4 GiB" limit here -- this is the
     * physical side of the story, and the mapping makes any RAM address
     * reachable at the window's low virtual address.
     */
    UQUAD pages = (X86_64_LOW_TPA_BYTES + X86_64_PAGE_2M_SIZE) / X86_64_PAGE_SIZE;
    UQUAD raw = x86_64_pmem_alloc_pages(pages);
    UQUAD phys = (raw + X86_64_PAGE_2M_SIZE - 1) & ~(X86_64_PAGE_2M_SIZE - 1);
    UQUAD end = phys + X86_64_LOW_TPA_BYTES;

    if (phys > raw)
        x86_64_pmem_free_pages(raw, (phys - raw) / X86_64_PAGE_SIZE);
    if (raw + pages * X86_64_PAGE_SIZE > end)
        x86_64_pmem_free_pages(end, (raw + pages * X86_64_PAGE_SIZE - end) / X86_64_PAGE_SIZE);

    x86_64_map_kernel_pages(X86_64_LOW_TPA_VIRT_BASE, phys,
                            X86_64_LOW_TPA_BYTES / X86_64_PAGE_2M_SIZE);
    window_phys = phys;
    memset(used, 0, sizeof(used));
    free_pages = WINDOW_PAGES;
}

BOOL procmem_contains(const void *p)
{
    UQUAD a = (UQUAD)(uintptr_t)p;

    return a >= X86_64_LOW_TPA_VIRT_BASE &&
           a < X86_64_LOW_TPA_VIRT_BASE + X86_64_LOW_TPA_BYTES;
}

UQUAD procmem_phys_of(UQUAD va)
{
    return window_phys + (va - X86_64_LOW_TPA_VIRT_BASE);
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
    UQUAD va;

    if (next_gen == ~(UQUAD)0 || !bytes || !window_phys ||
        bytes > X86_64_LOW_TPA_BYTES)       /* also rules out overflow below */
        return NULL;
    pages = (bytes + X86_64_PAGE_SIZE - 1) / X86_64_PAGE_SIZE;
    if (pages > free_pages || !find_run(pages, &first))
        return NULL;

    a = kalloc(sizeof(*a));
    if (!a)
        return NULL;

    va = X86_64_LOW_TPA_VIRT_BASE + (UQUAD)first * X86_64_PAGE_SIZE;
    a->va = va;
    a->pages = pages;
    a->owner = NULL;
    a->pins = 0;
    a->holds = NULL;
    a->hold_count = 0;
    a->cleanup_owned = FALSE;
    a->reclaim = FALSE;
    a->notify = NULL;
    a->notify_token = NULL;
    a->gen = next_gen++;
    a->next = allocs;
    allocs = a;

    set_pages(first, pages, TRUE);
    free_pages -= pages;
    live_allocs++;

    /* Zero through the direct map: unlike the window's own low alias,
     * it is present whichever address space is currently loaded. */
    if (flags & PROCMEM_ZERO)
        memset((void *)(uintptr_t)(X86_64_PHYS_MAP_BASE + procmem_phys_of(va)),
               0, (size_t)pages * X86_64_PAGE_SIZE);
    return (void *)(uintptr_t)va;
}

static struct alloc **find_link(const void *p)
{
    struct alloc **link;

    for (link = &allocs; *link; link = &(*link)->next)
        if ((*link)->va == (UQUAD)(uintptr_t)p)
            return link;
    return NULL;
}

static void release(struct alloc **link)
{
    struct alloc *a = *link;
    void (*notify)(void *) = a->notify;
    void *token = a->notify_token;

    *link = a->next;
    set_pages((ULONG)((a->va - X86_64_LOW_TPA_VIRT_BASE) / X86_64_PAGE_SIZE),
              a->pages, FALSE);
    free_pages += a->pages;
    live_allocs--;
    kfree(a);
    if (notify) notify(token);
}

#define HOLD_PAGE X86_64_PAGE_SIZE
#define HOLD_WINDOW X86_64_LOW_TPA_VIRT_BASE
#define HOLD_PIN_PAGE X86_64_PAGE_SIZE
#include "../../procmem_hold.h"

/* A deferred decision belongs to this allocation record, not its former
 * owner's reusable basepage address. */
static void reclaim_unpinned(void)
{
    struct alloc **link = &allocs;

    while (*link) {
        if ((*link)->reclaim && !(*link)->pins && !(*link)->hold_count)
            release(link);
        else
            link = &(*link)->next;
    }
}

BOOL procmem_on_free(void *base, void (*notify)(void *), void *token)
{
    struct alloc **link = find_link(base);
    if (!link || !notify || (*link)->notify) return FALSE;
    (*link)->notify = notify;
    (*link)->notify_token = token;
    return TRUE;
}

BOOL procmem_free(void *p)
{
    struct alloc **link = find_link(p);

    if (!link) {                /* not a live allocation base */
        bad_frees++;
        return FALSE;
    }
    if ((*link)->cleanup_owned || (*link)->pins || (*link)->hold_count) /* mapped or explicitly retained */
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
        if ((*link)->gen >= gen && !(*link)->owner && !(*link)->pins && !(*link)->hold_count && !(*link)->reclaim) {
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

void procmem_pin(UQUAD va, UQUAD bytes, int delta)
{
    struct alloc *a;

    for (a = allocs; a; a = a->next)
        if (a->va < va + bytes && va < a->va + (UQUAD)a->pages * X86_64_PAGE_SIZE) {
            if (delta > 0)
                a->pins++;
            else if (a->pins)
                a->pins--;
        }
    if (delta < 0) reclaim_unpinned();
}

UQUAD procmem_gen(const void *p)
{
    struct alloc **link = find_link(p);

    return link ? (*link)->gen : 0;
}

ULONG procmem_size(const void *p)
{
    struct alloc **link = find_link(p);

    return link ? (*link)->pages * (ULONG)X86_64_PAGE_SIZE : 0;
}

BOOL procmem_range_live(UQUAD va, UQUAD bytes)
{
    UQUAD first, last, i;

    if (!bytes || va < X86_64_LOW_TPA_VIRT_BASE ||
        va + bytes < va ||
        va + bytes > X86_64_LOW_TPA_VIRT_BASE + X86_64_LOW_TPA_BYTES)
        return FALSE;
    first = (va - X86_64_LOW_TPA_VIRT_BASE) / X86_64_PAGE_SIZE;
    last = (va + bytes - 1 - X86_64_LOW_TPA_VIRT_BASE) / X86_64_PAGE_SIZE;
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

BOOL procmem_set_owner(const void *p, const void *owner)
{
    struct alloc **link = find_link(p);

    if (!link || (*link)->cleanup_owned || ((*link)->hold_count && ((*link)->owner != owner || (*link)->reclaim))) return FALSE;
    (*link)->owner = owner;
    (*link)->reclaim = FALSE; /* transfer or make permanent */
    return TRUE;
}

/* Callbacks can unpin or free allocation records, or leave through a disk
 * error longjmp. Resume by generation, never by a pointer held across one.
 * Generations are unique; allocations made by a callback are not visited. */
static struct alloc *owned_before(const void *owner, UQUAD before)
{
    struct alloc *a, *best = NULL;
    for (a = allocs; a; a = a->next)
        if (a->owner == owner && a->gen < before &&
            (!best || a->gen > best->gen))
            best = a;
    return best;
}

void procmem_free_owned(const void *owner, void (*pre_free)(void *base))
{
    struct alloc **link;
    struct alloc *a;
    UQUAD before = ~(UQUAD)0;

    if (!owner)                 /* NULL means "permanent", never "any" */
        return;

    /* First let the caller drop whatever is keyed by each block (a basepage's
     * KPROC record, and with it that process's address space and the pins
     * it holds), so the second pass sees which blocks are really free to go. */
    if (pre_free)
        while ((a = owned_before(owner, before)) != NULL) {
            before = a->gen;
            pre_free((void *)(uintptr_t)a->va);
        }

    /* Mapped blocks remain allocated until the final pin is dropped. */
    link = &allocs;
    while (*link) {
        if ((*link)->owner == owner) {
            if (!(*link)->pins && !(*link)->hold_count) {
                release(link);
                continue;
            }
            /* Forget the reusable process address. The allocation record
             * itself carries this decision until unpin or ownership transfer. */
            (*link)->owner = NULL;
            (*link)->reclaim = TRUE;
        }
        link = &(*link)->next;
    }
    reclaim_unpinned();
}

void procmem_keep(const void *owner, ULONG keep_bytes, void (*pre_keep)(void *base))
{
    struct alloc *a;
    struct alloc **link;
    UQUAD before = ~(UQUAD)0, generation;
    void *base;

    if (!owner)
        return;
    while ((a = owned_before(owner, before)) != NULL) {
        before = generation = a->gen;
        base = (void *)(uintptr_t)a->va;
        if (a->va == (UQUAD)(uintptr_t)owner) {
            /* the owner's own block: keep only the requested prefix, in
             * whole pages, and give the tail back -- unless a live address
             * space can still reach it */
            ULONG keep = (ULONG)((keep_bytes + X86_64_PAGE_SIZE - 1) / X86_64_PAGE_SIZE);

            if (keep_bytes && keep && keep < a->pages && !a->pins && !a->hold_count) {
                set_pages((ULONG)((a->va - X86_64_LOW_TPA_VIRT_BASE) / X86_64_PAGE_SIZE) + keep,
                          a->pages - keep, FALSE);
                free_pages += a->pages - keep;
                a->pages = keep;
            }
        } else if (pre_keep) {
            pre_keep(base);
            /* It may have freed, reused, or transferred this allocation. */
            link = find_link(base);
            if (!link || (*link)->gen != generation || (*link)->owner != owner)
                continue;
            a = *link;
        }
        a->owner = NULL;
        a->reclaim = FALSE;       /* Ptermres keeps these blocks permanent */
    }
    reclaim_unpinned();
}

void procmem_stats(PROCMEM_STATS *stats)
{
    stats->live_allocs = live_allocs;
    stats->total_pages = WINDOW_PAGES;
    stats->free_pages = free_pages;
    stats->bad_frees = bad_frees;
}
