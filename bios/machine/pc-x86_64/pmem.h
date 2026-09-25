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
 * EFI_MEMORY_TYPE comment in efi.h), except for the byte range
 * [reserved_base, reserved_end) -- this image's own load span (code, data,
 * bss, boot stack and page tables) plus the saved memory map buffer
 * itself, none of which the firmware's memory map has any way to know
 * this OS is still using.
 *
 * Only ever grows the free list (there is no matching "free a page" yet
 * -- nothing this early returns memory), so this is a one-shot bump
 * allocator, not a general page allocator: adequate for standing up
 * permanent kernel mappings and, later, the shared core's own memory
 * pools, not for a process's reclaimable memory.
 */
void x86_64_pmem_init(const void *efi_map, UQUAD map_size, UQUAD descriptor_size,
                       UQUAD reserved_base, UQUAD reserved_end);

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

#endif /* PC_X86_64_PMEM_H */
