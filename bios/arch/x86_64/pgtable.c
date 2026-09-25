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
#define PTE_PS       0x80ULL   /* 2 MiB (PD) or 1 GiB (PDPT) page, context-dependent */
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

/* One 1 GiB-page PDPT, dedicated to the physical-memory direct map
 * (X86_64_PHYS_MAP_BASE): 512 entries at 1 GiB each reach 512 GiB, ample
 * for anything this port's target hardware/VMs report, without needing a
 * PD/PT level at all -- see x86_64_build_physmap(). */
static pgentry_t physmap_pdpt[512] __attribute__((aligned(4096)));

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

/* The identity (low) window's bounds, as x86_64_build_page_tables() last
 * set them up -- remembered here (rather than left for each caller to
 * recompute) so x86_64_low_to_high() and x86_64_drop_identity_map() below
 * have a single source of truth for exactly what that window covers. */
static UQUAD identity_low_base;
static UQUAD identity_low_end;

UQUAD x86_64_build_page_tables(UQUAD phys_base, UQUAD span, UQUAD *out_aligned_base)
{
    UQUAD aligned_base = phys_base & ~(X86_64_PAGE_2M_SIZE - 1);
    UQUAD aligned_end = (phys_base + span + X86_64_PAGE_2M_SIZE - 1)
                        & ~(X86_64_PAGE_2M_SIZE - 1);
    UQUAD count = (aligned_end - aligned_base) / X86_64_PAGE_2M_SIZE;

    map_2m_range(aligned_base, aligned_base, count);
    map_2m_range(X86_64_KERNEL_VIRT_BASE, aligned_base, count);

    identity_low_base = aligned_base;
    identity_low_end = aligned_end;

    *out_aligned_base = aligned_base;
    return (UQUAD)(uintptr_t)pml4;
}

static inline void reload_cr3(void)
{
    UQUAD cr3;

    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

/*
 * Translates a low (identity-mapped) address to its higher-half virtual
 * counterpart -- the same relationship x86_64_build_page_tables() itself
 * mapped, expressed as X86_64_KERNEL_VIRT_BASE plus the address's offset
 * from the identity window's own (2 MiB-aligned) base. Needed anywhere a
 * pointer gets baked in as compile-time-initialized data rather than
 * computed at runtime: such a pointer is fixed up by the PE loader to
 * this image's low load address (see x86_64_drop_identity_map()'s own
 * comment for why that stays true even for code that only ever runs
 * post-relocation), so it must be translated explicitly wherever it is
 * later read as a live address -- unlike an ordinary RIP-relative
 * pointer computation, which already reflects whatever alias is
 * currently executing (see startup.c's commentary on that distinction).
 * Only valid for addresses inside the window x86_64_build_page_tables()
 * was last told to map.
 */
UQUAD x86_64_low_to_high(UQUAD low_addr)
{
    return X86_64_KERNEL_VIRT_BASE + (low_addr - identity_low_base);
}

/*
 * Removes the identity (low) mapping x86_64_build_page_tables() built,
 * freeing that low canonical address range for #334's future ILP32 user
 * processes (see #343 and #344's address-space-split scope) -- the
 * kernel itself has no further use for it once every low-address pointer
 * baked into its own compile-time data has been translated via
 * x86_64_low_to_high() (idt.c's exception_stub[] jump table and panic.c's
 * vector_names[] string table, at the time this was written -- see their
 * own call sites). Only clears whichever PML4 slot(s) the identity window
 * fell in (ordinarily one; see the MAX_PDPTS comment above for why it
 * could exceptionally be two), leaving the higher-half kernel mapping and
 * the physical-memory direct map (both distinct, non-overlapping PML4
 * slots -- see pgtable.h) untouched. Reloads CR3 to flush the removed
 * translation from the TLB.
 */
void x86_64_drop_identity_map(void)
{
    UQUAD first_index = (identity_low_base >> 39) & 0x1FF;
    UQUAD last_index = ((identity_low_end - 1) >> 39) & 0x1FF;
    UQUAD i;

    for (i = first_index; i <= last_index; i++)
        pml4[i] = 0;

    reload_cr3();
}

/* CPUID.80000001H:EDX.Page1GB [bit 26] -- support for 1 GiB pages at the
 * PDPT level, which x86_64_build_physmap() relies on. Querying an
 * extended leaf this way is always valid on any CPU already running this
 * code: reaching long mode at all required CPUID.80000001H:EDX.LM to be
 * queried and found set, so leaf 0x80000001 is guaranteed to exist. Real
 * silicon has had this since ~2010 (AMD Barcelona, Intel Westmere), but
 * some conservative default virtual CPU models (e.g. QEMU's "qemu64")
 * still do not advertise it. */
int x86_64_cpu_has_1g_pages(void)
{
    ULONG eax = 0x80000001;
    ULONG ebx, ecx, edx;

    __asm__ volatile ("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    return (edx >> 26) & 1;
}

void x86_64_build_physmap(UQUAD max_phys)
{
    UQUAD count = (max_phys + X86_64_PAGE_1G_SIZE - 1) / X86_64_PAGE_1G_SIZE;
    UQUAD pml4_index = (X86_64_PHYS_MAP_BASE >> 39) & 0x1FF;
    UQUAD i;

    /* Without 1 GiB page support, the PS bit below is architecturally
     * reserved-must-be-zero at the PDPT level: setting it anyway does not
     * degrade to some smaller page size, it raises #PF with the reserved-
     * bit flag set. There is no PD/PT-level fallback implemented here (see
     * the CPUID comment above for why real hardware is not expected to
     * need one) -- trap loudly rather than silently building a mapping
     * that faults on first use. (The caller, startup.c, already checked
     * this earlier via a clean panic(); this is a second, redundant trap
     * in case this function is ever called from somewhere that skipped
     * that check.) */
    if (!x86_64_cpu_has_1g_pages())
        __builtin_trap();

    /* count can only exceed physmap_pdpt[]'s 512 entries (512 GiB) if
     * max_phys is itself absurd for this port's targets -- trap rather
     * than silently mapping less than promised. */
    if (count > 512)
        __builtin_trap();

    /* Must be a fresh PML4 slot: X86_64_PHYS_MAP_BASE is chosen well clear
     * of both the identity window (low addresses) and X86_64_KERNEL_VIRT_BASE
     * (see pgtable.h), so this can never legitimately collide with an
     * entry x86_64_build_page_tables() already made. */
    if (pml4[pml4_index] & PTE_PRESENT)
        __builtin_trap();

    pml4[pml4_index] = (UQUAD)(uintptr_t)physmap_pdpt | PTE_WRITABLE | PTE_PRESENT;

    for (i = 0; i < count; i++)
        physmap_pdpt[i] = (i * X86_64_PAGE_1G_SIZE) | PTE_PS | PTE_WRITABLE | PTE_PRESENT;

    reload_cr3();
}
