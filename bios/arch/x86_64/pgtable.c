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
 * windows. Two PDPTs are enough: the low and high windows each need
 * exactly one PML4 slot, and mapping a range that crosses a 512 GiB
 * boundary (a different PML4 slot within the *same* window) cannot
 * happen for an image this small. Four PDs are generous headroom: the
 * identity window can span at most two 1 GiB-aligned PDs for any
 * span this milestone maps (a few MiB), and the higher-half window
 * never needs more than one, since X86_64_KERNEL_VIRT_BASE is itself
 * 1 GiB-aligned.
 */
#define MAX_PDPTS 2
#define MAX_PDS 4

static pgentry_t pml4[512] __attribute__((aligned(4096)));
static pgentry_t pdpts[MAX_PDPTS][512] __attribute__((aligned(4096)));
static pgentry_t pds[MAX_PDS][512] __attribute__((aligned(4096)));
static int next_pdpt;
static int next_pd;

/* Returns the PDPT for pml4[pml4_index], allocating one from the pool on
 * first use. */
static pgentry_t *pml4_slot_pdpt(UQUAD pml4_index)
{
    pgentry_t *pdpt;

    if (!(pml4[pml4_index] & PTE_PRESENT)) {
        pdpt = pdpts[next_pdpt++];
        pml4[pml4_index] = (UQUAD)(uintptr_t)pdpt | PTE_WRITABLE | PTE_PRESENT;
    }
    return (pgentry_t *)(uintptr_t)(pml4[pml4_index] & PTE_ADDR_MASK);
}

/* Returns the PD for pdpt[pdpt_index], allocating one from the pool on
 * first use. */
static pgentry_t *pdpt_slot_pd(pgentry_t *pdpt, UQUAD pdpt_index)
{
    pgentry_t *pd;

    if (!(pdpt[pdpt_index] & PTE_PRESENT)) {
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
