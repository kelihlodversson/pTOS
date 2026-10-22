/*
 * aspace_hw.c - the page tables of a process on ARM (ARMv6K/ARMv7)
 *
 * The backend of bios/aspace.c (include/aspace32.h).  TTBCR.N = 1 makes
 * TTBR0 translate the lower 2 GiB, the user half, and TTBR1 the kernel half
 * for the whole uptime (doc/mmu-address-space.txt): this file builds the
 * tables of TTBR0.  The format is the short-descriptor one of the extended
 * page tables (XP set on the ARM1176): a first-level table of 2048 entries of
 * 1 MiB (8 KiB, aligned to 8 KiB), each pointing to a second-level table of
 * 256 small pages.  The ARM small page has no bit for software, and the
 * address space has to know which pages are its own: a second-level page
 * (4 KiB) therefore holds two hardware tables (for two neighbouring
 * megabytes) and, in its other half, a software word per page, as Linux does.
 *
 * ASIDs: a switch happens at Pexec(), Pterm() and the return to a launcher,
 * so every process runs with ASID 0 and a switch invalidates by ASID (the
 * global entries of the kernel half stay); see the document.
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"

#if CONF_WITH_USER_ASPACE && defined(__arm__)

#include "portab.h"
#include "string.h"
#include "procmem.h"
#include "pmem32.h"
#include "aspace32.h"
#include "raspi_mmu.h"
#include "processor_arm.h"

#define PAGE            PMEM32_PAGE
#define L1_ENTRIES      2048            /* the user half: 2 GiB of 1 MiB */
#define L1_BYTES        (L1_ENTRIES * 4)

/* first-level coarse page table descriptor, domain 0 */
#define L1_COARSE       0x1UL
#define L1_TYPE_MASK    0x3UL

/* second-level small page descriptor */
#define L2_SMALL        0x2UL           /* bits 1:0 = 1x */
#define L2_XN           0x1UL
#define L2_B            (1UL << 2)
#define L2_C            (1UL << 3)
#define L2_AP_SHIFT     4
#define L2_APX          (1UL << 9)
#define L2_S            (1UL << 10)
#define L2_NG           (1UL << 11)

/* what pages of RAM are: normal memory, write-back, shareable (as the
 * sections of the kernel half, virt_mmu.c) and global only in the kernel half */
#define L2_RAM          (L2_B | L2_C | L2_S | L2_NG)

#ifdef TARGET_RPI1
#define TTBR_MODE       ARM_TTBR_INNER_CACHEABLE
#else
#define TTBR_MODE       (ARM_TTBR_INNER_WRITE_BACK | ARM_TTBR_OUTER_WRITE_BACK)
#endif

ULONG aspace_hw_root_pages(void)
{
    return L1_BYTES / PAGE;
}

ULONG aspace_hw_root_align(void)
{
    return L1_BYTES / PAGE;             /* TTBR0 with TTBCR.N = 1: 8 KiB aligned */
}

static volatile ULONG *l1(PADDR root)
{
    return (volatile ULONG *)pmem32_virt(root);
}

static void barrier(void)
{
    __asm__ volatile ("dsb" : : : "memory");
}

/* the second-level table of the megabyte of va, and its software words; NULL
 * if the first-level entry is not a table */
static BOOL tables(PADDR root, VADDR va, volatile ULONG **hw, volatile ULONG **soft)
{
    ULONG e = l1(root)[va >> 20];
    ULONG idx = (va >> 12) & 0xff;
    ULONG half = (e >> 10) & 0x3;       /* which of the page's two tables: 0 or 1 */
    UBYTE *page;

    if ((e & L1_TYPE_MASK) != L1_COARSE)
        return FALSE;
    page = pmem32_virt(e & ~(PAGE - 1));
    *hw = (volatile ULONG *)(page + half * 1024) + idx;
    *soft = (volatile ULONG *)(page + 2048) + half * 256 + idx;
    return TRUE;
}

/* the descriptor for a page */
static ULONG make_desc(PADDR phys, UWORD prot)
{
    ULONG d = (ULONG)phys | L2_SMALL | L2_RAM;
    ULONG ap = (prot & ASPACE_PROT_USER) ? 3 : 1;

    d |= ap << L2_AP_SHIFT;
    if (!(prot & ASPACE_PROT_WRITE))
        d |= L2_APX;
    if (!(prot & ASPACE_PROT_EXEC))
        d |= L2_XN;
    return d;
}

static UWORD desc_prot(ULONG d)
{
    ULONG ap = (d >> L2_AP_SHIFT) & 3;
    UWORD prot = 0;

    if (ap & 2)
        prot |= ASPACE_PROT_USER;
    if (!(d & L2_APX) && (ap == 1 || ap == 3))
        prot |= ASPACE_PROT_WRITE;
    if (!(d & L2_XN))
        prot |= ASPACE_PROT_EXEC;
    return prot;
}

BOOL aspace_hw_map(PADDR root, VADDR va, PADDR phys, UWORD prot, UWORD flags,
                   ASPACE_TABLE_ALLOC alloc, void *ctx)
{
    volatile ULONG *hw, *soft;

    if (!tables(root, va, &hw, &soft)) {
        PADDR t = alloc(ctx);
        ULONG j = (va >> 20) & ~1UL;

        if (t == PMEM32_NONE)
            return FALSE;
        l1(root)[j] = (ULONG)t | L1_COARSE;
        l1(root)[j + 1] = ((ULONG)t + 1024) | L1_COARSE;
        tables(root, va, &hw, &soft);
    }
    *soft = flags | 0x80000000UL;       /* (a leaf is there) */
    *hw = make_desc(phys, prot);
    barrier();
    return TRUE;
}

BOOL aspace_hw_lookup(PADDR root, VADDR va, PADDR *phys, UWORD *prot, UWORD *flags)
{
    volatile ULONG *hw, *soft;
    ULONG d;

    if (!tables(root, va, &hw, &soft))
        return FALSE;
    d = *hw;
    if (!(d & L2_SMALL))
        return FALSE;
    *phys = d & ~(PAGE - 1);
    *prot = desc_prot(d);
    *flags = (UWORD)(*soft & 0xffff);
    return TRUE;
}

BOOL aspace_hw_unmap(PADDR root, VADDR va)
{
    volatile ULONG *hw, *soft;
    BOOL was;

    if (!tables(root, va, &hw, &soft))
        return FALSE;
    was = (*hw & L2_SMALL) != 0;
    *hw = 0;
    *soft = 0;
    barrier();
    return was;
}

BOOL aspace_hw_update(PADDR root, VADDR va, UWORD prot, UWORD flags)
{
    volatile ULONG *hw, *soft;

    if (!tables(root, va, &hw, &soft) || !(*hw & L2_SMALL))
        return FALSE;
    *hw = make_desc(*hw & ~(PAGE - 1), prot);
    *soft = flags | 0x80000000UL;
    barrier();
    return TRUE;
}

void aspace_hw_walk(PADDR root,
                    void (*fn)(VADDR va, PADDR phys, UWORD flags, void *ctx), void *ctx)
{
    ULONG i, k;

    for (i = 0; i < L1_ENTRIES; i++) {
        if ((l1(root)[i] & L1_TYPE_MASK) != L1_COARSE)
            continue;
        for (k = 0; k < 256; k++) {
            volatile ULONG *hw, *soft;
            VADDR va = ((VADDR)i << 20) | (k << 12);

            if (tables(root, va, &hw, &soft) && (*hw & L2_SMALL))
                fn(va, *hw & ~(PAGE - 1), (UWORD)(*soft & 0xffff), ctx);
        }
    }
}

void aspace_hw_drop_tables(PADDR root, const PADDR *pages, ULONG n)
{
    ULONG i, k;

    for (i = 0; i < L1_ENTRIES; i++) {
        ULONG e = l1(root)[i];

        if ((e & L1_TYPE_MASK) != L1_COARSE)
            continue;
        for (k = 0; k < n; k++)
            if ((e & ~(PAGE - 1)) == pages[k]) {
                l1(root)[i] = 0;
                break;
            }
    }
    barrier();
}

static void invalidate_asid(void)
{
    ULONG zero = 0;

    __asm__ volatile ("dsb" : : : "memory");
    __asm__ volatile ("mcr p15, 0, %0, c8, c7, 2" : : "r" (zero) : "memory");  /* TLBIASID, ASID 0 */
    __asm__ volatile ("mcr p15, 0, %0, c7, c5, 6" : : "r" (zero) : "memory");  /* branch predictor */
    __asm__ volatile ("dsb" : : : "memory");
    __asm__ volatile ("isb" : : : "memory");
}

PADDR aspace_hw_current(void)
{
    ULONG ttbr0;

    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r" (ttbr0));
    return ttbr0 & ~(L1_BYTES - 1);
}

void aspace_hw_switch(PADDR root)
{
    __asm__ volatile ("dsb" : : : "memory");
    __asm__ volatile ("mcr p15, 0, %0, c2, c0, 0" : : "r" ((ULONG)root | TTBR_MODE) : "memory");
    __asm__ volatile ("isb" : : : "memory");
    invalidate_asid();
}

void aspace_hw_flush(void)
{
    invalidate_asid();
}

void aspace_hw_code_changed(void)
{
    ULONG zero = 0;

    flush_data_cache_all();                                                     /* clean to memory */
    __asm__ volatile ("mcr p15, 0, %0, c7, c5, 0" : : "r" (zero) : "memory");   /* ICIALLU */
    __asm__ volatile ("mcr p15, 0, %0, c7, c5, 6" : : "r" (zero) : "memory");   /* BPIALL */
    __asm__ volatile ("dsb" : : : "memory");
    __asm__ volatile ("isb" : : : "memory");
}

#endif /* CONF_WITH_USER_ASPACE */
