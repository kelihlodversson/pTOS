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
 * worst-case doubling, not be provably exhaustive. +1 each over that
 * covers x86_64_map_low_vectors()'s own single-2 MiB-page mapping (#349),
 * which (like the identity window) falls in PML4 slot 0 and so needs a
 * fresh PDPT/PD of its own once x86_64_drop_identity_map() has cleared
 * whatever the identity window used there.
 */
#define MAX_PDPTS 4
#define MAX_PDS 7

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

/* The identity (low) window's bounds, as x86_64_build_page_tables() last
 * set them up -- remembered here (rather than left for each caller to
 * recompute) so phys_addr_of()/x86_64_low_to_high()/
 * x86_64_drop_identity_map() below have a single source of truth for
 * exactly what that window covers. */
static UQUAD identity_low_base;
static UQUAD identity_low_end;

/*
 * Converts a pointer to this image's own static data -- as a file-static
 * array's address is always computed, i.e. RIP-relative -- into the
 * physical address that belongs in a page-table entry. Such a computation
 * reflects whichever alias is CURRENTLY executing it: this image's actual
 * low load address before the higher-half jump (startup.c), or its high
 * alias after (x86_64_low_to_high()'s own comment has the full story).
 * Needed because pml4_slot_pdpt()/pdpt_slot_pd() below are called from
 * both sides of that jump -- x86_64_build_page_tables() before it,
 * x86_64_map_low_vectors() after -- and only the low case is already a
 * physical address; the high one needs translating back down, or it gets
 * stored verbatim as a physical frame number with a canonical-high-half
 * bit pattern nowhere near this system's actual physical address width
 * (the exact bug x86_64_build_physmap() was separately fixed for, and
 * this needs its own fix rather than reusing that one: physmap_pdpt is
 * only ever built pre-jump, but pdpts[]/pds[] are now built on both
 * sides).
 */
static UQUAD phys_addr_of(const void *p)
{
    UQUAD addr = (UQUAD)(uintptr_t)p;

    if (addr >= X86_64_KERNEL_VIRT_BASE)
        return addr - X86_64_KERNEL_VIRT_BASE + identity_low_base;
    return addr;
}

/* Returns the PDPT for pml4[pml4_index], allocating one from the pool on
 * first use. Traps (see the MAX_PDPTS comment above) rather than
 * overrunning the pdpts[] pool if the caller ever needs more distinct
 * PML4 slots than provisioned for.
 *
 * Always returns the pool array element directly (a pointer dereferenceable
 * from wherever this is CURRENTLY called from), never a pointer
 * reconstructed from the table entry's stored (always physical, see
 * phys_addr_of()) address: that reconstruction would only be
 * dereferenceable when physical addresses happen to equal virtual ones,
 * true only in the low/identity context. This means a pml4_index whose
 * entry already exists must have been allocated during the SAME low-or-
 * high context as the current call -- true for every caller today (each
 * only ever (re)uses a pml4_index it fully owns within one call), but not
 * proven for all possible future ones.
 */
static pgentry_t *pml4_slot_pdpt(UQUAD pml4_index)
{
    pgentry_t *pdpt;

    if (!(pml4[pml4_index] & PTE_PRESENT)) {
        if (next_pdpt >= MAX_PDPTS)
            __builtin_trap();
        pdpt = pdpts[next_pdpt++];
        pml4[pml4_index] = phys_addr_of(pdpt) | PTE_WRITABLE | PTE_PRESENT;
        return pdpt;
    }
    return pdpts[next_pdpt - 1];
}

/* Returns the PD for pdpt[pdpt_index], allocating one from the pool on
 * first use. Traps (see the MAX_PDPTS comment above) rather than
 * overrunning the pds[] pool if the caller ever needs more distinct PDPT
 * slots than provisioned for. See pml4_slot_pdpt()'s own comment for why
 * this always returns a pool array element directly, and its stated
 * limitation. */
static pgentry_t *pdpt_slot_pd(pgentry_t *pdpt, UQUAD pdpt_index)
{
    pgentry_t *pd;

    if (!(pdpt[pdpt_index] & PTE_PRESENT)) {
        if (next_pd >= MAX_PDS)
            __builtin_trap();
        pd = pds[next_pd++];
        pdpt[pdpt_index] = phys_addr_of(pd) | PTE_WRITABLE | PTE_PRESENT;
        return pd;
    }
    return pds[next_pd - 1];
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

/*
 * Identity-maps physical/virtual [0, 2 MiB) -- a single 2 MiB page -- and
 * zeroes it. Must be called after x86_64_drop_identity_map(): both target
 * PML4 slot 0 (any real or emulated system has far less than 512 GiB of
 * RAM, so both the image's own identity window and address 0 fall in the
 * same slot), and this needs that slot already cleared so it allocates
 * its own fresh PDPT/PD there rather than corrupting whatever the
 * identity window's now-dangling one still occupied.
 *
 * This is the low system-vector area the shared core's generic bios_init()
 * unconditionally writes through (VEC_GEM/VEC_BIOS/VEC_XBIOS at their
 * traditional m68k addresses, bios/vectors.h) and the trap dispatch path
 * (#349) reads back from -- every other pTOS port already reserves this
 * same low range for exactly this, whether it is real m68k hardware or
 * ARM's own simulated equivalent (bios/arch/arm/vectors.c). Zeroing it
 * (rather than leaving whatever garbage was physically there) makes every
 * not-yet-installed vector a null pointer: dereferencing one faults
 * straight into this arch's own panic path (#331), which needs no
 * ARM-style "any_vec" indirection to produce a readable diagnostic.
 */
void x86_64_map_low_vectors(void)
{
    volatile UQUAD *p = (volatile UQUAD *)(uintptr_t)0;
    UQUAD i;

    map_2m_range(0, 0, 1);
    reload_cr3();

    for (i = 0; i < 4096 / sizeof(UQUAD); i++)
        p[i] = 0;
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
