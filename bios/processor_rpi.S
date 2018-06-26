/*
 * processor.S - Detect and set CPU and FPU type
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



/* References */

        .globl  _processor_init
        .globl  _invalidate_instruction_cache
        .globl  _instruction_cache_kludge
        .globl  _flush_data_cache
        .globl  _invalidate_data_cache
        .globl  _mcpu
        .globl  _fputype

        .extern _longframe              // If not 0, use long stack frames
        .extern _setup_68030_pmmu



        .text

/*
 * void processor_init(void) - sets mcpu and fputype.
 */

_processor_init:
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

    // TODO: we should set up caching and MMU tables here
    bx lr

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

_invalidate_instruction_cache:
        mov ip, #0
        mcr p15, 0, ip, c7, c5,  0
        bx lr

_raspi_flush_prefetch_buffer:
#ifdef TARGET_RPI1
        mov ip, #0
        mcr p15, 0, ip, c7, c5,  4
#else
        isb
#endif
        bx lr

_raspi_flush_branch_target_cache:
        mov ip, #0
        mcr p15, 0, ip, c7, c5,  6
        bx lr


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

_flush_data_cache:
        bx lr   // Assuming write-through on arm


/*
 * void invalidate_data_cache(void *start, long length)
 *
 * invalidate data cache after data has been read with DMA
 *
 * for both modes of data cache (write_through and copyback),
 * the cache needs to be invalidated
 */
_invalidate_data_cache:
#ifdef TARGET_RPI1
        mov ip, #0
        mcr p15, 0, ip, c7, c6, 0
		mcr p15, 0, ip, c7, c10, 4
#else
    	push	{r4-r5, r7, r9-r11}
    	dmb					           // ensure ordering with previous memory accesses
    	mrc	p15, 1, r0, c0, c0, 1	   // read clidr
    	mov	r3, r0, lsr #23			   // move LoC into position
    	ands	r3, r3, #7 << 1		   // extract LoC*2 from clidr
    	beq	5f				           // if loc is 0, then no need to clean
    	mov	r10, #0				       // start clean at cache level 0
1:      add	r2, r10, r10, lsr #1	   // work out 3x current cache level
    	mov	r1, r0, lsr r2			   // extract cache type bits from clidr
    	and	r1, r1, #7			       // mask of the bits for current cache only
    	cmp	r1, #2				       // see what cache we have at this level
    	blt	4f				           // skip if no cache, or just i-cache
    	mrs	r9, cpsr			       // make cssr&csidr read atomic
    	cpsid	i
    	mcr	p15, 2, r10, c0, c0, 0	   // select current cache level in cssr
    	isb					           // isb to sych the new cssr&csidr
    	mrc	p15, 1, r1, c0, c0, 0	   // read the new csidr
    	msr	cpsr_c, r9
    	and	r2, r1, #7			       // extract the length of the cache lines
    	add	r2, r2, #4			       // add 4 (line length offset)
    	movw	r4, #0x3ff
    	ands	r4, r4, r1, lsr #3	   // find maximum number on the way size
    	clz	r5, r4				       // find bit position of way size increment
    	movw	r7, #0x7fff
    	ands	r7, r7, r1, lsr #13	   // extract max number of the index size
2:      mov	r9, r7				       // create working copy of max index
3:	    orr	r11, r10, r4, lsl r5	   // factor way and cache number into r11
    	orr	r11, r11, r9, lsl r2	   // factor index number into r11
    	mcr	p15, 0, r11, c7, c6, 2	   // invalidate by set/way
    	subs	r9, r9, #1			   // decrement the index
    	bge	3b
    	subs	r4, r4, #1			   // decrement the way
    	bge	2b
4:      add	r10, r10, #2			   // increment cache number
    	cmp	r3, r10
    	bgt	1b
5:      mov	r10, #0				       // swith back to cache level 0
    	mcr	p15, 2, r10, c0, c0, 0	   // select current cache level in cssr
    	dsb	st
    	isb
    	pop	{r4-r5, r7, r9-r11}
#endif
        bx lr


        SECTION_RODATA


// ==== Variables ============================================================

        .bss
        .balign 4

_mcpu:          .ds.l   1
_mcpu_subtype:  .ds.w   1
_fputype:       .ds.l   1
temp:           .ds.l   1


// ===========================================================================
// ==== End ==================================================================
// ===========================================================================
