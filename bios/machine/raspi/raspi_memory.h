/*
 * raspi_screen.h Raspberry PI framebuffer support
 *
 * Copyright (C) 2013-2018 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef RASPI_MEMORY_H
#   define RASPI_MEMORY_H
#   ifdef MACHINE_RPI

void raspi_vcmem_init(void);

#if CONF_WITH_HIGHER_HALF
/*
 * The topmost megabyte of the RAM, below what the VideoCore keeps, belongs to
 * the MMU tables and the buffers the VideoCore writes to.  The start code
 * (raspi_mmu_bootstrap(), raspi_mmu_boot.c) builds the tables there before
 * the MMU is on:
 *
 *   +0x0000  the table of the kernel half (TTBR1), 16 KiB
 *   +0x4000  the table of the lower half (TTBR0), 8 KiB
 *   +0x6000  the page table of the first megabyte of the window, 1 KiB
 *            (CONF_WITH_MMU_TEXT_PROTECT)
 *   +0x8000  the coherent buffers, a page each
 */
#define RASPI_L2_OFFSET         0x6000UL
#define RASPI_COHERENT_OFFSET   0x8000UL

ULONG raspi_mmu_bootstrap(void);
UBYTE *virt_pool_start(UBYTE *kernel_end);
#endif

UBYTE* raspi_get_coherent_buffer(int tag);
#define COHERENT_TAG_MAILBOX 0

#if CONF_WITH_MMU_TEXT_PROTECT
/* mark [start, end) read-only at page granularity.
 * Both must fall within the first megabyte of RAM. See memory.c. */
void raspi_mmu_protect_range(ULONG start, ULONG end);
#endif



#   endif /* MACHINE_RPI */
#endif /* RASPI_MEMORY_H */
