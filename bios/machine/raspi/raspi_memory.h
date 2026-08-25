/*
 * raspi_memory.h - Raspberry Pi memory setup (MMU, RAM detection)
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
UBYTE* raspi_get_coherent_buffer(int tag);
#define COHERENT_TAG_MAILBOX 0

/* Top of ARM-visible RAM as reported by firmware, set by
 * raspi_vcmem_init(). Not the same as phystop (include/tosvars.h):
 * phystop marks where the topmost reserved megabyte (page table, cache
 * coherent buffers) BEGINS, not where RAM ENDS -- callers that need the
 * true top of RAM (e.g. to check a fixed physical address doesn't
 * overlap ANY detected RAM, reserved or not) must use this, not
 * phystop. See memory.c. */
extern ULONG raspi_top_of_ram;

#if CONF_WITH_MMU_TEXT_PROTECT
/* mark [start, end) read-only at page granularity.
 * Both must fall within the first megabyte of RAM. See memory.c. */
void raspi_mmu_protect_range(ULONG start, ULONG end);
#endif



#   endif /* MACHINE_RPI */
#endif /* RASPI_MEMORY_H */
