/*
 * processor.c - x86-64 processor/cache primitives
 *
 * Copyright (C) 2018-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "processor.h"
#include "biosext.h"

/*
 * bios.c's detect_cpu() (guarded by the same #if this arch shares with
 * ARM) already sets mcpu/mcpu_name; there is no ColdFire/m68k-style CPU
 * *variant* to configure here, and no SSE/FPU enable step either --
 * UEFI firmware already runs in long mode with CR0/CR4 set up for SSE,
 * and no floating point is used anywhere in this port yet (#329).
 */
void processor_init(void)
{
    detect_cpu();
}

/*
 * No-ops: x86 (unlike ARM/m68k) does not require explicit cache
 * maintenance around ordinary program loading or DMA -- the CPU's
 * instruction cache is coherent with the data cache/memory for code a
 * normal store just wrote (Intel SDM Vol 3A 11.6), and there is no
 * software-managed, non-coherent DMA path active on this arch yet (the
 * bios/acsi.c/floppy.c/scsi.c/amiga.c call sites are all Atari-hardware
 * only, already excluded via CONF_ATARI_HARDWARE; the virtio_*.c ones
 * need CONF_WITH_VIRTIO, not yet enabled for this machine).
 */
void instruction_cache_kludge(void *start, long size)
{
    (void)start; (void)size;
}

void flush_data_cache(void *start, long size)
{
    (void)start; (void)size;
}

void invalidate_data_cache(void *start, long size)
{
    (void)start; (void)size;
}

void invalidate_instruction_cache(void *start, long size)
{
    (void)start; (void)size;
}

/*
 * Do-nothing placeholder handler (tosvars.c's OSHEADER.reseth, mouse.c/
 * chardev.c's "no handler installed" default), matching ARM's own
 * bios/arch/arm/vectorsasm.S::just_rts -- this arch has no equivalent
 * vector-table file to put it in, so it lives here instead.
 */
void just_rts(void)
{
}
