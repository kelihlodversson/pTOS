/*
 * memory.c - x86-64 TPA memory pool stand-in
 *
 * Every other machine's biosmem.c (bios/biosmem.c) gets _end_os_stram and
 * phystop from a real linker script (emutos.ld/tosvars.ld) plus a bit of
 * machine-specific code that fills in phystop before biosmain() reaches
 * bmem_init() -- raspi's raspi_vcmem_init() (bios/machine/raspi/memory.c)
 * and virt-arm's startup.S (bios/machine/virt-arm/memory.c, empty on
 * purpose) are the two existing examples.
 *
 * x86-64 has no emutos.ld processing at all (see the ARCH_X86_64 branch of
 * the top level Makefile's $(EMUTOS_IMG) rule): it links as a PE32+ EFI
 * application via a raw "ld -m i386pep", not EmuTOS's own ROM/RAM linker
 * script, so none of _text/_etext/_data/_edata/_bss/_ebss/stkbot/stktop
 * exist as linker-provided symbols here. bios/bios.h's declarations of
 * those are only ever read from bios/biosmem.c's KDEBUG()-gated
 * diagnostics (ENABLE_KDEBUG is off by default), so leaving them
 * undefined is fine -- only _end_os_stram is read outside KDEBUG, in
 * bmem_init()'s membot/end_os setup.
 *
 * Real memory discovery already exists for this arch (pmem.c, built from
 * the EFI memory map), but that is a physical-page bump allocator feeding
 * the kernel's own paging setup, not something bmem_init() knows how to
 * consume -- it wants one flat [membot, memtop) range. Rather than
 * plumbing pmem.c's free list through biosmem.c's very different API this
 * early, stand in with an ordinary higher-half BSS array as the TPA pool
 * itself, matching how a small ROM/RAM EmuTOS build's own BSS-adjacent
 * free memory works. This is deliberately a placeholder: real GEMDOS
 * process launching (Pexec, #333) will need the pool to live in a low,
 * 32-bit-representable address range instead (include/bdosdefs.h's PD
 * struct stores a resume pointer into it as a plain LONG), which this
 * array does not attempt to satisfy. Sufficient for now, since nothing on
 * the path to bios_init()/biosmain() reaching CONF_WITH_CLI's EmuCON
 * launch exercises Pexec yet.
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_PC_X86_64
#error This file must only be compiled for the pc-x86_64 machine
#endif

#include "portab.h"
#include "tosvars.h"
#include "pc_x86_64_memory.h"
#include "pgtable.h"
#include "pmem.h"

#define POOL_BYTES (2 * 1024 * 1024)

UBYTE _end_os_stram[POOL_BYTES] __attribute__((aligned(16)));

void pc_x86_64_memory_init(void)
{
    phystop = &_end_os_stram[POOL_BYTES];
}

/*
 * A second, INDEPENDENT pool for #334's own GEMDOS process memory
 * (bdos/proc.c's alloc_tpa(), via its own __x86_64__ branch) -- unlike
 * _end_os_stram above, mapped at a real low, sub-4GiB, virtual-equals-
 * physical address, so a PD's p_lowtpa/p_hitpa/p_tbase/... (USERPTR_T,
 * bdosdefs.h) can hold it directly without truncation.
 *
 * Why this can't just be "_end_os_stram, but low": _end_os_stram is an
 * ordinary extern array (bios/biosmem.c's shared, unmodified bmem_init()
 * takes its address with a plain `(UQUAD)(uintptr_t)_end_os_stram`),
 * which -fpie always compiles as a RIP-relative `lea` -- fine as long as
 * the linker places the symbol within +-2 GiB of whatever code
 * references it, which it always does for an ORDINARY symbol (the
 * linker clusters a PE image's own sections together near its own link
 * base). Forcing that address down near 0 instead (the one thing
 * "low, sub-4GiB" needs) makes the *distance* from biosmem.o's own code
 * (linked close to this image's ~0x140000000 base) so large that the
 * relocation the compiler already emitted overflows
 * (R_X86_64_PC32, "relocation truncated to fit") -- tried once this
 * session, confirmed, reverted. See #351/#334.
 *
 * This pool sidesteps that entirely by never being an addressable
 * *symbol* at all: alloc_tpa()'s __x86_64__ branch gets its base as an
 * ordinary runtime VALUE (a plain integer constant cast to a pointer,
 * X86_64_LOW_TPA_VIRT_BASE below -- needing no relocation whatsoever,
 * just an immediate load) plumbed through x86_64_low_tpa_alloc(), not
 * through _end_os_stram/membot/memtop's own machinery at all.
 *
 * A single-shot bump allocator, like _end_os_stram's own "stand-in"
 * pool above and pmem.c's physical allocator underneath it: acceptable
 * because #334's own scope explicitly excludes multi-process/scheduling
 * concerns ("what's needed to demonstrate one process running and
 * exiting"), so nothing needs to ever free memory allocated from here
 * yet. bdos/umem.c's set_owner()/xmfree() both already handle an
 * address outside every known MPB gracefully (find_mpb() returns NULL;
 * set_owner() no-ops, xmfree() returns EIMBA) -- exactly what happens
 * for memory this pool hands out, since it deliberately never registers
 * with pmd/pmdalt's MD-list bookkeeping.
 */
#define X86_64_LOW_TPA_VIRT_BASE X86_64_PAGE_2M_SIZE
#define X86_64_LOW_TPA_BYTES (2 * 1024 * 1024)

static UQUAD low_tpa_next;
static UQUAD low_tpa_end;

void x86_64_low_tpa_init(void)
{
    /*
     * x86_64_map_kernel_pages() (like every 2 MiB mapping this file's
     * own pgtable.c builds) requires a 2 MiB-ALIGNED backing_phys --
     * x86_64_pmem_alloc_pages_below() only guarantees 4 KiB alignment
     * (pmem.c's free-region bump allocator has no coarser granularity),
     * so an extra 2 MiB is requested and the returned base rounded up,
     * exactly as x86_64_build_page_tables() itself rounds phys_base.
     * The rounding can waste at most one 2 MiB page, comfortably within
     * the slack requested.
     */
    UQUAD raw = x86_64_pmem_alloc_pages_below(
        (X86_64_LOW_TPA_BYTES + X86_64_PAGE_2M_SIZE) / X86_64_PAGE_SIZE,
        0x100000000ULL);
    UQUAD phys = (raw + X86_64_PAGE_2M_SIZE - 1) & ~(X86_64_PAGE_2M_SIZE - 1);

    x86_64_map_kernel_pages(X86_64_LOW_TPA_VIRT_BASE, phys,
                             X86_64_LOW_TPA_BYTES / X86_64_PAGE_2M_SIZE);

    low_tpa_next = X86_64_LOW_TPA_VIRT_BASE;
    low_tpa_end = X86_64_LOW_TPA_VIRT_BASE + X86_64_LOW_TPA_BYTES;
}

UBYTE *x86_64_low_tpa_alloc(LONG needed)
{
    UQUAD aligned = (low_tpa_next + 15) & ~(UQUAD)15;

    if (aligned + (UQUAD)needed > low_tpa_end)
        return NULL;

    low_tpa_next = aligned + (UQUAD)needed;
    return (UBYTE *)(uintptr_t)aligned;
}

