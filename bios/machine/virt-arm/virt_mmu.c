/*
 * virt_mmu.c - static MMU translation table bring-up for QEMU's ARM
 * 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_VIRT_ARM
#error This file must only be compiled for the QEMU ARM virt target
#endif

#include "portab.h"
#include "raspi_mmu.h"
#include "processor_arm.h"
#include "asm.h"
#include "virt_memmap.h"
#include "virt_mmu.h"

#define MEGABYTE            0x100000UL
#define PAGE_TABLE_ENTRIES  4096

#define MMU_MODE    ( ARM_CONTROL_MMU                  \
                    | ARM_CONTROL_L1_CACHE              \
                    | ARM_CONTROL_L1_INSTRUCTION_CACHE  \
                    | ARM_CONTROL_BRANCH_PREDICTION)

#define TTBR_MODE   ( ARM_TTBR_INNER_WRITE_BACK  \
                    | ARM_TTBR_OUTER_WRITE_BACK)

void virt_mmu_bootstrap(ULONG ram_size_bytes, void *pagetable_phys)
{
    struct TARMV6MMU_LEVEL1_SECTION_DESCRIPTOR *table =
        (struct TARMV6MMU_LEVEL1_SECTION_DESCRIPTOR *) pagetable_phys;
    ULONG ram_window_sections = ram_size_bytes / MEGABYTE;
    ULONG i;
    ULONG control, aux_control;

    clean_data_cache();

    for (i = 0; i < PAGE_TABLE_ENTRIES; i++)
    {
        struct TARMV6MMU_LEVEL1_SECTION_DESCRIPTOR *entry = &table[i];
        ULONG phys_base;
        BOOL is_ram = (i < ram_window_sections);

        if (is_ram)
            phys_base = i * MEGABYTE + VIRT_RAM_BASE;
        else
            phys_base = i * MEGABYTE;

        entry->Value10 = 2;
        entry->XNBit   = is_ram ? 0 : 1;
        entry->Domain  = 0;
        entry->IMPBit  = 0;
        entry->AP      = AP_ALL_ACCESS;
        entry->APXBit  = APX_RW_ACCESS;
        entry->NGBit   = 0;
        entry->Value0  = 0;
        entry->SBZ     = 0;
        entry->Base    = ARMV6MMUL1SECTIONBASE(phys_base);

        if (is_ram)
        {
            entry->BBit = 1;
            entry->CBit = 1;
            entry->TEX  = 0;
            entry->SBit = 1;
        }
        else
        {
            /* device: not cacheable, not bufferable */
            entry->BBit = 1;
            entry->CBit = 0;
            entry->TEX  = 0;
            entry->SBit = 1;
        }
    }

    clean_data_cache();

    asm volatile ("mrc p15, 0, %0, c1, c0,  1" : "=r" (aux_control));
    aux_control |= ARM_AUX_CONTROL_SMP;
    asm volatile ("mcr p15, 0, %0, c1, c0,  1" : : "r" (aux_control));

    asm volatile ("mcr p15, 0, %0, c2, c0,  2" : : "r" (0));
    asm volatile ("mcr p15, 0, %0, c2, c0,  0" : : "r" ((ULONG)table | TTBR_MODE));
    asm volatile ("mcr p15, 0, %0, c2, c0,  1" : : "r" ((ULONG)table | TTBR_MODE));
    asm volatile ("mcr p15, 0, %0, c3, c0,  0" : : "r" (DOMAIN_CLIENT << 0));

    flush_data_cache_all();
    flush_branch_target_cache();
    asm volatile ("mcr p15, 0, %0, c8, c7,  0" : : "r" (0));  /* invalidate unified TLB */
    data_sync_barrier();
    flush_prefetch_buffer();

    asm volatile ("mrc p15, 0, %0, c1, c0,  0" : "=r" (control));
    control &= ~ARM_CONTROL_STRICT_ALIGNMENT;
    control |= MMU_MODE;
    asm volatile ("mcr p15, 0, %0, c1, c0,  0" : : "r" (control) : "memory");
}
