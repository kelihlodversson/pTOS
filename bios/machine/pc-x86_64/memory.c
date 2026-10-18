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
#include "procmem.h"

#define POOL_BYTES (2 * 1024 * 1024)

UBYTE _end_os_stram[POOL_BYTES] __attribute__((aligned(16)));

void pc_x86_64_memory_init(void)
{
    phystop = &_end_os_stram[POOL_BYTES];
}

/*
 * The second pool, the low process window (#334), lives in procmem.c now:
 * bdos/proc.c's alloc_tpa()/alloc_env() take reclaimable, tracked pages
 * from it instead of bump-allocating from a pool that never gave memory
 * back.  X86_64_LOW_TPA_VIRT_BASE/X86_64_LOW_TPA_BYTES (pc_x86_64_memory.h)
 * still say where it sits, because the pool below is placed after it.
 */

/* Above the user image window (include/procmem.h): every address space maps
 * the framebuffer supervisor-only, so it must not collide with anything a
 * process maps for itself. */
#define X86_64_LOW_FB_VIRT_BASE (USER_IMAGE_BASE + USER_IMAGE_SIZE)

/*
 * A third pool (#332): the EFI GOP framebuffer, mapped read/write into
 * this kernel's own page tables at a fixed low virtual base right after
 * the user image window. Unlike the other two, this one's physical backing
 * isn't allocated from pmem.c at all -- it's wherever firmware/hardware
 * already put the framebuffer (bios/machine/pc-x86_64/gop.c's own
 * gop_probe() reads that address from GOP itself) -- this file
 * only maps that caller-given range, it never chooses or owns it.
 *
 * Placed in the same low-canonical, PML4-slot-0 region as the TPA
 * pool (reusing their already-built PD, not a fresh PML4 slot) rather
 * than somewhere in the kernel's own higher half, for two reasons: it
 * costs no extra PDPT/PD-pool budget (pgtable.c's MAX_PDPTS/MAX_PDS,
 * already fully spoken for -- see that file's own comment) the way a new
 * PML4 slot would, and it means v_bas_ad (screen.c) ends up with a real,
 * sub-4GiB address "for free" -- one entry closer to closing #372's own
 * v_bas_ad line in ssystem.c's lval_table, though not itself a claim
 * that #372 is closed (the value stored there is still whatever a real
 * ILP32 consumer needs it to be, which this file has no say over).
 *
 * X86_64_FRAMEBUFFER_MAX_BYTES (pc_x86_64_memory.h -- public so gop.c's
 * gop_reserved_range() can apply the identical bound, see its own
 * comment there) is a sanity ceiling, not a real limit on any actual
 * framebuffer size: it exists so a firmware-reported mode this arch has
 * no business trusting blindly (a garbled FrameBufferSize, or a pixel
 * format check that somehow let through something enormous) fails closed
 * (x86_64_low_fb_init() returns 0, gop.c treats that as "no framebuffer")
 * rather than mapping an unbounded amount of physical address space on
 * the caller's say-so. 64 MiB comfortably covers every mode a QEMU/OVMF
 * or typical real firmware GOP implementation reports, up to and
 * including a 3840x2160 32bpp (4K) mode at a little over 33 MiB -- a
 * plain 16 MiB ceiling (this pool's original size) rejected that
 * resolution outright, and real firmware reporting it is not implausible
 * (Copilot review, PR #373). Costs nothing extra in PDPT/PD-pool budget
 * either way: this whole pool shares PML4 slot 0's single PD with the
 * TPA pool (see above), which covers up to 1 GiB of virtual space
 * at 2 MiB granularity -- 64 MiB is a small fraction of that, the same as
 * 16 MiB was.
 */

static UQUAD low_fb_end;

UQUAD x86_64_low_fb_init(UQUAD aligned_phys, UQUAD page_count)
{
    /*
     * Compared as a page count against a page-count ceiling, not as
     * page_count * X86_64_PAGE_2M_SIZE against a byte ceiling: gop.c
     * rejects any FrameBufferBase/FrameBufferSize pair that could
     * overflow a UQUAD by the time it reaches this range-in-2MiB-pages
     * form, but page_count itself is not otherwise bounded before this
     * point, so multiplying first could still wrap to a small value and
     * pass the byte-ceiling check it exists to enforce (Copilot review,
     * PR #373).
     */
    if (page_count > X86_64_FRAMEBUFFER_MAX_BYTES / X86_64_PAGE_2M_SIZE)
        return 0;

    x86_64_map_kernel_pages(X86_64_LOW_FB_VIRT_BASE, aligned_phys, page_count);
    low_fb_end = X86_64_LOW_FB_VIRT_BASE + page_count * X86_64_PAGE_2M_SIZE;
    return X86_64_LOW_FB_VIRT_BASE;
}

/*
 * The kernel-only low range [*start, *end) every process address space has
 * to carry besides the system-vector area at 0: the framebuffer, if
 * there is one.  Ring 0 touches them while servicing a
 * system call under the calling process's own CR3.
 */
void x86_64_low_kernel_range(UQUAD *start, UQUAD *end)
{
    *start = X86_64_LOW_FB_VIRT_BASE;
    *end = low_fb_end ? low_fb_end : X86_64_LOW_FB_VIRT_BASE;
}

