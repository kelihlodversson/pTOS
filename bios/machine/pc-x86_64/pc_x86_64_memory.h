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
 * Maps [virt_start, virt_end) -- some range within the low TPA pool --
 * into pml4_phys, a process address space x86_64_new_address_space()
 * already built, writable+executable and rounded out to whole pages
 * (see this function's own comment in memory.c for why). Without this,
 * a real loaded process's own text/data/bss/heap/stack (bdos/proc.c's
 * alloc_tpa()/alloc_env(), all carved from this pool) are simply not
 * present in its address space -- x86_64_new_address_space() clears
 * PML4 slot 0 like every other low slot -- so the process faults on its
 * very first instruction once CR3 is switched. Called from bdos/arch/
 * x86_64/rwa.c's gouser(), after x86_64_new_address_space() and before
 * x86_64_enter_user(). Deliberately only ever a range this call names,
 * not the whole pool: every process shares this single pool (see
 * memory.c's own comment on it), so mapping all of it would let a
 * process reach every other process's/the kernel's own bookkeeping
 * sharing the same pool (initial_basepage, other processes' PDs/env
 * blocks) -- Copilot's review of #356 caught this.
 *
 * user selects the leaf permission: 1 for the calling process's own
 * p_env..p_hitpa range (ring 3 genuinely reads/writes/executes this),
 * 0 for kernel bookkeeping the process's own CR3 must still resolve
 * from ring 0 but ring 3 itself must never touch -- initial_basepage's
 * own PD, mapped in by gouser()'s separate p_parent call, is exactly
 * that case (a later review round caught it going out user-writable
 * like every other leaf this function had ever mapped).
 */
void x86_64_map_low_tpa_into(UQUAD pml4_phys, UQUAD virt_start, UQUAD virt_end, int user);

/*
 * Allocates and low-maps (see memory.c's own #351 comment) the third
 * pool: kernel-owned structures a 32-bit ABI field (the cookie jar, so
 * far) hands out by address, and so must have a real sub-4GiB address of
 * their own. Same preconditions as x86_64_low_tpa_init(); call it right
 * alongside that one.
 */
void x86_64_low_kdata_init(void);

/*
 * Bump-allocates `needed` bytes (16-byte aligned) from the pool
 * x86_64_low_kdata_init() set up, for a kernel-internal caller to point
 * its own low, sub-4GiB-address global at (bios/vectors.h's
 * x86_64_vector_5ms_ptr, bios/scsidriv.h's x86_64_scsidriv_root_ptr).
 * Returns NULL if the pool is exhausted; every current caller allocates
 * once, at boot, for a small fixed-size object, so this is a can't-happen
 * rather than a real error path.
 */
void *x86_64_low_kdata_alloc(LONG needed);

/*
 * Sanity ceiling for the fourth (EFI GOP framebuffer) pool below -- not a
 * real limit on any actual framebuffer size, just a bound past which a
 * firmware-reported mode is refused rather than mapped on faith (see
 * memory.c's own #332 comment on x86_64_low_fb_init() for the full
 * rationale). Public rather than private to memory.c so
 * bios/machine/pc-x86_64/gop.c's own x86_64_gop_reserved_range() can
 * apply the exact same bound before x86_64_pmem_init() ever reserves
 * physical memory for a mode x86_64_low_fb_init() will end up rejecting
 * anyway (Copilot review, PR #373) -- one constant, not two that could
 * drift apart.
 */
#define X86_64_FRAMEBUFFER_MAX_BYTES (64 * 1024 * 1024)

/*
 * Maps count 2 MiB pages of the CALLER-GIVEN physical range
 * [aligned_phys, aligned_phys + page_count*2MiB) -- already 2 MiB-aligned,
 * unlike x86_64_low_tpa_init()/x86_64_low_kdata_init()'s own allocations
 * -- at a fixed low virtual base right after the low-kdata pool (see
 * memory.c's own #332 comment). Used for the EFI GOP framebuffer
 * (bios/machine/pc-x86_64/gop.c), whose physical location this file has
 * no say over. Returns that virtual base, or 0 if page_count describes
 * more than X86_64_FRAMEBUFFER_MAX_BYTES above -- the caller's job to
 * treat 0 as "nothing mapped", not to retry with a smaller range.
 */
UQUAD x86_64_low_fb_init(UQUAD aligned_phys, UQUAD page_count);

#endif /* PC_X86_64_MEMORY_H */
