/*
 * kheap.c - growable kernel-private heap (32-bit MMU configurations)
 *
 * See include/kheap.h for the contract.  Small blocks come from per-size-
 * class slabs (one physical page each), larger ones from a dedicated run of
 * pages.  Slab pages whose last block is freed go straight back to the
 * physical page allocator (include/pmem32.h), so a burst of allocations
 * does not leave the memory pinned afterwards.  The pages are reached through
 * the physical-memory window of the kernel half.
 *
 * (The x86-64 kernel has its own, bios/machine/pc-x86_64/kheap.c: found
 * first through vpath.)
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"

#if CONF_WITH_KHEAP && !defined(__x86_64__)

#include "portab.h"
#include "string.h"
#include "kheap.h"
#include "pmem32.h"

#define PAGE            PMEM32_PAGE
#define PAGE_MASK       (~(ULONG)(PAGE - 1))

#define SLAB_MAGIC      0x4B48534CUL    /* "KHSL" */
#define LARGE_MAGIC     0x4B484C47UL    /* "KHLG" */
#define BLOCK_USED      0x4B485544UL    /* "KHUD" */
#define BLOCK_FREE      0x4B484652UL    /* "KHFR" */

/* Header at the start of every heap page; magic says which kind. */
struct slab {
    ULONG magic;                /* SLAB_MAGIC or LARGE_MAGIC */
    struct slab *next;          /* slab: next slab with a free block */
    struct block *free;         /* slab: free-block list */
    ULONG a;                    /* slab: class index; large: page count */
    ULONG used;                 /* slab: blocks in use */
};
#define SLAB_HDR_PAD    32                      /* keeps payloads 16-aligned */

struct block {
    ULONG magic;                /* BLOCK_USED / BLOCK_FREE */
    struct block *next;         /* free: next free block in the slab */
    ULONG pad[2];               /* the header is 16 bytes: payloads are 16-aligned */
};
#define BLOCK_HDR       sizeof(struct block)    /* 16 */

/* Block sizes including the 16-byte header: payloads of 48 .. 2032 bytes. */
static const UWORD class_size[] = { 64, 128, 256, 512, 1024, 2048 };
#define NUM_CLASSES     (sizeof(class_size) / sizeof(class_size[0]))
#define MAX_SMALL       (2048 - BLOCK_HDR)

/* sanity bound: 4 MiB and a page of header, a program image as one block */
#define MAX_LARGE_PAGES 1088

static struct slab *partial[NUM_CLASSES];
static KHEAP_STATS stats;

static BOOL in_window(const void *p)
{
    return pmem32_in_window(p) && pmem32_phys(p) < pmem32_highest_addr();
}

static struct slab *new_page(ULONG pages)
{
    PADDR phys = pmem32_alloc(pages, 1);

    if (phys == PMEM32_NONE)
        return NULL;
    stats.pages += pages;
    return (struct slab *)pmem32_virt(phys);
}

static void release_page(struct slab *s, ULONG pages)
{
    s->magic = 0;               /* a stale pointer must not look live */
    pmem32_free(pmem32_phys(s), pages);
    stats.pages -= pages;
}

static struct slab *new_slab(UWORD cls)
{
    struct slab *s = new_page(1);
    UWORD size = class_size[cls];
    UBYTE *base;
    ULONG i, n;

    if (!s)
        return NULL;
    memset(s, 0, PAGE);
    s->magic = SLAB_MAGIC;
    s->a = cls;
    n = (PAGE - SLAB_HDR_PAD) / size;
    base = (UBYTE *)s + SLAB_HDR_PAD;
    for (i = n; i > 0; i--) {
        struct block *b = (struct block *)(base + (i - 1) * size);

        b->magic = BLOCK_FREE;
        b->next = s->free;
        s->free = b;
    }
    return s;
}

void *kalloc(ULONG size)
{
    UWORD cls;
    struct slab *s;
    struct block *b;
    ULONG pages;

    if (!size)
        return NULL;

    if (size > MAX_SMALL) {
        if (size > (ULONG)MAX_LARGE_PAGES * PAGE - SLAB_HDR_PAD)
            return NULL;
        pages = (size + SLAB_HDR_PAD + PAGE - 1) / PAGE;
        s = new_page(pages);
        if (!s)
            return NULL;
        memset(s, 0, pages * PAGE);
        s->magic = LARGE_MAGIC;
        s->a = pages;
        stats.live_blocks++;
        return (UBYTE *)s + SLAB_HDR_PAD;
    }

    for (cls = 0; class_size[cls] < size + BLOCK_HDR; cls++)
        ;
    s = partial[cls];
    if (!s) {
        s = new_slab(cls);
        if (!s)
            return NULL;
        s->next = NULL;
        partial[cls] = s;
    }

    b = s->free;
    s->free = b->next;
    s->used++;
    if (!s->free)
        partial[cls] = s->next;     /* full: off the list until a free */
    b->magic = BLOCK_USED;
    b->next = NULL;
    memset(b + 1, 0, class_size[cls] - BLOCK_HDR);
    stats.live_blocks++;
    return b + 1;
}

void kfree(void *p)
{
    ULONG addr = (ULONG)(uintptr_t)p;
    struct slab *s;
    struct block *b;
    UWORD cls;

    if (!p)
        return;

    if (!in_window(p) || (addr & 15)) {
        stats.bad_frees++;
        return;
    }

    s = (struct slab *)(uintptr_t)(addr & PAGE_MASK);

    if (s->magic == LARGE_MAGIC && addr == (ULONG)(uintptr_t)s + SLAB_HDR_PAD) {
        release_page(s, s->a);
        stats.live_blocks--;
        return;
    }

    if (s->magic != SLAB_MAGIC || addr - (ULONG)(uintptr_t)s < SLAB_HDR_PAD + BLOCK_HDR) {
        stats.bad_frees++;
        return;
    }
    cls = (UWORD)s->a;
    b = (struct block *)p - 1;
    if (cls >= NUM_CLASSES || b->magic != BLOCK_USED ||
        (addr - BLOCK_HDR - (ULONG)(uintptr_t)s - SLAB_HDR_PAD) % class_size[cls]) {
        stats.bad_frees++;          /* double free, or not a block start */
        return;
    }

    b->magic = BLOCK_FREE;
    b->next = s->free;
    if (!s->free) {                 /* was full: back on the list */
        s->next = partial[cls];
        partial[cls] = s;
    }
    s->free = b;
    stats.live_blocks--;

    if (--s->used == 0) {           /* empty: hand the page back */
        struct slab **link;

        for (link = &partial[cls]; *link; link = &(*link)->next)
            if (*link == s) {
                *link = s->next;
                break;
            }
        release_page(s, 1);
    }
}

void kheap_stats(KHEAP_STATS *out)
{
    *out = stats;
}

#endif /* CONF_WITH_KHEAP */
