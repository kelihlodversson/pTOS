/*
 * pgtable.c - x86-64 higher-half page table setup
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "portab.h"
#include "pgtable.h"

#define PTE_PRESENT  0x1ULL
#define PTE_WRITABLE 0x2ULL
#define PTE_PS       0x80ULL   /* 2 MiB page (only meaningful at the PD level) */
#define PTE_ADDR_MASK 0xFFFFFFFFFF000ULL

typedef UQUAD pgentry_t;

/*
 * One PML4, and a small pool of PDPT/PD tables allocated on demand as
 * x86_64_build_page_tables() maps the identity (low) and higher-half
 * windows. The higher-half window always needs exactly one PDPT and one
 * PD, since X86_64_KERNEL_VIRT_BASE is itself both 512 GiB- and 1 GiB-
 * aligned. The identity window normally needs one PDPT and at most two
 * PDs (it can cross one 1 GiB boundary for any span this milestone maps,
 * a few MiB) -- but if this image happened to be loaded within that span
 * of an exact 512 GiB boundary, it would need a second PDPT (and a
 * matching extra PD) too. That is astronomically unlikely on any real or
 * emulated firmware, but pml4_slot_pdpt()/pdpt_slot_pd() below hard-trap
 * rather than silently overrunning the pool if it ever happens, so these
 * sizes only need to comfortably cover the normal case plus that
 * worst-case doubling, not be provably exhaustive.
 */
#define MAX_PDPTS 3
#define MAX_PDS 6

static pgentry_t pml4[512] __attribute__((aligned(4096)));
static pgentry_t pdpts[MAX_PDPTS][512] __attribute__((aligned(4096)));
static pgentry_t pds[MAX_PDS][512] __attribute__((aligned(4096)));
static int next_pdpt;
static int next_pd;

/* Returns the PDPT for pml4[pml4_index], allocating one from the pool on
 * first use. Traps (see the MAX_PDPTS comment above) rather than
 * overrunning the pdpts[] pool if the caller ever needs more distinct
 * PML4 slots than provisioned for. */
static pgentry_t *pml4_slot_pdpt(UQUAD pml4_index)
{
    pgentry_t *pdpt;

    if (!(pml4[pml4_index] & PTE_PRESENT)) {
        if (next_pdpt >= MAX_PDPTS)
            __builtin_trap();
        pdpt = pdpts[next_pdpt++];
        pml4[pml4_index] = (UQUAD)(uintptr_t)pdpt | PTE_WRITABLE | PTE_PRESENT;
    }
    return (pgentry_t *)(uintptr_t)(pml4[pml4_index] & PTE_ADDR_MASK);
}

/* Returns the PD for pdpt[pdpt_index], allocating one from the pool on
 * first use. Traps (see the MAX_PDPTS comment above) rather than
 * overrunning the pds[] pool if the caller ever needs more distinct PDPT
 * slots than provisioned for. */
static pgentry_t *pdpt_slot_pd(pgentry_t *pdpt, UQUAD pdpt_index)
{
    pgentry_t *pd;

    if (!(pdpt[pdpt_index] & PTE_PRESENT)) {
        if (next_pd >= MAX_PDS)
            __builtin_trap();
        pd = pds[next_pd++];
        pdpt[pdpt_index] = (UQUAD)(uintptr_t)pd | PTE_WRITABLE | PTE_PRESENT;
    }
    return (pgentry_t *)(uintptr_t)(pdpt[pdpt_index] & PTE_ADDR_MASK);
}

/* Maps one 2 MiB page at virt to phys, walking (and allocating, as
 * needed) the PDPT/PD chain for it -- so a range that happens to cross a
 * 1 GiB (PD) boundary is handled correctly by mapping into two different
 * PD tables, rather than overflowing a single 512-entry PD array. */
static void map_2m_page(UQUAD virt, UQUAD phys)
{
    UQUAD pml4_index = (virt >> 39) & 0x1FF;
    UQUAD pdpt_index = (virt >> 30) & 0x1FF;
    UQUAD pd_index = (virt >> 21) & 0x1FF;
    pgentry_t *pdpt = pml4_slot_pdpt(pml4_index);
    pgentry_t *pd = pdpt_slot_pd(pdpt, pdpt_index);

    pd[pd_index] = phys | PTE_PS | PTE_WRITABLE | PTE_PRESENT;
}

static void map_2m_range(UQUAD virt, UQUAD phys, UQUAD count)
{
    UQUAD i;

    for (i = 0; i < count; i++)
        map_2m_page(virt + i * X86_64_PAGE_2M_SIZE, phys + i * X86_64_PAGE_2M_SIZE);
}

UQUAD x86_64_build_page_tables(UQUAD phys_base, UQUAD span, UQUAD *out_aligned_base)
{
    UQUAD aligned_base = phys_base & ~(X86_64_PAGE_2M_SIZE - 1);
    UQUAD aligned_end = (phys_base + span + X86_64_PAGE_2M_SIZE - 1)
                        & ~(X86_64_PAGE_2M_SIZE - 1);
    UQUAD count = (aligned_end - aligned_base) / X86_64_PAGE_2M_SIZE;

    map_2m_range(aligned_base, aligned_base, count);
    map_2m_range(X86_64_KERNEL_VIRT_BASE, aligned_base, count);

    *out_aligned_base = aligned_base;
    return (UQUAD)(uintptr_t)pml4;
}
