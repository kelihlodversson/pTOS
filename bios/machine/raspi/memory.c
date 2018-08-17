/*
 * raspi_screen.h Raspberry PI framebuffer support
 *
 * Copyright (C) 2013-2018 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_RPI
#error This file must only be compiled for raspberry PI targets
#endif

#include "portab.h"
#include "raspi_io.h"
#include "raspi_int.h"
#include "raspi_mbox.h"
#include "raspi_mmu.h"
#include "raspi_memory.h"
#include "tosvars.h"
#include "asm.h"
#include "processor.h"
#include "string.h"
#include "biosext.h"

#define MEGABYTE    0x100000
#ifdef TARGET_RPI1
#define MMU_MODE    ( ARM_CONTROL_MMU                   \
                    | ARM_CONTROL_L1_CACHE              \
                    | ARM_CONTROL_L1_INSTRUCTION_CACHE  \
                    | ARM_CONTROL_BRANCH_PREDICTION     \
                    | ARM_CONTROL_EXTENDED_PAGE_TABLE)

#define TTBR_MODE    ( ARM_TTBR_INNER_CACHEABLE         \
                     | ARM_TTBR_OUTER_NON_CACHEABLE)
#else
#define MMU_MODE    ( ARM_CONTROL_MMU                   \
                    | ARM_CONTROL_L1_CACHE              \
                    | ARM_CONTROL_L1_INSTRUCTION_CACHE  \
                    | ARM_CONTROL_BRANCH_PREDICTION)

#define TTBR_MODE   ( ARM_TTBR_INNER_WRITE_BACK        \
                    | ARM_TTBR_OUTER_WRITE_BACK)
#endif
#define TTBCR_SPLIT    0
#define PAGE_TABLE0_ENTRIES    4096
#define PAGE_TABLE0_SIZE       (PAGE_TABLE0_ENTRIES* sizeof(struct TARMV6MMU_LEVEL1_SECTION_DESCRIPTOR))

static void init_mmu(ULONG memory_size);

extern char sysvars_start[];
extern char sysvars_end[];

static UBYTE* coherent_buffer;
struct TARMV6MMU_LEVEL1_SECTION_DESCRIPTOR* raspi_page_table0;

UBYTE* raspi_get_coherent_buffer(int tag)
{
    return coherent_buffer + (tag * 4096);
}

extern long start_in_hyp;
void raspi_vcmem_init(void)
{
    /* Preserve the contents of start_in_hyp across clearing the bss segment */
    long start_in_hyp_sv = start_in_hyp;

    /* Clear the sysvars */
    bzero(sysvars_start, sysvars_end - sysvars_start);

    /*
    * Clear the BSS segment.
    * Our stack is explicitly set outside the BSS, so this is safe:
    * bzero() will be able to return.
    */
    bzero(_bss, _ebss - _bss);
    start_in_hyp = start_in_hyp_sv;

    // Temporary set coherent_buffer base to aligned RAM before we know the total size
    coherent_buffer  = (UBYTE*)(((ULONG)_end_os_stram + (5*MEGABYTE)) & ~(MEGABYTE-1));
    struct
    {
        prop_tag_2u32_t    get_arm_memory;
        prop_tag_2u32_t    get_vc_memory;
    } init_tags;

    init_tags.get_arm_memory.tag.tag_id = PROPTAG_GET_ARM_MEMORY;
    init_tags.get_arm_memory.tag.value_buf_size = 8;
    init_tags.get_arm_memory.tag.value_length = 8;
    init_tags.get_vc_memory.tag.tag_id = PROPTAG_GET_VC_MEMORY;
    init_tags.get_vc_memory.tag.value_buf_size = 8;
    init_tags.get_vc_memory.tag.value_length = 8;
    raspi_prop_get_tags(&init_tags, sizeof(init_tags));

    ULONG top_of_ram = (init_tags.get_arm_memory.value1 + init_tags.get_arm_memory.value2);

    /* Reserve the topmost megabyte for page tables and cache coherent buffers */
    phystop = (UBYTE *)((top_of_ram - MEGABYTE) & ~(MEGABYTE-1));

    raspi_page_table0 = (struct TARMV6MMU_LEVEL1_SECTION_DESCRIPTOR*)phystop;
    coherent_buffer = phystop + PAGE_TABLE0_SIZE;

    /* Now the bss has been cleared, we can enable the MMU and caches */
    init_mmu((ULONG)phystop);
}

static void init_mmu(ULONG memory_size)
{
    unsigned i;
    clean_data_cache ();

    for (i = 0; i < PAGE_TABLE0_ENTRIES; i++)
    {
        ULONG base_address = MEGABYTE * i;

        struct TARMV6MMU_LEVEL1_SECTION_DESCRIPTOR *entry = &raspi_page_table0[i];

        // shared device
        entry->Value10 = 2;
        entry->BBit    = 1;
        entry->CBit    = 1;
        entry->XNBit   = 0;
        entry->Domain  = 0;
        entry->IMPBit  = 0;
        entry->AP      = AP_ALL_ACCESS;
        entry->TEX     = 0;
        entry->APXBit  = APX_RW_ACCESS;
        entry->SBit    = 1;
        entry->NGBit   = 0;
        entry->Value0  = 0;
        entry->SBZ     = 0;
        entry->Base    = ARMV6MMUL1SECTIONBASE (base_address);

        // We actually have a megabyte of memory above phystop we use
        // for the page table and cache coherent buffers:
        if (base_address == memory_size)
        {
            // strongly ordered
            entry->BBit  = 0;
            entry->CBit  = 0;
            entry->TEX   = 0;
            entry->SBit  = 1;
        }
        else
        if (base_address > memory_size)
        {
            // shared device
            entry->XNBit = 1;
            entry->BBit  = 1;
            entry->CBit  = 0;
            entry->TEX   = 0;
            entry->SBit  = 1;
        }
    }

    clean_data_cache ();

    ULONG aux_control;
    asm volatile ("mrc p15, 0, %0, c1, c0,  1" : "=r" (aux_control));
#ifdef TARGET_RPI1
    aux_control |= ARM_AUX_CONTROL_CACHE_SIZE;    // restrict cache size (no page coloring)
#else
    aux_control |= ARM_AUX_CONTROL_SMP;
#endif
    asm volatile ("mcr p15, 0, %0, c1, c0,  1" : : "r" (aux_control));

    ULONG TLB_type;
    asm volatile ("mrc p15, 0, %0, c0, c0,  3" : "=r" (TLB_type));

    // set TTB control
    asm volatile ("mcr p15, 0, %0, c2, c0,  2" : : "r" (0));

    // set TTBR0
    asm volatile ("mcr p15, 0, %0, c2, c0,  0" : : "r" ((ULONG)raspi_page_table0 | TTBR_MODE));

    // set TTBR1
    asm volatile ("mcr p15, 0, %0, c2, c0,  1" : : "r" ((ULONG)raspi_page_table0 | TTBR_MODE));

    // set Domain Access Control register (Domain 0 and 1 to client)
    asm volatile ("mcr p15, 0, %0, c3, c0,  0" : : "r" (  DOMAIN_CLIENT << 0
                                                        | DOMAIN_CLIENT << 2));

#ifndef TARGET_RPI1
    flush_data_cache_all();
#endif

    // required if MMU was previously enabled and not properly reset
    invalidate_instruction_cache(0, memory_size);
    flush_branch_target_cache();
    asm volatile ("mcr p15, 0, %0, c8, c7,  0" : : "r" (0));    // invalidate unified TLB
    data_sync_barrier();
    flush_prefetch_buffer();

    // enable MMU
    ULONG control;
    asm volatile ("mrc p15, 0, %0, c1, c0,  0" : "=r" (control));
    control &= ~ARM_CONTROL_STRICT_ALIGNMENT;
#ifdef TARGET_RPI1
    control |= ARM_CONTROL_UNALIGNED_PERMITTED;
#endif
    control |= MMU_MODE;
    asm volatile ("mcr p15, 0, %0, c1, c0,  0" : : "r" (control) : "memory");
}


#ifdef TARGET_RPI1
//
// Cache maintenance operations for ARMv6
//
// NOTE: The following functions should hold all variables in CPU registers. Currently this will be
//	 ensured using maximum optimation (see bios/processor.h).
//
//	 The following numbers can be determined (dynamically) using CTR.
//	 As long we use the ARM1176JZF-S implementation in the BCM2835 these static values will work:
//

#define DATA_CACHE_LINE_LENGTH		32

void invalidate_data_cache (void *start, long length)
{
	length += DATA_CACHE_LINE_LENGTH;

	while (1)
	{
		asm volatile ("mcr p15, 0, %0, c7, c14,  1" : : "r" ((ULONG)start) : "memory");

		if (length < DATA_CACHE_LINE_LENGTH)
		{
			break;
		}

		start += DATA_CACHE_LINE_LENGTH;
		length  -= DATA_CACHE_LINE_LENGTH;
	}

	data_sync_barrier ();
}
#else
// The RPI 2+ implementation is in cache_armv7.S
#endif
