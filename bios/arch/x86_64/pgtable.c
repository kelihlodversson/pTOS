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

typedef UQUAD pgentry_t;

/*
 * One PML4, and one PDPT/PD pair for the low (identity) window and one for
 * the high (X86_64_KERNEL_VIRT_BASE) window.  Milestone 1's image is a few
 * KB, comfortably inside a single PD's reach (1 GiB) on either side, so two
 * PDPT/PD pairs are enough; this does not yet handle a kernel spanning more
 * than 1 GiB or crossing a PDPT boundary.
 */
static pgentry_t pml4[512] __attribute__((aligned(4096)));
static pgentry_t pdpt_low[512] __attribute__((aligned(4096)));
static pgentry_t pdpt_high[512] __attribute__((aligned(4096)));
static pgentry_t pd_low[512] __attribute__((aligned(4096)));
static pgentry_t pd_high[512] __attribute__((aligned(4096)));

static void map_2m_range(pgentry_t *pd, UQUAD virt, UQUAD phys, UQUAD count)
{
    UQUAD i;
    UQUAD pd_index = (virt >> 21) & 0x1FF;

    for (i = 0; i < count; i++)
        pd[pd_index + i] = (phys + i * X86_64_PAGE_2M_SIZE) | PTE_PS | PTE_WRITABLE | PTE_PRESENT;
}

UQUAD x86_64_build_page_tables(UQUAD phys_base, UQUAD span, UQUAD *out_aligned_base)
{
    UQUAD aligned_base = phys_base & ~(X86_64_PAGE_2M_SIZE - 1);
    UQUAD aligned_end = (phys_base + span + X86_64_PAGE_2M_SIZE - 1)
                        & ~(X86_64_PAGE_2M_SIZE - 1);
    UQUAD count = (aligned_end - aligned_base) / X86_64_PAGE_2M_SIZE;
    UQUAD virt_high = X86_64_KERNEL_VIRT_BASE;

    UQUAD pml4_low_index = (aligned_base >> 39) & 0x1FF;
    UQUAD pdpt_low_index = (aligned_base >> 30) & 0x1FF;
    UQUAD pml4_high_index = (virt_high >> 39) & 0x1FF;
    UQUAD pdpt_high_index = (virt_high >> 30) & 0x1FF;

    map_2m_range(pd_low, aligned_base, aligned_base, count);
    map_2m_range(pd_high, virt_high, aligned_base, count);

    pdpt_low[pdpt_low_index] = (UQUAD)(uintptr_t)pd_low | PTE_WRITABLE | PTE_PRESENT;
    pdpt_high[pdpt_high_index] = (UQUAD)(uintptr_t)pd_high | PTE_WRITABLE | PTE_PRESENT;

    /*
     * X86_64_KERNEL_VIRT_BASE's PML4 slot (511) cannot collide with the low
     * window's slot for any physical load address a real firmware would
     * hand out (that would require loading this image above the 39-bit
     * per-PML4-slot boundary, i.e. at or above physical 512 GiB), so the
     * two PML4 entries below are always distinct.
     */
    pml4[pml4_low_index] = (UQUAD)(uintptr_t)pdpt_low | PTE_WRITABLE | PTE_PRESENT;
    pml4[pml4_high_index] = (UQUAD)(uintptr_t)pdpt_high | PTE_WRITABLE | PTE_PRESENT;

    *out_aligned_base = aligned_base;
    return (UQUAD)(uintptr_t)pml4;
}
