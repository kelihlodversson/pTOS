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
 * Size of the prefix of the identity-mapped low page (see
 * x86_64_map_low_vectors()) that is actually zeroed and used as the
 * simulated m68k system-vector area: one 4 KiB page, comfortably above
 * VEC_UNIMPINT (bios/vectors.h, 0xf4), the highest offset this arch's
 * bios_init() call chain writes into. Callers wanting to confirm this
 * exact range is real RAM before it is touched (x86_64_pmem_region_is_ram(),
 * bios/machine/pc-x86_64/pmem.h) use this same constant rather than
 * guessing a size independently.
 */
#define X86_64_LOW_VECTOR_BYTES 0x1000ULL

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
 * Translates a low (identity-mapped) address -- one inside the window the
 * most recent x86_64_build_page_tables() call was told to map -- to its
 * higher-half virtual counterpart. Needed wherever a pointer is baked in
 * as compile-time-initialized data (a jump table, a string-literal table,
 * ...) rather than computed at runtime: the PE loader fixes such a
 * pointer up to this image's low load address once, at load time, and
 * that stays true forever after, even for data only ever read from code
 * running post-relocation -- unlike an ordinary RIP-relative pointer
 * computation, which automatically reflects whatever alias currently
 * executes it (see startup.c's commentary on that distinction, and the
 * bug it names). idt.c's exception_stub[] and panic.c's vector_names[]
 * are the two places this codebase currently needs it.
 */
UQUAD x86_64_low_to_high(UQUAD low_addr);

/*
 * Removes the identity (low) mapping, freeing that address range for
 * #334's future ILP32 user processes (see #343 and #344's address-space
 * split). Safe to call only once every low-address pointer baked into
 * this image's own compile-time data has already been translated via
 * x86_64_low_to_high() and is no longer needed in its untranslated form
 * -- in particular, after x86_64_idt_init() has installed its gates.
 * Leaves the higher-half kernel mapping and the physical-memory direct
 * map untouched (distinct PML4 slots, see X86_64_KERNEL_VIRT_BASE and
 * X86_64_PHYS_MAP_BASE above). Reloads CR3 itself before returning.
 */
void x86_64_drop_identity_map(void);

/*
 * Maps virtual [0, 2 MiB) to backing_phys (a 2 MiB-aligned physical
 * address the caller allocated from the physical-memory allocator, real
 * RAM by construction -- not physical address 0 itself, which real PC/
 * UEFI firmware is not guaranteed to report as usable memory; see this
 * function's own comment in pgtable.c) and zeroes the low system-vector
 * area within it -- what bios_init() (bios/bios.c, generic)
 * unconditionally writes VEC_GEM/VEC_BIOS/VEC_XBIOS into, and the
 * GEMDOS/BIOS/XBIOS trap dispatch path (#349) reads back from. Must be
 * called after x86_64_drop_identity_map(): see that function's own
 * comment for why (both target PML4 slot 0).
 */
void x86_64_map_low_vectors(UQUAD backing_phys);

/*
 * True iff virt is backed by a present mapping in this kernel's own page
 * tables, checked read-only (never allocates, unlike
 * x86_64_build_page_tables()/x86_64_build_physmap()'s own internal PML4/
 * PDPT walking helpers). Requires x86_64_build_physmap() to have already
 * run (it reads back through the physical-memory direct map); safe any
 * time after that, including from an exception handler. See its own
 * comment in pgtable.c for exactly what "confirmed mapped" means here
 * (a 1 GiB or 2 MiB page; a further 4 KiB PT level, never created by
 * this file, reads as "not confirmed" rather than being walked).
 */
int x86_64_addr_mapped_readable(UQUAD virt);

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
