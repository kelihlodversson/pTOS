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

#endif /* __ASSEMBLER__ */

#endif /* X86_64_PGTABLE_H */
