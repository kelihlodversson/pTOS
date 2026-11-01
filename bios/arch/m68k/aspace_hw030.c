/*
 * aspace_hw030.c - the page tables of a process on the 68030
 *
 * The backend of bios/aspace.c (include/aspace32.h) for the Atari and Amiga
 * ROMs that run in the upper half (CONF_ATARI_HIGHER_HALF, atari_mmu.S;
 * CONF_AMIGA_HIGHER_HALF, amiga_mmu.S), the counterpart of aspace_hw.c of the
 * 68040 and 68060.  The tree has the
 * same shape: 4 KiB pages and TC with PS = 12, IS = 0, TIA = 7, TIB = 7,
 * TIC = 6, which makes a root table of 128 entries of 32 MiB, pointer tables
 * of 128 entries of 256 KiB and page tables of 64 pages.  The descriptors are
 * long ones (8 bytes), because only those have the S bit that makes a page
 * supervisor-only and a write-protect bit: the tables are twice the size of
 * the 68040's (1 KiB, 1 KiB and 512 bytes).
 *
 * Only CRP is used (TC.SRE is clear), so the supervisor runs on the tree of the
 * process as well and reaches the process's buffers by their user addresses.
 * The root has the kernel's half in its upper 64 entries, copied from the
 * static tree (the same, ROM-resident, supervisor-only descriptors in every
 * process), and the user half in its lower 64.  Every descriptor has the used
 * (and for a page the modified) bit set when it is made, so that the MMU never
 * has to write one back: it cannot in the ROM, and in the RAM a write behind the
 * back of the data cache would leave the cached copy of the descriptor stale.
 *
 * The first 32 KiB of every space is the kernel's low memory (the vectors, the
 * system variables, the BIOS stack, the low ST-RAM section), supervisor only,
 * at the addresses it has on every Atari.  The tables are far smaller than a
 * page and are cut from pages as they are wanted, as the 68040 does it.
 *
 * The 68030 has no global bit: a switch loads CRP, which flushes the ATC, and
 * the kernel's entries are loaded again as they are used.  Its caches are
 * addressed logically, so the same memory is cached under every address it
 * is reached by (the window for the kernel, the user address for the
 * process): a switch clears both, and so does everything that changes what a
 * user address means (aspace_hw_flush()).  The kernel writes the memory of a
 * process through the window as well, so the way back to user mode clears the
 * data cache (uspaceasm.S).  The data cache is write-through, so nothing is
 * lost.
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"

#if CONF_WITH_USER_ASPACE && CONF_M68K_ROM_WINDOW && !CONF_M68K_ROM_WINDOW_040

#include "portab.h"
#include "string.h"
#include "procmem.h"
#include "pmem32.h"
#include "aspace32.h"

#define PAGE            PMEM32_PAGE
#define ROOT_ENTRIES    128
#define USER_ROOT       64              /* entries 0-63: below 0x80000000 */
#define PTR_ENTRIES     128
#define PT_ENTRIES      64
#define LOW_PAGES       8               /* the kernel's low memory: 32 KiB */

/* the first long of a long-format descriptor: no limit (0x7fff), the bits
 * that FreeMiNT sets in a working 68030 tree, and the used bit */
#define D_BASE          0x7fffFC00UL
#define DT_MASK         0x3UL
#define DT_PAGE         0x1UL
#define DT_TABLE        0x3UL
#define D_WP            0x04UL
#define D_USED          0x08UL
#define D_MOD           0x10UL
#define D_SUPER         0x100UL
#define A_OWNED         0x1UL           /* (the page address has 12 bits the MMU ignores) a private page */

/* a descriptor is two longs; table(phys) is indexed by longs */
#define W0(i)           ((i) * 2)
#define W1(i)           ((i) * 2 + 1)

/*
 * The root page: the root table (1 KiB), then, at 1024, the state of the
 * cutting of tables, then the first tables.
 */
#define CARVE_OFFSET    1024
#define FIRST_TABLE     2048
#define ROOT_BYTES      (ROOT_ENTRIES * 8)
#define PTR_BYTES       (PTR_ENTRIES * 8)
#define PT_BYTES        (PT_ENTRIES * 8)
struct carve {
    ULONG page;                         /* the page tables are being cut from; 0 if none yet */
    ULONG offset;
};

static PADDR kernel_root;               /* the static tree: the kernel's half is its upper entries */
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

/* PMOVE, spelled as words: the file is assembled for the -m68020 the image is built for */
static void write_crp(PADDR root)
{
    static ULONG crp[2];                /* (the kernel's: only read by the instruction) */

    crp[0] = 0x7fff0003UL;              /* no limit, 8-byte descriptors */
    crp[1] = (ULONG)root;
    __asm__ volatile ("move.l %0,%%a0\n\t.long 0xf0104c00"          /* pmove (a0),crp */
                      : : "g" (crp) : "a0", "memory");
}

static PADDR read_crp(void)
{
    static ULONG crp[2];

    __asm__ volatile ("move.l %0,%%a0\n\t.long 0xf0104e00"          /* pmove crp,(a0) */
                      : : "g" (crp) : "a0", "memory");
    return crp[1] & ~0xfUL;
}

static void pflusha(void)
{
    __asm__ volatile (".long 0xf0002400" : : : "memory");           /* pflusha */
}

/* clears the instruction and the data cache (CACR: CI and CD) */
static void clear_caches(void)
{
    __asm__ volatile (".long 0x4e7a0002\n\t"                        /* movec cacr,d0 */
                      "ori.l #0x808,%%d0\n\t"
                      ".long 0x4e7b0002"                            /* movec d0,cacr */
                      : : : "d0", "cc", "memory");
}

BOOL aspace_hw_init(void)
{
    volatile ULONG *pt;
    ULONG i;

    kernel_root = read_crp();

    /* the page table of the kernel's low memory, supervisor only */
#if CONF_AMIGA_HIGHER_HALF
    /* from the chip RAM that is left if there is some, so that a board of 2 MiB
     * stays whole for the process window that is allocated next */
    low_page_table = pmem32_alloc_low(1, 1);
    if (low_page_table == PMEM32_NONE)
        low_page_table = pmem32_alloc(1, 1);
#else
    low_page_table = pmem32_alloc(1, 1);
#endif
    if (low_page_table == PMEM32_NONE)
        return FALSE;
    memset(pmem32_virt(low_page_table), 0, PAGE);
    pt = table(low_page_table);
    for (i = 0; i < LOW_PAGES; i++) {
        pt[W0(i)] = D_BASE | DT_PAGE | D_USED | D_MOD | D_SUPER;
        pt[W1(i)] = i * PAGE;
    }
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

static void set_table(volatile ULONG *t, ULONG i, PADDR phys)
{
    t[W1(i)] = (ULONG)phys;
    t[W0(i)] = D_BASE | DT_TABLE | D_USED;
}

void aspace_hw_init_root(PADDR root)
{
    volatile ULONG *r = table(root);
    PADDR ptr, pt;
    ULONG i;

    /* the kernel's half: the same descriptors everywhere */
    for (i = USER_ROOT; i < ROOT_ENTRIES; i++) {
        r[W0(i)] = table(kernel_root)[W0(i)];
        r[W1(i)] = table(kernel_root)[W1(i)];
    }

    /* the kernel's low memory: the first pointer table and the first page
     * table (the 64 pages from 0, of which the first eight are the kernel's
     * and supervisor-only) are cut from this page.  The page table is a copy
     * of the template: the descriptors of the kernel's pages are the same in
     * every root, but the rest of it is the process's own. */
    ptr = carve(root, PTR_BYTES, NULL, NULL);
    pt = carve(root, PT_BYTES, NULL, NULL);
    set_table(r, 0, ptr);
    set_table(table(ptr), 0, pt);
    for (i = 0; i < PT_ENTRIES * 2; i++)
        table(pt)[i] = table(low_page_table)[i];
}

/* the page descriptor for a page */
static void make_desc(volatile ULONG *d, PADDR phys, UWORD prot, UWORD flags)
{
    ULONG w0 = D_BASE | DT_PAGE | D_USED | D_MOD;

    if (!(prot & ASPACE_PROT_USER))
        w0 |= D_SUPER;
    if (!(prot & ASPACE_PROT_WRITE))
        w0 |= D_WP;
    d[1] = (ULONG)phys | ((flags & ASPACE_LEAF_OWNED) ? A_OWNED : 0);
    d[0] = w0;
}

static UWORD desc_prot(ULONG w0)
{
    UWORD prot = ASPACE_PROT_EXEC;      /* every readable page can be executed */

    if (!(w0 & D_SUPER))
        prot |= ASPACE_PROT_USER;
    if (!(w0 & D_WP))
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
    if ((r[W0(ri)] & DT_MASK) < 2) {
        if (!create || (t = carve(root, PTR_BYTES, alloc, ctx)) == PMEM32_NONE)
            return NULL;
        set_table(r, ri, t);
    }
    ptr = table((PADDR)(r[W1(ri)] & ~0xfUL));
    if ((ptr[W0(pi)] & DT_MASK) < 2) {
        if (!create || (t = carve(root, PT_BYTES, alloc, ctx)) == PMEM32_NONE)
            return NULL;
        set_table(ptr, pi, t);
    }
    pt = table((PADDR)(ptr[W1(pi)] & ~0xfUL));
    return pt + W0(gi);
}

BOOL aspace_hw_map(PADDR root, VADDR va, PADDR phys, UWORD prot, UWORD flags,
                   ASPACE_TABLE_ALLOC alloc, void *ctx)
{
    volatile ULONG *d = leaf(root, va, TRUE, alloc, ctx);

    if (!d)
        return FALSE;
    make_desc(d, phys, prot, flags);
    return TRUE;
}

BOOL aspace_hw_lookup(PADDR root, VADDR va, PADDR *phys, UWORD *prot, UWORD *flags)
{
    volatile ULONG *d = leaf(root, va, FALSE, NULL, NULL);
    ULONG w0;

    if (!d || (va >> 12) < LOW_PAGES)   /* (the low memory is the kernel's, not a leaf of the process) */
        return FALSE;
    w0 = d[0];
    if ((w0 & DT_MASK) != DT_PAGE)
        return FALSE;
    *phys = d[1] & ~(PAGE - 1);
    *prot = desc_prot(w0);
    *flags = (d[1] & A_OWNED) ? ASPACE_LEAF_OWNED : 0;
    return TRUE;
}

BOOL aspace_hw_unmap(PADDR root, VADDR va)
{
    volatile ULONG *d = leaf(root, va, FALSE, NULL, NULL);
    BOOL was;

    if (!d || (va >> 12) < LOW_PAGES)
        return FALSE;
    was = (d[0] & DT_MASK) != 0;
    d[0] = 0;
    d[1] = 0;
    return was;
}

BOOL aspace_hw_update(PADDR root, VADDR va, UWORD prot, UWORD flags)
{
    volatile ULONG *d = leaf(root, va, FALSE, NULL, NULL);

    if (!d || (va >> 12) < LOW_PAGES || (d[0] & DT_MASK) != DT_PAGE)
        return FALSE;
    make_desc(d, d[1] & ~(PAGE - 1), prot, flags);
    return TRUE;
}

void aspace_hw_walk(PADDR root,
                    void (*fn)(VADDR va, PADDR phys, UWORD flags, void *ctx), void *ctx)
{
    volatile ULONG *r = table(root);
    ULONG ri, pi, gi;

    for (ri = 0; ri < USER_ROOT; ri++) {
        volatile ULONG *ptr;

        if ((r[W0(ri)] & DT_MASK) < 2)
            continue;
        ptr = table((PADDR)(r[W1(ri)] & ~0xfUL));
        for (pi = 0; pi < PTR_ENTRIES; pi++) {
            volatile ULONG *pt;

            if ((ptr[W0(pi)] & DT_MASK) < 2)
                continue;
            pt = table((PADDR)(ptr[W1(pi)] & ~0xfUL));
            for (gi = 0; gi < PT_ENTRIES; gi++) {
                VADDR va = (ri << 25) | (pi << 18) | (gi << 12);

                if ((pt[W0(gi)] & DT_MASK) == DT_PAGE && (va >> 12) >= LOW_PAGES)
                    fn(va, pt[W1(gi)] & ~(PAGE - 1), (pt[W1(gi)] & A_OWNED) ? ASPACE_LEAF_OWNED : 0, ctx);
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

        if ((r[W0(ri)] & DT_MASK) < 2)
            continue;
        if (in_pages(r[W1(ri)], pages, n)) {
            r[W0(ri)] = 0;
            r[W1(ri)] = 0;
            continue;
        }
        ptr = table((PADDR)(r[W1(ri)] & ~0xfUL));
        for (pi = 0; pi < PTR_ENTRIES; pi++)
            if ((ptr[W0(pi)] & DT_MASK) >= 2 && in_pages(ptr[W1(pi)], pages, n)) {
                ptr[W0(pi)] = 0;
                ptr[W1(pi)] = 0;
            }
    }
    if (in_pages(c->page, pages, n)) {      /* the page tables are cut from goes: the next one needs a new page */
        c->page = root;
        c->offset = PAGE;
    }
}

PADDR aspace_hw_current(void)
{
    return read_crp();
}

void aspace_hw_switch(PADDR root)
{
    write_crp(root);                    /* (the load flushes the ATC) */
    pflusha();
    clear_caches();                     /* (logically addressed: the other space's lines) */
}

void aspace_hw_flush(void)
{
    pflusha();
    clear_caches();
}

void aspace_hw_code_changed(void)
{
    pflusha();
    clear_caches();
}

#endif /* CONF_WITH_USER_ASPACE && CONF_M68K_ROM_WINDOW && !CONF_M68K_ROM_WINDOW_040 */
