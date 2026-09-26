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
 * EFI_MEMORY_TYPE comment in efi.h), except for two independent byte
 * ranges neither the firmware's memory map nor anything it reports has
 * any way to know this OS is still using: [reserved1_base, reserved1_end)
 * -- this image's own load span (code, data, bss, boot stack and page
 * tables) plus the saved memory map buffer itself -- and
 * [reserved2_base, reserved2_end) -- the low system-vector page
 * (pgtable.c's x86_64_map_low_vectors(), #349). Kept as two ranges rather
 * than the smallest single range spanning both: EFI typically loads this
 * image well above address 0, and collapsing the (usually large) gap
 * between them into one reserved block would falsely exclude a lot of
 * genuinely free memory. Pass the same range twice for both if a caller
 * ever has only one to reserve.
 *
 * Only ever grows the free list (there is no matching "free a page" yet
 * -- nothing this early returns memory), so this is a one-shot bump
 * allocator, not a general page allocator: adequate for standing up
 * permanent kernel mappings and, later, the shared core's own memory
 * pools, not for a process's reclaimable memory.
 */
void x86_64_pmem_init(const void *efi_map, UQUAD map_size, UQUAD descriptor_size,
                       UQUAD reserved1_base, UQUAD reserved1_end,
                       UQUAD reserved2_base, UQUAD reserved2_end);

/* Allocates `count` contiguous, page-aligned physical pages and returns
 * the physical address of the first one. Traps (see pmem.c) if the free
 * list cannot satisfy the request -- there is no failure return, since
 * every caller at this boot stage has no fallback if memory has actually
 * run out. */
UQUAD x86_64_pmem_alloc_pages(UQUAD count);

/* Total free bytes remaining across the whole free list (diagnostics). */
UQUAD x86_64_pmem_free_bytes(void);

/* The highest physical address (exclusive) x86_64_pmem_init() saw
 * anywhere in the EFI memory map, regardless of type -- the span the
 * physical-memory direct map (pgtable.c's x86_64_build_physmap()) needs
 * to cover to reach every byte of RAM the firmware ever reported, not
 * just the free subset. */
UQUAD x86_64_pmem_highest_addr(void);

/*
 * True iff [base, base+length) is entirely covered by is_ram_type()
 * descriptors in the EFI memory map x86_64_pmem_init() was given.
 * Unlike the free-page list, this walks the raw map itself, so it can
 * answer "is this real memory" for a range x86_64_pmem_init() was told
 * to reserve (and which therefore never appears as free) -- pgtable.c's
 * x86_64_map_low_vectors() uses this to confirm physical address 0 is
 * actually backed by RAM before zeroing it: real PC firmware can report
 * anything from VGA/option-ROM shadow MMIO to ACPI-reserved regions
 * starting well before the 2 MiB mark, and nothing about being asked to
 * simulate the m68k low system-vector area there makes that safe to
 * assume.
 */
int x86_64_pmem_region_is_ram(UQUAD base, UQUAD length);

#endif /* PC_X86_64_PMEM_H */
