/*
 * procmem.h - tracked process memory and address spaces (x86-64)
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef PROCMEM_H
#define PROCMEM_H

#include "portab.h"

#ifdef __x86_64__

/*
 * Process allocations
 * -------------------
 * Everything an x32 process owns in user memory -- its environment, its
 * basepage and TPA -- comes from one low virtual window that lies entirely
 * below 4 GiB, so any address in it fits the 32-bit GEMDOS ABI fields
 * (p_env, p_tbase, p_hitpa, ...).  That is a VIRTUAL constraint, met by
 * construction: x86_64_procmem_alloc() can only return window addresses.
 * The PHYSICAL pages backing the window are chosen separately and may lie
 * anywhere in RAM, above 4 GiB included, because the MMU maps them;
 * nothing here assumes a low physical address.
 *
 * Each allocation is a whole number of 4 KiB pages and is tracked: the
 * window records which pages are in use, and each live allocation has a
 * record (a kheap block) naming its base, size and owner.  An allocation
 * is released exactly once -- by x86_64_procmem_free() (Mfree), by
 * x86_64_procmem_free_owned() (process teardown), or by the caller's
 * failure path -- and a second release is refused and counted, never
 * applied.  These blocks are user memory: they are never kheap blocks, and
 * kfree() rejects them.
 *
 * x86_64_procmem_alloc() returns NULL, with no side effect, when the
 * window has no free run of pages or the tracking record cannot be
 * allocated.
 */
#define PROCMEM_ZERO    0x0001  /* fill with zeros before returning; without
                                 * it the pages hold whatever a previous
                                 * owner left behind */

void *x86_64_procmem_alloc(ULONG bytes, UWORD flags);
BOOL x86_64_procmem_free(void *p);

/* Size in bytes (a whole number of pages) of the live allocation whose
 * base is exactly p; 0 if p is not the base of one. */
ULONG x86_64_procmem_size(const void *p);

/* Identity of the live allocation whose base is exactly p: a number never
 * given to any other allocation, even one later placed at the same address.
 * 0 if p is not the base of one.  Lets a holder of an address tell "still my
 * block" from "someone else's block that reused the space". */
UQUAD x86_64_procmem_gen(const void *p);

/* TRUE iff p lies in the window at all (live or not). */
BOOL x86_64_procmem_contains(const void *p);

/* TRUE iff every page of [va, va + bytes) belongs to a live allocation. */
BOOL x86_64_procmem_range_live(UQUAD va, UQUAD bytes);

/*
 * A block mapped into a live address space (x86_64_aspace_map_procmem()) is
 * pinned: it cannot be released while the mapping exists, because the pages
 * would be handed to a new owner while the old process could still reach
 * them.  x86_64_procmem_free() returns FALSE for a pinned block and leaves
 * it allocated; x86_64_procmem_free_owned() skips it.  The address space
 * unpins when it is destroyed.
 */
BOOL x86_64_procmem_pinned(const void *p);
void x86_64_procmem_pin(UQUAD va, UQUAD bytes, int delta);   /* +1 / -1 */

/* Physical address backing window address va (which must be inside it). */
UQUAD x86_64_procmem_phys_of(UQUAD va);

/*
 * Ownership, mirroring the GEMDOS memory descriptor owner: set_owner()
 * names the process that dies with the block, NULL (the initial state)
 * means permanent.  free_owned() releases every block owned by `owner`,
 * calling pre_free(base) first for each (so the caller can drop anything
 * keyed by that address, such as a KPROC record).  keep() is Ptermres: the
 * owner's blocks become permanent, and the owner's own block -- the one
 * starting at the address `owner` -- is cut back to keep_bytes (rounded up
 * to whole pages, 0 meaning "all of it") when no live address space maps
 * it; every other owned block is passed to pre_keep(base) first, for the
 * same kind of per-address cleanup.  `owner` is only an identity, never
 * dereferenced.
 */
void x86_64_procmem_set_owner(const void *p, const void *owner);
void x86_64_procmem_free_owned(const void *owner, void (*pre_free)(void *base));
void x86_64_procmem_keep(const void *owner, ULONG keep_bytes, void (*pre_keep)(void *base));

typedef struct {
    ULONG live_allocs;      /* allocations not yet freed */
    ULONG total_pages;      /* size of the window */
    ULONG free_pages;       /* pages not in any allocation */
    ULONG bad_frees;        /* rejected releases (double/foreign free) */
} PROCMEM_STATS;

void x86_64_procmem_stats(PROCMEM_STATS *stats);

/*
 * Address spaces
 * --------------
 * One ring-3 process's page tables.  The object owns the PML4 and every
 * page-table page built under it, and nothing else: the leaf pages it
 * maps belong to procmem (or are device/test pages) and are never freed
 * with the address space.  Creation and mapping can fail for lack of
 * memory; destruction cannot, and frees each table page exactly once.
 *
 * The kernel half (PML4 slots 256-511) is shared; the low half starts empty.
 */
typedef struct x86_64_aspace X86_64_ASPACE;

/* Explicit leaf permissions.  Execution is denied unless PROT_EXEC is given
 * (NX), and the page is reachable from ring 3 only with PROT_USER. */
#define ASPACE_PROT_WRITE   0x1
#define ASPACE_PROT_EXEC    0x2
#define ASPACE_PROT_USER    0x4

/* The 32-bit ABI limit: no user mapping may reach or pass this address. */
#define X86_64_USER_VA_LIMIT 0x100000000ULL

X86_64_ASPACE *x86_64_aspace_create(void);
void x86_64_aspace_destroy(X86_64_ASPACE *as);
UQUAD x86_64_aspace_pml4(const X86_64_ASPACE *as);

/*
 * Maps one 4 KiB page: user virtual address va (page-aligned, and the
 * whole page below X86_64_USER_VA_LIMIT) to the physical page phys
 * (page-aligned; ANY physical address, including above 4 GiB).  FALSE if
 * a constraint is violated or table memory ran out; the page is then not
 * mapped.
 */
BOOL x86_64_aspace_map_page(X86_64_ASPACE *as, UQUAD va, UQUAD phys, UWORD prot);

/*
 * Maps [va, va + bytes) -- rounded out to whole pages -- of live procmem
 * allocations at the same addresses.  FALSE if any page is not live, a
 * constraint is violated or memory ran out; pages mapped before the
 * failure stay in the address space and go away with it.
 */
BOOL x86_64_aspace_map_procmem(X86_64_ASPACE *as, UQUAD va, UQUAD bytes, UWORD prot);

/* Number of table pages (PML4 included) the address space currently owns. */
ULONG x86_64_aspace_table_pages(const X86_64_ASPACE *as);

#endif /* __x86_64__ */

#endif /* PROCMEM_H */
