/*
 * pgtable.h - x86-64 higher-half page table setup
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef X86_64_PGTABLE_H
#define X86_64_PGTABLE_H

/*
 * relocate.S includes this header too (for X86_64_KERNEL_VIRT_BASE), and
 * GAS immediates do not understand a C integer suffix such as "ULL".
 */
#ifdef __ASSEMBLER__
#define X86_64_KERNEL_VIRT_BASE 0xFFFFFFFF80000000
#else

#include "portab.h"

/*
 * Virtual base of the higher-half kernel mapping: the classic "top -2 GiB"
 * x86-64 kernel convention (a canonical address, since bits 47-63 are all
 * set).  See issue #329/#330.
 */
#define X86_64_KERNEL_VIRT_BASE 0xFFFFFFFF80000000ULL

/* Size of one identity-mapped/higher-half-mapped 2 MiB page table entry. */
#define X86_64_PAGE_2M_SIZE 0x200000ULL

/* Size of one physical-memory direct-map 1 GiB page table entry. */
#define X86_64_PAGE_1G_SIZE 0x40000000ULL

/*
 * Virtual base of the permanent physical-memory direct map: every
 * physical address x86_64_build_physmap() was told to cover is also
 * reachable at this virtual base plus its physical address. Chosen well
 * below X86_64_KERNEL_VIRT_BASE (itself the top -2 GiB) so the two
 * windows can never collide as long as physical RAM stays under 128 TiB
 * -- outlandish for the hardware and VMs this port targets. Canonical
 * (bits 47-63 all 1, like X86_64_KERNEL_VIRT_BASE) and 1 GiB-aligned, as
 * x86_64_build_physmap()'s use of 1 GiB pages requires.
 */
#define X86_64_PHYS_MAP_BASE 0xFFFF800000000000ULL

/*
 * Build a minimal set of page tables identity-mapping [phys_base, phys_base
 * + span) and additionally mapping the same physical range at
 * X86_64_KERNEL_VIRT_BASE.  phys_base is rounded down, and span rounded up,
 * to a 2 MiB boundary internally; *out_aligned_base receives that rounded-
 * down base, since X86_64_KERNEL_VIRT_BASE corresponds to it (not to the
 * unrounded phys_base) -- callers translating their own low addresses to
 * their higher-half counterparts must subtract *out_aligned_base, not
 * phys_base, or they will be off by (phys_base - *out_aligned_base).
 * Returns the physical address to load into CR3 (the PML4 table).
 *
 * The tables and everything they map (this image's own code, data, bss,
 * boot-time stack and the tables themselves) must fit within [phys_base,
 * phys_base + span); the caller is responsible for that.
 */
UQUAD x86_64_build_page_tables(UQUAD phys_base, UQUAD span, UQUAD *out_aligned_base);

/*
 * Maps [0, max_phys) into the permanent physical-memory direct map at
 * X86_64_PHYS_MAP_BASE, using 1 GiB pages -- so any physical address the
 * physical-memory allocator (pmem.c) hands out is reachable simply by
 * adding X86_64_PHYS_MAP_BASE, without needing its own entry in whatever
 * page tables the kernel builds for its own mappings later.  max_phys is
 * rounded up to a 1 GiB boundary internally.
 *
 * Must be called after x86_64_build_page_tables(), which this reuses the
 * PML4 of, and -- critically -- while still running at this image's
 * actual (low) load address, i.e. before the higher-half jump (see
 * startup.c): &physmap_pdpt is a position-independent (RIP-relative)
 * computation, so calling this after that jump would compute physmap_pdpt's
 * *virtual* higher-half address and store that where a physical one
 * belongs, corrupting the entry (an earlier version of this code did
 * exactly that). Reloads CR3 itself before returning; harmless if CR3
 * does not point at this PML4 yet (still EFI's own, at the call site this
 * requires), and required once it does.
 */
void x86_64_build_physmap(UQUAD max_phys);

/* True if this CPU supports 1 GiB pages (CPUID.80000001H:EDX.Page1GB),
 * which x86_64_build_physmap() requires -- see its own comment. Exposed so
 * callers can panic with a clear message before ever calling it, rather
 * than via that function's own (uninformative, IDT-less at that point in
 * boot) trap. */
int x86_64_cpu_has_1g_pages(void);

#endif /* __ASSEMBLER__ */

#endif /* X86_64_PGTABLE_H */
