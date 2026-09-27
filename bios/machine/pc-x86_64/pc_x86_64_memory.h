/*
 * pc_x86_64_memory.h - x86-64 TPA memory pool stand-in
 *
 * Not named memory.h: the include-path search order for every file
 * compiled under bios/ (not just this machine's own memory.c) puts
 * bios/machine/pc-x86_64/ ahead of bios/ itself, so a memory.h here would
 * shadow -- and break -- bios/memory.h's #include "memory.h" users
 * (bios.c, memory2.c, machine.c, machine.h), the same reason raspi's
 * equivalent header is raspi_memory.h rather than memory.h.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef PC_X86_64_MEMORY_H
#define PC_X86_64_MEMORY_H

/* Sets phystop (tosvars.h) to the end of the placeholder TPA pool. Must
 * be called before biosmain() reaches bios/biosmem.c's bmem_init(). See
 * memory.c for why this is a stand-in rather than real memory discovery. */
void pc_x86_64_memory_init(void);

/*
 * Allocates and low-maps (see memory.c's own comment) the second pool
 * bdos/proc.c's alloc_tpa() uses on this arch. Must run after
 * x86_64_build_physmap() (x86_64_pmem_alloc_pages_below() reads back
 * through the physical-memory direct map) and after
 * x86_64_map_low_vectors() (shares PML4 slot 0 with it, see
 * x86_64_map_kernel_pages()'s own comment in pgtable.h) -- in practice,
 * any time after pc_x86_64_memory_init() itself, which already satisfies
 * both preconditions.
 */
void x86_64_low_tpa_init(void);

/*
 * Bump-allocates `needed` bytes (16-byte aligned, matching _end_os_stram's
 * own alignment attribute) from the pool x86_64_low_tpa_init() set up.
 * Returns NULL if the pool is exhausted -- unlike x86_64_pmem_alloc_pages()
 * one level down, callers here (bdos/proc.c's alloc_tpa()) already have
 * an established "return NULL, caller reports ENSMEM" convention to use
 * instead of panicking.
 */
UBYTE *x86_64_low_tpa_alloc(LONG needed);

/*
 * Maps the whole low TPA pool (the same range x86_64_low_tpa_alloc()
 * hands out from) into pml4_phys -- a process address space
 * x86_64_new_address_space() already built -- with user+writable+
 * executable leaf permissions. Without this, a real loaded process's
 * own text/data/bss/heap/stack and its own PD/basepage (bdos/proc.c's
 * alloc_tpa()/alloc_env(), bdosmain.c's initial_basepage: all carved
 * from this same pool) are simply not present in its address space --
 * x86_64_new_address_space() clears PML4 slot 0 like every other low
 * slot -- so the process faults on its very first instruction once
 * CR3 is switched. Called from bdos/arch/x86_64/rwa.c's gouser(),
 * after x86_64_new_address_space() and before x86_64_enter_user().
 * One flat mapping for the whole pool, not just the calling process's
 * own p_lowtpa..p_hitpa sub-range: #334's own scope excludes multi-
 * process concerns, and every process today shares this single pool
 * (see memory.c's own comment on it), so there is no narrower "this
 * process's own pages" to compute yet.
 */
void x86_64_map_low_tpa_into(UQUAD pml4_phys);

#endif /* PC_X86_64_MEMORY_H */
