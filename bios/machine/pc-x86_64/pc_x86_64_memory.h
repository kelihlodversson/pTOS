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
 * any time after pc_x86_64_memory_init() itself, which both preconditions
 * already hold by.
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

#endif /* PC_X86_64_MEMORY_H */
