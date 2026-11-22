/*
 * procmem.c - tracked process memory window
 *
 * See include/procmem.h for the allocation and ownership contract. The
 * kernel maps the low virtual window to architecture-owned physical backing.
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"

#if CONF_WITH_PROCMEM

#include "portab.h"
#include "string.h"
#include "kheap.h"
#include "procmem.h"
#if MACHINE_PC_X86_64
#include "pc_x86_64_memory.h"
#include "pgtable.h"
#include "pmem.h"
#else
#include "pmem32.h"
#include "aspace32.h"
#endif

#define WINDOW_PAGES    (USER_PROCMEM_SIZE / USER_PAGE_SIZE)
#define WORDS           ((WINDOW_PAGES + 31) / 32)

/* The whole window must be representable in a 32-bit ABI field. */
typedef char window_is_below_limit[
    (USER_PROCMEM_BASE + USER_PROCMEM_SIZE <= USER_VA_LIMIT) ? 1 : -1];

/* The default program image region does not overlap the process window and
 * the stack ends at the top of the user half. */
typedef char image_outside_window[
    (USER_IMAGE_BASE + USER_IMAGE_SIZE <= USER_PROCMEM_BASE ||
     USER_IMAGE_BASE >= USER_PROCMEM_BASE + USER_PROCMEM_SIZE) ? 1 : -1];
typedef char stack_below_limit[
    (USER_STACK_TOP <= USER_VA_LIMIT &&
     USER_STACK_SIZE <= USER_STACK_TOP &&   /* before subtracting: no wrap */
     USER_IMAGE_BASE + USER_IMAGE_SIZE <= USER_STACK_TOP - USER_STACK_SIZE) ? 1 : -1];

#if MACHINE_PC_X86_64
typedef char window_matches_pc_layout[
    (X86_64_LOW_TPA_VIRT_BASE == USER_PROCMEM_BASE &&
     X86_64_LOW_TPA_BYTES == USER_PROCMEM_SIZE &&
     X86_64_PAGE_SIZE == USER_PAGE_SIZE) ? 1 : -1];
#else
typedef char window_matches_pmem32_page[(PMEM32_PAGE == USER_PAGE_SIZE) ? 1 : -1];
#endif

struct allocation_hold;
struct alloc {
    struct alloc *next;
    VADDR va;
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

static UQUAD gens;              /* generations handed out (no initialised data: the Atari image is a ROM) */

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

#if MACHINE_PC_X86_64
void x86_64_low_tpa_init(void)
{
    /*
     * x86_64_map_kernel_pages() requires a 2 MiB-aligned backing_phys,
     * while the page allocator only guarantees 4 KiB: take twice the size,
     * align inside it, and hand the slack on either side back.  Unlike the
     * earlier version there is no "below 4 GiB" limit here -- this is the
     * physical side of the story, and the mapping makes any RAM address
     * reachable at the window's low virtual address. This boot-time allocator
     * panics on exhaustion, so raw is always a valid physical base here.
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
#else
/* Takes the backing pages; aspace.c maps them into the kernel's address space. */
BOOL procmem_window_init(PADDR *phys)
{
    PADDR p = pmem32_alloc(WINDOW_PAGES, 1);

    if (p == PMEM32_NONE)
        return FALSE;
    window_phys = p;
    memset(pmem32_virt(p), 0, USER_PROCMEM_SIZE);
    memset(used, 0, sizeof(used));
    free_pages = WINDOW_PAGES;
    *phys = p;
    return TRUE;
}

#endif

BOOL procmem_contains(const void *p)
{
    VADDR a = (VADDR)(uintptr_t)p;

    return a >= USER_PROCMEM_BASE &&
           a < USER_PROCMEM_BASE + USER_PROCMEM_SIZE;
}

PADDR procmem_phys_of(VADDR va)
{
    return window_phys + (va - USER_PROCMEM_BASE);
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
    VADDR va;

    if (gens >= ~(UQUAD)0 - 1 || !bytes || !window_phys ||
        bytes > USER_PROCMEM_SIZE)       /* also rules out overflow below */
        return NULL;
    pages = (bytes + USER_PAGE_SIZE - 1) / USER_PAGE_SIZE;
    if (pages > free_pages || !find_run(pages, &first))
        return NULL;

    a = kalloc(sizeof(*a));
    if (!a)
        return NULL;

    va = USER_PROCMEM_BASE + first * USER_PAGE_SIZE;
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
    a->gen = ++gens;
    a->next = allocs;
    allocs = a;

    set_pages(first, pages, TRUE);
    free_pages -= pages;
    live_allocs++;

    /* The physical alias is available under every address space. */
    if (flags & PROCMEM_ZERO) {
        PADDR physical = procmem_phys_of(va);
#if MACHINE_PC_X86_64
        void *alias = (void *)(uintptr_t)(X86_64_PHYS_MAP_BASE + physical);
#else
        void *alias = pmem32_virt(physical);
#endif
        memset(alias, 0, pages * USER_PAGE_SIZE);
    }
    return (void *)(uintptr_t)va;
}

#if !MACHINE_PC_X86_64
ULONG procmem_largest_free(void)
{
    ULONG i, run = 0, best = 0;

    for (i = 0; i < WINDOW_PAGES; i++) {
        if (page_used(i)) {
            run = 0;
            continue;
        }
        if (++run > best)
            best = run;
    }
    return best * (ULONG)USER_PAGE_SIZE;
}

#endif

static struct alloc **find_link(const void *p)
{
    struct alloc **link;

    for (link = &allocs; *link; link = &(*link)->next)
        if ((*link)->va == (VADDR)(uintptr_t)p)
            return link;
    return NULL;
}

static void release(struct alloc **link)
{
    struct alloc *a = *link;
    void (*notify)(void *) = a->notify;
    void *token = a->notify_token;

    *link = a->next;
    set_pages((ULONG)((a->va - USER_PROCMEM_BASE) / USER_PAGE_SIZE),
              a->pages, FALSE);
    free_pages += a->pages;
    live_allocs--;
    kfree(a);
    if (notify) notify(token);
}

#define HOLD_PAGE USER_PAGE_SIZE
#define HOLD_WINDOW USER_PROCMEM_BASE
#define HOLD_PIN_PAGE USER_PAGE_SIZE
#include "procmem_hold.h"

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
    return gens + 1;
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

void procmem_pin(VADDR va, VADDR bytes, int delta)
{
    struct alloc *a;

    for (a = allocs; a; a = a->next)
        if (a->va < va + bytes && va < a->va + (VADDR)a->pages * USER_PAGE_SIZE) {
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

    return link ? (*link)->pages * (ULONG)USER_PAGE_SIZE : 0;
}

BOOL procmem_range_live(VADDR va, VADDR bytes)
{
    ULONG first, last, i;

    if (!bytes || va < USER_PROCMEM_BASE ||
        va + bytes < va ||
        va + bytes > USER_PROCMEM_BASE + USER_PROCMEM_SIZE)
        return FALSE;
    first = (va - USER_PROCMEM_BASE) / USER_PAGE_SIZE;
    last = (va + bytes - 1 - USER_PROCMEM_BASE) / USER_PAGE_SIZE;
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
        if (a->va == (VADDR)(uintptr_t)owner) {
            /* the owner's own block: keep only the requested prefix, in
             * whole pages, and give the tail back -- unless a live address
             * space can still reach it */
            ULONG keep = (ULONG)((keep_bytes + USER_PAGE_SIZE - 1) / USER_PAGE_SIZE);

            if (keep_bytes && keep && keep < a->pages && !a->pins && !a->hold_count) {
                set_pages((ULONG)((a->va - USER_PROCMEM_BASE) / USER_PAGE_SIZE) + keep,
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

#if CONF_WITH_SHARED_VM_PEXEC && CONF_WITH_PRIVATE_CLONE_TEST
#include "shared_pexec_test.h"
void procmem_shared_snapshot(SHARED_PEXEC_SNAPSHOT *out)
{
    const struct alloc *a;
    out->count[SP_RAW_ALLOCS]=live_allocs;
    out->count[SP_RAW_FREE_PAGES]=free_pages;
    out->count[SP_RAW_BAD_FREES]=bad_frees;
    for(a=allocs;a;a=a->next) {
        out->count[SP_RAW_HOLDS]+=a->hold_count;
        out->count[SP_RAW_PINS]+=a->pins;
        if(a->reclaim) out->count[SP_RAW_RETIRED]++;
    }
}
#endif

#endif /* CONF_WITH_PROCMEM */
