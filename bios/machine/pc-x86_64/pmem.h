/*
 * pmem.h - x86-64 early physical-memory allocator
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef PC_X86_64_PMEM_H
#define PC_X86_64_PMEM_H

#include "portab.h"

#define X86_64_PAGE_SIZE 0x1000ULL

/*
 * Upper bound on the raw EFI memory map startup.c keeps a copy of after
 * ExitBootServices() (the buffer GetMemoryMap() itself filled in has no
 * promise of staying valid past that point). Shared with pmem.c, whose
 * MAX_REGIONS is derived from this rather than guessed independently --
 * an independent guess can't be proven never to overflow against a
 * buffer sized independently of it (#348 review: a 16 KiB buffer and a
 * MAX_REGIONS both picked by eyeballing one observed QEMU/OVMF map could
 * still be defeated by a more fragmented real one). 64 KiB comfortably
 * exceeds anything observed from real firmware (that same QEMU/OVMF boot:
 * 128 descriptors, ~5 KiB) with well over an order of magnitude of
 * headroom, and is cheap: this only reserves bss, not disk image size.
 */
#define X86_64_EFI_MAP_BYTES (64 * 1024)

/*
 * Builds the free-page list from the EFI memory map GetMemoryMap() filled
 * in (see startup.c, which saves a copy of it before ExitBootServices()
 * -- the pointer GetMemoryMap() itself returned lives in memory that is
 * only guaranteed valid while boot services are still active).
 *
 * map_size/descriptor_size are exactly what GetMemoryMap() returned
 * alongside it. Every descriptor of type EfiConventionalMemory,
 * EfiBootServicesCode or EfiBootServicesData becomes free memory (see the
 * EFI_MEMORY_TYPE comment in efi.h), except for three independent byte
 * ranges neither the firmware's memory map nor anything it reports has
 * any way to know this OS is still using: [reserved1_base, reserved1_end)
 * -- this image's own load span (code, data, bss, boot stack and page
 * tables) plus the saved memory map buffer itself -- [reserved2_base,
 * reserved2_end) -- a conservative margin below the caller's own load
 * span, historically the low system-vector page pgtable.c's
 * x86_64_map_low_vectors() identity-mapped directly; that function now
 * maps virtual address 0 to an ordinary allocated page instead (see its
 * own comment on why: physical address 0 is not guaranteed to be usable
 * RAM on real PC firmware), so nothing actually requires this range to
 * stay unallocated any more, but leaving it reserved costs nothing and
 * avoids handing out low addresses some other firmware quirk might still
 * treat specially -- and [reserved3_base, reserved3_end) -- the EFI GOP
 * framebuffer's own aperture (bios/machine/pc-x86_64/gop.c's
 * x86_64_gop_reserved_range(), read before this call), which this
 * allocator would otherwise be free to hand out to something else: GOP's
 * memory map descriptor for it is ordinary EfiConventionalMemory or
 * EfiBootServices{Code,Data} on real firmware (nothing marks it
 * "reserved" on this OS's behalf), so without this exclusion a later
 * TPA/kdata/physmap allocation could receive the exact same physical
 * pages the framebuffer is mapped to, silently corrupting the display or
 * whatever the allocator handed that range out for (Copilot review, PR
 * #373). Pass (0, 0) for any range a caller has nothing to reserve for
 * (an empty range excludes nothing, see add_free_region_excluding()) --
 * gop.c's own accessor already does this when no GOP framebuffer was
 * found. Kept as three independent ranges rather than the smallest single
 * range spanning all of them: they are typically far apart, and
 * collapsing the gaps between them into one reserved block would falsely
 * exclude a lot of genuinely free memory.
 *
 * x86_64_pmem_alloc_pages()/_below() are the panicking, permanent
 * allocations for boot-time mappings; reclaimable memory (process address
 * spaces, the kernel heap) uses x86_64_pmem_try_alloc_pages() and
 * x86_64_pmem_free_pages() below.
 */
void x86_64_pmem_init(const void *efi_map, UQUAD map_size, UQUAD descriptor_size,
                       UQUAD reserved1_base, UQUAD reserved1_end,
                       UQUAD reserved2_base, UQUAD reserved2_end,
                       UQUAD reserved3_base, UQUAD reserved3_end);

/* Allocates `count` contiguous, page-aligned physical pages and returns
 * the physical address of the first one. Traps (see pmem.c) if the free
 * list cannot satisfy the request -- there is no failure return, since
 * every caller at this boot stage has no fallback if memory has actually
 * run out. */
UQUAD x86_64_pmem_alloc_pages(UQUAD count);

/*
 * Same as x86_64_pmem_alloc_pages(), but only ever returns memory whose
 * entire allocated span (base through base + count*X86_64_PAGE_SIZE) is
 * below limit -- for the handful of allocations that must be
 * dereferenceable through a genuinely 32-bit-wide field (a GEMDOS PD's
 * p_tbase/p_hitpa, and anything else #351 tracks): x86_64_pmem_alloc_pages()
 * hands out whichever free region a first-fit scan reaches first,
 * regardless of address, which on a system with more than 4 GiB of RAM
 * could just as easily be well above it. Scans the same free-region list
 * for the first region whose base and end both fall under limit, rather
 * than allocating normally and checking the result afterward (which
 * would have no way to give the memory back if it turned out too high).
 * Traps (see pmem.c) if no region under limit can satisfy the request.
 */
UQUAD x86_64_pmem_alloc_pages_below(UQUAD count, UQUAD limit);

/*
 * Fallible, reclaimable counterparts of the allocators above, for callers
 * that own what they allocate and can report failure (process address
 * spaces and the kernel heap; the panicking forms stay for boot-time
 * permanent mappings).
 *
 * x86_64_pmem_try_alloc_pages() returns X86_64_PMEM_NONE when `count`
 * contiguous pages cannot be found.  limit is a PHYSICAL constraint -- the
 * allocated span must end at or below it -- and is deliberately unrelated
 * to any virtual-address limit: pass 0 for "anywhere" (the common case,
 * since the MMU can map any physical page below a 4 GiB virtual address),
 * or a real device limit for DMA buffers.
 *
 * x86_64_pmem_free_pages() returns pages to the free list (coalescing with
 * free neighbours).  It returns FALSE and changes nothing if the range is
 * misaligned, beyond RAM, or overlaps memory that is already free, so a
 * double free is detected and cannot corrupt the list.  Pages are NOT
 * zeroed on free: whoever allocates them next decides (see procmem.h).
 */
#define X86_64_PMEM_NONE (~0ULL)

UQUAD x86_64_pmem_try_alloc_pages(UQUAD count, UQUAD limit);
BOOL x86_64_pmem_free_pages(UQUAD base, UQUAD count);

/* Diagnostics for leak tests: pages dropped because the free-region list
 * was full, and rejected (double/foreign) frees.  Both stay 0 normally. */
UQUAD x86_64_pmem_lost_pages(void);
UQUAD x86_64_pmem_bad_frees(void);

/* Failure injection for the boot self-test: the n-th (1 = next)
 * x86_64_pmem_try_alloc_pages() from now fails.  n <= 0 disarms it. */
void x86_64_pmem_test_fail_after(LONG n);

/* Total free bytes remaining across the whole free list (diagnostics). */
UQUAD x86_64_pmem_free_bytes(void);

/* The highest physical address (exclusive) x86_64_pmem_init() saw
 * anywhere in the EFI memory map, regardless of type -- the span the
 * physical-memory direct map (pgtable.c's x86_64_build_physmap()) needs
 * to cover to reach every byte of RAM the firmware ever reported, not
 * just the free subset. */
UQUAD x86_64_pmem_highest_addr(void);

#endif /* PC_X86_64_PMEM_H */
