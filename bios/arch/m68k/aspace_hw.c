/*
 * aspace_hw.c - the page tables of a process on the 68040 and 68060
 *
 * The backend of bios/aspace.c (include/aspace32.h).  The MMU has a three
 * level tree with 4 KiB pages: a root table of 128 entries of 32 MiB (512
 * bytes), pointer tables of 128 entries of 256 KiB (512 bytes) and page tables
 * of 64 pages (256 bytes).  Both root pointers, URP and SRP, point to the
 * root of the running process (doc/mmu-address-space.txt): the root has the
 * kernel's half in its upper 64 entries, the same pointers in every process,
 * and the user half in its lower 64.  The tables are far smaller than a page,
 * so they are cut from pages as they are wanted: the first from the page of
 * the root itself (after its 512 bytes), the others from the pages the core
 * hands out through its allocator and records for freeing.
 *
 * The first 32 KiB of every space is the kernel's low memory (the vectors,
 * the system variables, the BIOS stack, the low ST-RAM section), supervisor
 * only, at the addresses it has on every m68k machine: the same page table in
 * every root.  There is no execute permission on the 68040: every readable
 * page can be executed.
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"

#if CONF_WITH_USER_ASPACE && defined(__m68k__)

#include "portab.h"
#include "string.h"
#include "procmem.h"
#include "pmem32.h"
#include "aspace32.h"
#if CONF_ATARI_HIGHER_HALF
#include "tosvars.h"
#include "atari_mmu.h"
#elif CONF_AMIGA_HIGHER_HALF
#include "amiga_mmu.h"
#endif

#define PAGE            PMEM32_PAGE
#define ROOT_ENTRIES    128
#define USER_ROOT       64              /* entries 0-63: below 0x80000000 */
#define PTR_ENTRIES     128
#define PT_ENTRIES      64
#define LOW_PAGES       8               /* the kernel's low memory: 32 KiB */

/* root and pointer table descriptors */
#define UDT_RESIDENT    0x3UL
#define UDT_MASK        0x3UL

/* page descriptor */
#define PDT_RESIDENT    0x1UL
#define PDT_MASK        0x3UL
#define PTE_W           0x004UL         /* write protected */
#if CONF_M68K_ROM_WINDOW
/*
 * The pages of a process are write-through on an Atari (and an Amiga, whose
 * custom chips DMA into the chip RAM behind the data cache), as the ST-RAM is in
 * bios/arch/m68k/68040_pmmu.S: the disk controllers DMA into the RAM behind
 * the data cache, and a page is mapped twice, in the process and in the window
 * of the kernel.  With copyback pages the TT does not run on Hatari's 68040:
 * after a disk read a word on the stack of the kernel (a return address) is
 * stale and a return jumps into data.  (#728 has what was tried.)
 */
#define PTE_CM_COPYBACK 0x000UL         /* cacheable, write-through */
#else
#define PTE_CM_COPYBACK 0x020UL         /* cacheable, copyback */
#endif
#define PTE_S           0x080UL         /* supervisor only */
#define PTE_G           0x400UL         /* the same in every address space */
#define PTE_OWNED       0x800UL         /* (a bit left to software) a private page */

/*
 * The root page: the root table, then, at 512, the state of the cutting of
 * tables, then the first tables.
 */
#define CARVE_OFFSET    512
#define FIRST_TABLE     1024
struct carve {
    ULONG page;                         /* the page tables are being cut from; 0 if none yet */
    ULONG offset;
};

static PADDR kernel_root;               /* the tree the MMU runs on when this starts: the kernel's half is its upper entries */
static PADDR low_page_table;            /* the page table of the kernel's low memory */

ULONG aspace_hw_root_pages(void)
{
    return 1;
}

ULONG aspace_hw_root_align(void)
{
    return 1;
}

static volatile ULONG *table(PADDR phys)
{
    return (volatile ULONG *)pmem32_virt(phys);
}

/* the 68040 instructions, spelled as words: the file is assembled for the -m68020 the image is built for */
static ULONG read_urp(void)
{
    ULONG v;

    __asm__ volatile (".long 0x4e7a0806\n\tmove.l %%d0,%0" : "=g" (v) : : "d0");   /* movec urp,d0 */
    return v;
}

static ULONG read_srp(void)
{
    ULONG v;

    __asm__ volatile (".long 0x4e7a0807\n\tmove.l %%d0,%0" : "=g" (v) : : "d0");   /* movec srp,d0 */
    return v;
}

static void write_roots(ULONG v)
{
    __asm__ volatile ("move.l %0,%%d0\n\t.long 0x4e7b0806\n\t.long 0x4e7b0807"   /* movec d0,urp/srp */
                      : : "g" (v) : "d0", "memory");
}

static void pflushan(void)
{
    __asm__ volatile (".word 0xf510" : : : "memory");                    /* pflushan */
}

static void pflusha(void)
{
    __asm__ volatile (".word 0xf518" : : : "memory");                    /* pflusha */
}

#if CONF_ATARI_HIGHER_HALF && CONF_WITH_TTRAM
/*
 * The tree in the ROM (atari_mmu040.S) maps the window over the first 16 MiB
 * only: the size of the TT-RAM is not known when it is built.  So the window
 * of the TT-RAM, at 0x80000000 + its physical address, is built here, in a
 * root in the RAM that the MMU runs on from then on (the ROM tree is the
 * template of the rest).  Its tables are permanent and come from the ST-RAM,
 * which the window reaches already.
 */
static PADDR tt_page;                    /* the page the tables are cut from */
static ULONG tt_offset;

static volatile ULONG *tt_table(ULONG size)    /* zeroed, aligned to its size */
{
    ULONG off = (tt_offset + size - 1) & ~(size - 1);

    if (!tt_page || off + size > PAGE) {
        tt_page = pmem32_alloc_low(1, 1);
        if (tt_page == PMEM32_NONE) {
            tt_page = 0;
            return NULL;
        }
        memset(pmem32_virt(tt_page), 0, PAGE);
        off = 0;
    }
    tt_offset = off + size;
    return table(tt_page + off);
}

static BOOL map_ttram(volatile ULONG *r)
{
    ULONG start = (ULONG)(uintptr_t)TTRAM_START;
    ULONG end = (ULONG)(uintptr_t)ramtop & ~(PAGE - 1);
    volatile ULONG *ptr;
    ULONG p;

    if (!ramtop)
        return TRUE;
    /* the pointer table of the ROM tree, which has no room for the TT-RAM, copied into the RAM */
    ptr = tt_table(512);
    if (!ptr)
        return FALSE;
    memcpy((void *)ptr, (const void *)table(r[USER_ROOT] & ~0x1ffUL), 512);
    r[USER_ROOT] = (ULONG)pmem32_phys((void *)ptr) | UDT_RESIDENT;
    if (end > 0x40000000UL)             /* the window is 1 GiB */
        end = 0x40000000UL;
    for (p = start; p < end; p += PAGE) {
        volatile ULONG *pt;
        ULONG ri = USER_ROOT + (p >> 25);
        ULONG pi = (p >> 18) & (PTR_ENTRIES - 1);

        if (!(r[ri] & UDT_MASK)) {
            ptr = tt_table(512);
            if (!ptr)
                return FALSE;
            r[ri] = (ULONG)pmem32_phys((void *)ptr) | UDT_RESIDENT;
        }
        ptr = table(r[ri] & ~0x1ffUL);
        if (!(ptr[pi] & UDT_MASK)) {
            pt = tt_table(256);
            if (!pt)
                return FALSE;
            ptr[pi] = (ULONG)pmem32_phys((void *)pt) | UDT_RESIDENT;
        } else {
            pt = table(ptr[pi] & ~0xffUL);
        }
        pt[(p >> 12) & (PT_ENTRIES - 1)] = p | PDT_RESIDENT | PTE_CM_COPYBACK | PTE_S | PTE_G;
    }
    return TRUE;
}
#endif

BOOL aspace_hw_init(void)
{
    volatile ULONG *pt;
    ULONG i;

    kernel_root = read_srp() & ~0x1ffUL;

#if CONF_ATARI_HIGHER_HALF && CONF_WITH_TTRAM
    if (ramtop) {
        /* a root of the RAM with the window of the TT-RAM; the ROM tree is not changed */
        PADDR root = pmem32_alloc_low(1, 1);
        volatile ULONG *r;

        if (root == PMEM32_NONE)
            return FALSE;
        memset(pmem32_virt(root), 0, PAGE);
        r = table(root);
        memcpy((void *)r, (const void *)table(kernel_root), ROOT_ENTRIES * 4);
        if (!map_ttram(r))
            return FALSE;
        kernel_root = root;
        write_roots((ULONG)root);       /* (nothing is cached in the RAM above the 16 MiB yet) */
        pflusha();
    }
#endif

    /* the page table of the kernel's low memory, supervisor only (on the Amiga,
     * where the pages above the chip RAM may be all there is, from any page: only
     * the custom chips want the chip RAM, and the CPU walks this table) */
#if CONF_AMIGA_HIGHER_HALF
    low_page_table = pmem32_alloc(1, 1);
#else
    low_page_table = pmem32_alloc_low(1, 1);
#endif
    if (low_page_table == PMEM32_NONE)
        return FALSE;
    memset(pmem32_virt(low_page_table), 0, PAGE);
    pt = table(low_page_table);
    for (i = 0; i < LOW_PAGES; i++)
        pt[i] = (i * PAGE) | PDT_RESIDENT | PTE_S | PTE_G;      /* write-through */
    return TRUE;
}

/* cuts a table of `size` bytes (a power of two, the alignment it needs too); PMEM32_NONE if no page could be had */
static PADDR carve(PADDR root, ULONG size, ASPACE_TABLE_ALLOC alloc, void *ctx)
{
    struct carve *c = (struct carve *)((UBYTE *)pmem32_virt(root) + CARVE_OFFSET);
    ULONG off;

    if (!c->page) {
        c->page = root;
        c->offset = FIRST_TABLE;
    }
    off = (c->offset + size - 1) & ~(size - 1);
    if (off + size > PAGE) {
        PADDR p = alloc ? alloc(ctx) : PMEM32_NONE;

        if (p == PMEM32_NONE)
            return PMEM32_NONE;
        c->page = p;
        off = 0;
    }
    c->offset = off + size;
    return c->page + off;
}

void aspace_hw_init_root(PADDR root)
{
    volatile ULONG *r = table(root);
    PADDR ptr, pt;
    ULONG i;

    /* the kernel's half: the same pointer tables everywhere */
    for (i = USER_ROOT; i < ROOT_ENTRIES; i++)
        r[i] = table(kernel_root)[i];

    /* the kernel's low memory: the first pointer table and the first page
     * table (the 64 pages from 0, of which the first eight are the kernel's
     * and supervisor-only) are cut from this page.  The page table is a copy
     * of the template: the descriptors of the kernel's pages are the same in
     * every root, but the rest of it is the process's own, for the pages it
     * maps from 0x8000 on (a shared one would put a private page into every
     * address space, and free it with the first). */
    ptr = carve(root, 512, NULL, NULL);
    pt = carve(root, 256, NULL, NULL);
    r[0] = (ULONG)ptr | UDT_RESIDENT;
    table(ptr)[0] = (ULONG)pt | UDT_RESIDENT;
    for (i = 0; i < PT_ENTRIES; i++)
        table(pt)[i] = table(low_page_table)[i];
}

/* the page descriptor for a page */
static ULONG make_desc(PADDR phys, UWORD prot, UWORD flags)
{
    ULONG d = (ULONG)phys | PDT_RESIDENT | PTE_CM_COPYBACK;

    if (!(prot & ASPACE_PROT_USER))
        d |= PTE_S;
    if (!(prot & ASPACE_PROT_WRITE))
        d |= PTE_W;
    if (flags & ASPACE_LEAF_OWNED)
        d |= PTE_OWNED;
    return d;
}

static UWORD desc_prot(ULONG d)
{
    UWORD prot = ASPACE_PROT_EXEC;      /* every readable page can be executed */

    if (!(d & PTE_S))
        prot |= ASPACE_PROT_USER;
    if (!(d & PTE_W))
        prot |= ASPACE_PROT_WRITE;
    return prot;
}

/* the descriptor of the page at va in the user half; NULL if a table is missing.  With
 * `create`, the tables are made (alloc is the source of their pages): NULL if one cannot be. */
static volatile ULONG *leaf(PADDR root, VADDR va, BOOL create, ASPACE_TABLE_ALLOC alloc, void *ctx)
{
    volatile ULONG *r = table(root);
    ULONG ri = va >> 25, pi = (va >> 18) & 0x7f, gi = (va >> 12) & 0x3f;
    volatile ULONG *ptr, *pt;
    PADDR t;

    if (ri >= USER_ROOT)
        return NULL;
    if ((r[ri] & UDT_MASK) < 2) {
        if (!create || (t = carve(root, 512, alloc, ctx)) == PMEM32_NONE)
            return NULL;
        r[ri] = (ULONG)t | UDT_RESIDENT;
    }
    ptr = table((PADDR)(r[ri] & ~0x1ffUL));
    if ((ptr[pi] & UDT_MASK) < 2) {
        if (!create || (t = carve(root, 256, alloc, ctx)) == PMEM32_NONE)
            return NULL;
        ptr[pi] = (ULONG)t | UDT_RESIDENT;
    }
    pt = table((PADDR)(ptr[pi] & ~0xffUL));
    return pt + gi;
}

BOOL aspace_hw_map(PADDR root, VADDR va, PADDR phys, UWORD prot, UWORD flags,
                   ASPACE_TABLE_ALLOC alloc, void *ctx)
{
    volatile ULONG *d = leaf(root, va, TRUE, alloc, ctx);

    if (!d)
        return FALSE;
    *d = make_desc(phys, prot, flags);
    return TRUE;
}

BOOL aspace_hw_lookup(PADDR root, VADDR va, PADDR *phys, UWORD *prot, UWORD *flags)
{
    volatile ULONG *d = leaf(root, va, FALSE, NULL, NULL);
    ULONG v;

    if (!d || (va >> 12) < LOW_PAGES)   /* (the low memory is the kernel's, not a leaf of the process) */
        return FALSE;
    v = *d;
    if (!(v & PDT_MASK))
        return FALSE;
    *phys = v & ~(PAGE - 1);
    *prot = desc_prot(v);
    *flags = (v & PTE_OWNED) ? ASPACE_LEAF_OWNED : 0;
    return TRUE;
}

BOOL aspace_hw_unmap(PADDR root, VADDR va)
{
    volatile ULONG *d = leaf(root, va, FALSE, NULL, NULL);
    BOOL was;

    if (!d || (va >> 12) < LOW_PAGES)
        return FALSE;
    was = (*d & PDT_MASK) != 0;
    *d = 0;
    return was;
}

BOOL aspace_hw_update(PADDR root, VADDR va, UWORD prot, UWORD flags)
{
    volatile ULONG *d = leaf(root, va, FALSE, NULL, NULL);

    if (!d || (va >> 12) < LOW_PAGES || !(*d & PDT_MASK))
        return FALSE;
    *d = make_desc(*d & ~(PAGE - 1), prot, flags);
    return TRUE;
}

void aspace_hw_walk(PADDR root,
                    void (*fn)(VADDR va, PADDR phys, UWORD flags, void *ctx), void *ctx)
{
    volatile ULONG *r = table(root);
    ULONG ri, pi, gi;

    for (ri = 0; ri < USER_ROOT; ri++) {
        volatile ULONG *ptr;

        if ((r[ri] & UDT_MASK) < 2)
            continue;
        ptr = table((PADDR)(r[ri] & ~0x1ffUL));
        for (pi = 0; pi < PTR_ENTRIES; pi++) {
            volatile ULONG *pt;

            if ((ptr[pi] & UDT_MASK) < 2)
                continue;
            pt = table((PADDR)(ptr[pi] & ~0xffUL));
            for (gi = 0; gi < PT_ENTRIES; gi++) {
                VADDR va = (ri << 25) | (pi << 18) | (gi << 12);

                if ((pt[gi] & PDT_MASK) && (va >> 12) >= LOW_PAGES)
                    fn(va, pt[gi] & ~(PAGE - 1), (pt[gi] & PTE_OWNED) ? ASPACE_LEAF_OWNED : 0, ctx);
            }
        }
    }
}

static BOOL in_pages(ULONG phys, const PADDR *pages, ULONG n)
{
    ULONG k;

    for (k = 0; k < n; k++)
        if ((phys & ~(PAGE - 1)) == pages[k])
            return TRUE;
    return FALSE;
}

void aspace_hw_drop_tables(PADDR root, const PADDR *pages, ULONG n)
{
    volatile ULONG *r = table(root);
    struct carve *c = (struct carve *)((UBYTE *)pmem32_virt(root) + CARVE_OFFSET);
    ULONG ri, pi;

    for (ri = 0; ri < USER_ROOT; ri++) {
        volatile ULONG *ptr;

        if ((r[ri] & UDT_MASK) < 2)
            continue;
        if (in_pages(r[ri], pages, n)) {
            r[ri] = 0;
            continue;
        }
        ptr = table((PADDR)(r[ri] & ~0x1ffUL));
        for (pi = 0; pi < PTR_ENTRIES; pi++)
            if ((ptr[pi] & UDT_MASK) >= 2 && in_pages(ptr[pi], pages, n))
                ptr[pi] = 0;
    }
    if (in_pages(c->page, pages, n)) {      /* the page tables are cut from goes: the next one needs a new page */
        c->page = root;
        c->offset = PAGE;
    }
}

#if CONF_M68K_ROM_WINDOW
/* the root the MMU ran on when the address spaces began: the static tree of the ROM, with the window of the TT-RAM */
ULONG aspace_hw_boot_root(void)
{
    return (ULONG)kernel_root;
}
#endif

PADDR aspace_hw_current(void)
{
    return read_urp() & ~0x1ffUL;
}

void aspace_hw_switch(PADDR root)
{
    write_roots((ULONG)root);
    pflushan();                         /* the non-global entries: the kernel's stay */
}

void aspace_hw_flush(void)
{
    pflushan();
}

void aspace_hw_code_changed(void)
{
    __asm__ volatile (".word 0xf4f8" : : : "memory");   /* cpusha bc: push and invalidate both caches */
    pflusha();
}

#endif /* CONF_WITH_USER_ASPACE */
