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

#define POOL_BYTES (2 * 1024 * 1024)

UBYTE _end_os_stram[POOL_BYTES] __attribute__((aligned(16)));

void pc_x86_64_memory_init(void)
{
    phystop = &_end_os_stram[POOL_BYTES];
}

