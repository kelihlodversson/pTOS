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
static UQUAD low_tpa_phys_base;

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
    low_tpa_phys_base = phys;
}

UBYTE *x86_64_low_tpa_alloc(LONG needed)
{
    /*
     * Page-aligned, not just 16-byte aligned: x86_64_map_low_tpa_into()
     * (gouser(), below) maps a process's own p_lowtpa..p_hitpa range
     * rounded OUT to whole 4 KiB pages (x86_64_map_user_page() only
     * ever maps a full leaf) -- with a finer-grained bump allocator,
     * that rounding could pull in a neighboring allocation's own data
     * (initial_basepage's own PD, bdosmain.c, is the first thing this
     * pool ever hands out, so the very first process's own TPA would
     * otherwise round back onto that same page) and expose it to ring
     * 3. Page-aligning every top-level allocation here instead means
     * no two different owners (kernel bookkeeping, one process's own
     * PD/TPA/env, or -- once #334 grows beyond one process -- a second
     * process's own) can ever share a page in the first place, closing
     * the gap at the source rather than in the mapping code (Copilot's
     * review of #356 caught this). Wastes at most one page per
     * allocation out of this pool's 2 MiB; #334's own single-process
     * scope makes that comfortably affordable.
     */
    UQUAD aligned = (low_tpa_next + (X86_64_PAGE_SIZE - 1)) & ~(UQUAD)(X86_64_PAGE_SIZE - 1);

    if (aligned + (UQUAD)needed > low_tpa_end)
        return NULL;

    low_tpa_next = aligned + (UQUAD)needed;
    return (UBYTE *)(uintptr_t)aligned;
}

/* one-line wrapper matching x86_64_map_user_page()'s alloc_page callback
 * shape (pgtable.h's own comment on that function names this exact
 * wrapper as the pc-x86_64 process loader's job) */
static UQUAD low_tpa_alloc_page(void)
{
    return x86_64_pmem_alloc_pages(1);
}

/*
 * Maps only [virt_start, virt_end) -- never the whole pool: mapping
 * every other process's PD/env/bookkeeping sharing this same pool as
 * user-accessible would let a ring-3 program corrupt or read
 * structures that aren't its own (Copilot's review of #356 caught
 * this). Rounded out to whole pages since x86_64_map_user_page() only
 * ever maps a full 4 KiB leaf: a range that shares a page boundary with
 * a neighboring allocation still exposes that page, an inherent
 * granularity limit this pool's page-aligned-per-allocation bump
 * allocator doesn't avoid, but is a far smaller residual than mapping
 * the entire 2 MiB pool.
 *
 * user (see this function's own comment in pc_x86_64_memory.h) is
 * forwarded straight to x86_64_map_user_page() -- 0 here means the
 * mapped range stays reachable through this process's own CR3 from
 * ring 0 (initial_basepage's own PD, gouser()'s p_parent call) but
 * traps if ring 3 itself ever touches it, instead of the user-writable
 * leaf every call here produced before a later review round caught it.
 */
void x86_64_map_low_tpa_into(UQUAD pml4_phys, UQUAD virt_start, UQUAD virt_end, int user)
{
    UQUAD page_start = virt_start & ~(UQUAD)(X86_64_PAGE_SIZE - 1);
    UQUAD page_end = (virt_end + X86_64_PAGE_SIZE - 1) & ~(UQUAD)(X86_64_PAGE_SIZE - 1);
    UQUAD virt;

    for (virt = page_start; virt < page_end; virt += X86_64_PAGE_SIZE)
        x86_64_map_user_page(pml4_phys, virt,
                              low_tpa_phys_base + (virt - X86_64_LOW_TPA_VIRT_BASE),
                              1, 1, user, low_tpa_alloc_page);
}

/*
 * A THIRD, independent pool (#351): kernel-owned structures that a 32-bit
 * ABI field hands out *by address* -- today, the cookie jar's own 'SCSI'
 * and '_5MS' entries (bios/machine.c's fill_cookie_jar(), guarded by
 * CONF_WITH_SCSI_DRIVER and !CONF_WITH_MFP, both true for this machine)
 * take the address of an ordinary higher-half kernel global
 * (scsidriv_root, vector_5ms) and truncate it to the ULONG a cookie's
 * value field can hold -- silently corrupting it, since this kernel's own
 * data lives at 0xffffffff80000000+, nowhere near representable in 32
 * bits. Not yet an active crash (nothing on this arch reads a cookie back
 * yet), but already-wrong data produced on every boot.
 *
 * Deliberately a THIRD pool rather than reusing the low TPA pool's own
 * spare space: the TPA pool's virtual range gets explicitly, selectively
 * remapped into a real process's own address space as user-accessible
 * (x86_64_map_low_tpa_into(), gouser()) -- carving kernel-only bookkeeping
 * out of the same physical range would depend on every future caller of
 * that function continuing to avoid this pool's particular offset, an
 * invariant nothing enforces. This pool is never passed to
 * x86_64_map_user_page() at all, so it can never become ring-3-reachable
 * by construction, not by convention.
 *
 * Like the TPA pool, this only relocates each structure's *storage*: the
 * variable itself becomes a pointer into this pool (set once, at
 * kernel-init time, to whatever x86_64_low_kdata_alloc() hands back),
 * with a macro making every existing read/write/address-of site
 * transparently dereference that pointer instead of naming a fixed
 * symbol -- each site remains an ordinary higher-half global on every
 * other arch, where a 32-bit cookie value has never been a problem. See
 * bios/vectors.h/bios/scsidriv.h's own __x86_64__ branches for the
 * macro-indirection this pool feeds.
 *
 * Whether a real ring-3 process's own BIOS/XBIOS call could ever reach
 * scsidriv_root/vector_5ms while running under that process's own CR3
 * (which does not map this pool at all, the same gap #352 already tracks
 * for the low system-vector page) is not addressed here: no BIOS/XBIOS
 * call reachable from #334's current single-process milestone touches
 * either structure, so this pool's own kernel-only reachability is
 * sufficient for now, not a claim that it always will be.
 */
#define X86_64_LOW_KDATA_VIRT_BASE (X86_64_LOW_TPA_VIRT_BASE + X86_64_LOW_TPA_BYTES)
#define X86_64_LOW_KDATA_BYTES (2 * 1024 * 1024)

static UQUAD low_kdata_next;
static UQUAD low_kdata_end;

void x86_64_low_kdata_init(void)
{
    UQUAD raw = x86_64_pmem_alloc_pages_below(
        (X86_64_LOW_KDATA_BYTES + X86_64_PAGE_2M_SIZE) / X86_64_PAGE_SIZE,
        0x100000000ULL);
    UQUAD phys = (raw + X86_64_PAGE_2M_SIZE - 1) & ~(X86_64_PAGE_2M_SIZE - 1);

    x86_64_map_kernel_pages(X86_64_LOW_KDATA_VIRT_BASE, phys,
                             X86_64_LOW_KDATA_BYTES / X86_64_PAGE_2M_SIZE);

    low_kdata_next = X86_64_LOW_KDATA_VIRT_BASE;
    low_kdata_end = X86_64_LOW_KDATA_VIRT_BASE + X86_64_LOW_KDATA_BYTES;
}

void *x86_64_low_kdata_alloc(LONG needed)
{
    UQUAD aligned = (low_kdata_next + 15) & ~(UQUAD)15;

    if (aligned + (UQUAD)needed > low_kdata_end)
        return NULL;

    low_kdata_next = aligned + (UQUAD)needed;
    return (void *)(uintptr_t)aligned;
}

/*
 * A FOURTH pool (#332): the EFI GOP framebuffer, mapped read/write into
 * this kernel's own page tables at a fixed low virtual base right after
 * the low-kdata pool. Unlike the other three, this one's physical backing
 * isn't allocated from pmem.c at all -- it's wherever firmware/hardware
 * already put the framebuffer (bios/machine/pc-x86_64/gop.c's own
 * x86_64_gop_probe() reads that address from GOP itself) -- this file
 * only maps that caller-given range, it never chooses or owns it.
 *
 * Placed in the same low-canonical, PML4-slot-0 region as the TPA/kdata
 * pools (reusing their already-built PD, not a fresh PML4 slot) rather
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
 * x86_64_gop_reserved_range() can apply the identical bound, see its own
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
 * TPA/kdata pools (see above), which covers up to 1 GiB of virtual space
 * at 2 MiB granularity -- 64 MiB is a small fraction of that, the same as
 * 16 MiB was.
 */
#define X86_64_LOW_FB_VIRT_BASE (X86_64_LOW_KDATA_VIRT_BASE + X86_64_LOW_KDATA_BYTES)

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
    return X86_64_LOW_FB_VIRT_BASE;
}

