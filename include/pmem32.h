/*
 * pmem32.h - the physical page allocator of the 32-bit MMU configurations
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef PMEM32_H
#define PMEM32_H

#include "portab.h"
#include "config.h"
#include <aspace_types.h>

#if CONF_WITH_USER_ASPACE && !defined(__x86_64__)

#define PMEM32_PAGE     4096UL
#define PMEM32_NONE     ((PADDR)0xffffffffUL)

/*
 * The pages that are not the kernel's own: after the kernel image and the
 * pool of the GEMDOS memory descriptors, up to the top of the RAM less what
 * the page tables of the boot take.  Everything a process owns (its memory,
 * its page tables, its kernel stack) and the kernel heap come from here.
 * Reached through the physical-memory window of the kernel half
 * (doc/mmu-address-space.txt): virtual = physical - RAM base + window base.
 *
 * Not interrupt-safe: process context only, as the heap (include/kheap.h).
 */
void pmem32_init(PADDR start, PADDR end);

/* More RAM above what pmem32_init() was given (the TT-RAM of an Atari, with
 * ST-RAM below it): the pages in between are not the allocator's. */
void pmem32_add(PADDR start, PADDR end);

/* The pages from `start` up are the ones pmem32_alloc() hands out first: the
 * TT-RAM, which is faster than the ST-RAM and which the DMA of the machine
 * does not reach.  The ST-RAM below it is used when those are all taken. */
void pmem32_prefer(PADDR start);

/* `pages` contiguous pages, the first at an address that is a multiple of
 * `align_pages` pages (a power of two); PMEM32_NONE if there are none.  The
 * pages are not cleared. */
PADDR pmem32_alloc(ULONG pages, ULONG align_pages);
/* The same, but only below the preferred pages: for what a DMA device reads or writes */
PADDR pmem32_alloc_low(ULONG pages, ULONG align_pages);
void pmem32_free(PADDR phys, ULONG pages);

/* TRUE iff the page containing phys is one the allocator manages and has handed out */
BOOL pmem32_is_allocated(PADDR phys);

ULONG pmem32_free_bytes(void);
ULONG pmem32_managed_bytes(void);
PADDR pmem32_highest_addr(void);        /* the end of what it manages */

/*
 * The window: the kernel's view of the physical memory, a kernel address for
 * every byte of the RAM.
 */
#if defined(MACHINE_VIRT_ARM)
#include "virt_memmap.h"
#define PMEM32_RAM_BASE     VIRT_RAM_BASE
#define PMEM32_WINDOW_BASE  VIRT_WINDOW_VBASE
#elif defined(MACHINE_RPI)
#include "raspi_memmap.h"
#define PMEM32_RAM_BASE     0UL
#define PMEM32_WINDOW_BASE  RASPI_WINDOW_VBASE
#elif defined(MACHINE_VIRT_M68K) || CONF_ATARI_HIGHER_HALF || CONF_AMIGA_HIGHER_HALF
#define PMEM32_RAM_BASE     0UL
#define PMEM32_WINDOW_BASE  0x80000000UL
#else
#error No physical-memory window for this machine
#endif

static __inline__ void *pmem32_virt(PADDR phys)
{
    return (void *)(uintptr_t)(phys - PMEM32_RAM_BASE + PMEM32_WINDOW_BASE);
}

static __inline__ PADDR pmem32_phys(const void *va)
{
    return (PADDR)(uintptr_t)va - PMEM32_WINDOW_BASE + PMEM32_RAM_BASE;
}

/* TRUE iff va is an address of the window that the allocator may have handed out */
static __inline__ BOOL pmem32_in_window(const void *va)
{
    PADDR a = (PADDR)(uintptr_t)va;

    return a >= PMEM32_WINDOW_BASE && a - PMEM32_WINDOW_BASE < 0x40000000UL;
}

#endif /* CONF_WITH_USER_ASPACE */

#endif /* PMEM32_H */
