/*
 * processor_arm.S - Detect and set CPU and FPU type
 *
 * Copyright (C) 2002-2017 The EmuTOS development team
 * Copyright (C) 1999, 2002 by Authors
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * Portions taken from linux/arch/arm/mm/cache-v7.S
 *
 *  Copyright (C) 2001 Deep Blue Solutions Ltd.
 *  Copyright (C) 2005 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "asmdefs.h"

        .text

/*
 * void processor_init(void) - sets mcpu and fputype.
 */

 .globl  _processor_init
_processor_init:
	push {lr}
    bl _detect_cpu

    // Initialize the VFP
    mrc p15, 0, ip, c1, c0, 2
    orr ip, ip, #0xF00000  // enable cp10 (single precision) and cp11 (double precision)
    mcr p15, 0, ip, c1, c0, 2

    // insert an instruction mem barrier
#ifdef TARGET_RPI1
    mov ip, #0
    mcr p15, 0, ip, c7, c5,  4
#else
    isb
#endif

    mov ip, #1 << 30    // VFP_FPEXC_EN
    fmxr fpexc, ip
    mov ip, #1 << 25    // VFP_FPSCR_DN	 default NaN mode
    fmxr fpscr, ip

	pop {pc}

/*
 * void instruction_cache_kludge(void *start, long length)
 *
 * TOS compatibility: invalidate the instruction cache
 *
 * this provides backward compatibility in case some foolish person
 * reads code from an I/O device and branches to it directly; this
 * would have been legal on STs and STes.
 *
 * we don't do that on ColdFire or ARM, because executables on these systems
 * are brand new and supposed to be aware of cache issues.
 */

 .globl  _instruction_cache_kludge
_instruction_cache_kludge:
        bx lr
/*
 * void invalidate_instruction_cache(void *start, long length)
 * First, the data cache is flushed to push changes into the RAM.
 * Then the instruction cache is invalidated for the specified zone.
 *
 * We're lazy here and invalidate all the cache. A real implementation
 * would invalidate only the needed pages using several cinvp ic,(a0).
 * It is not worth the trouble for EmuTOS right now.
 */

.globl  _invalidate_instruction_cache
_invalidate_instruction_cache:
        mov ip, #0
        mcr p15, 0, ip, c7, c5,  0
        bx lr

.globl _flush_prefetch_buffer
_flush_prefetch_buffer:
#ifdef TARGET_RPI1
        mov ip, #0
        mcr p15, 0, ip, c7, c5,  4
#else
        isb
#endif
        bx lr

.globl _flush_branch_target_cache
_flush_branch_target_cache:
        mov ip, #0
        mcr p15, 0, ip, c7, c5,  6
        bx lr


#ifdef TARGET_RPI1
/*
 * void flush_data_cache(void *start, long length)
 *
 * flush data cache before writing data with DMA
 *
 * the actions required depend on the mode of data cache:
 *   write-through:
 *     no action is necessary
 *   copyback:
 *     we must push the data cache (the backing memory may be stale)
 */

 .globl  _flush_data_cache
_flush_data_cache:
        bx lr   // Assuming write-through on arm

 .globl  _invalidate_data_cache_all
_invalidate_data_cache_all:
        mov ip, #0
        mcr p15, 0, ip, c7, c6, 0
		mcr p15, 0, ip, c7, c10, 4
        bx lr

.global _clean_data_cache
_clean_data_cache:
        mov ip, #0
        mcr p15, 0, ip, c7, c10, 0
		mcr p15, 0, ip, c7, c10, 4
        bx lr
#else
// RPI2+ definition is in cache_armv7.c
#endif

// ===========================================================================
// ==== End ==================================================================
// ===========================================================================
