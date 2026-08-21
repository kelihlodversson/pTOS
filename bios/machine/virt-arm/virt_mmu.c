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
    ULONG max_window_sections = VIRT_GIC_DIST_BASE / MEGABYTE;
    ULONG ram_window_sections = ram_size_bytes / MEGABYTE;
    ULONG ram_base_section = VIRT_RAM_BASE / MEGABYTE;
    ULONG i;
    ULONG control, aux_control;

    if (ram_window_sections > max_window_sections)
        ram_window_sections = max_window_sections;

    invalidate_data_cache_all();

    for (i = 0; i < PAGE_TABLE_ENTRIES; i++)
    {
        struct TARMV6MMU_LEVEL1_SECTION_DESCRIPTOR *entry = &table[i];
        ULONG phys_base;
        /* Two disjoint aliases of the same physical RAM need Normal
         * (cacheable, executable) attributes: the low virtual window
         * used by the fixed-address sysvars, and the identity mapping
         * at VIRT_RAM_BASE -- which is where this function itself is
         * physically executing right now, pre-MMU, and must keep
         * executing from immediately after the MMU-enable write below,
         * until startup.S's explicit jump into virtual addressing. */
        BOOL low_window = (i < ram_window_sections);
        BOOL ram_identity = (i >= ram_base_section
                           && i < ram_base_section + ram_window_sections);
        BOOL is_ram = low_window || ram_identity;

        if (low_window)
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
            /* device: TEX=0, C=0, B=1 selects Shareable Device memory (not
             * cacheable) -- with TEX=0, C and B jointly index the memory
             * type table rather than acting as independent cacheable/
             * bufferable flags, so B=1 here is part of that selector, not
             * a separate "bufferable" attribute. Same encoding as the
             * "shared device" case in bios/machine/raspi/memory.c. */
            entry->BBit = 1;
            entry->CBit = 0;
            entry->TEX  = 0;
            entry->SBit = 1;
        }
    }

    flush_data_cache_all();

    asm volatile ("mrc p15, 0, %0, c1, c0,  1" : "=r" (aux_control));
    aux_control |= ARM_AUX_CONTROL_SMP;
    asm volatile ("mcr p15, 0, %0, c1, c0,  1" : : "r" (aux_control));

    asm volatile ("mcr p15, 0, %0, c2, c0,  2" : : "r" (0));
    asm volatile ("mcr p15, 0, %0, c2, c0,  0" : : "r" ((ULONG)table | TTBR_MODE));
    asm volatile ("mcr p15, 0, %0, c2, c0,  1" : : "r" ((ULONG)table | TTBR_MODE));
    asm volatile ("mcr p15, 0, %0, c3, c0,  0" : : "r" (DOMAIN_CLIENT << 0));

    flush_branch_target_cache();
    asm volatile ("mcr p15, 0, %0, c8, c7,  0" : : "r" (0));  /* invalidate unified TLB */
    data_sync_barrier();
    flush_prefetch_buffer();

    asm volatile ("mrc p15, 0, %0, c1, c0,  0" : "=r" (control));
    control &= ~ARM_CONTROL_STRICT_ALIGNMENT;
    control |= MMU_MODE;
    asm volatile ("mcr p15, 0, %0, c1, c0,  0" : : "r" (control) : "memory");

    /* Context-synchronize before relying on the new translation regime --
     * required by the architecture, and specifically what makes the very
     * next instruction fetch (back in startup.S) behave predictably. */
    flush_prefetch_buffer();
}

ULONG virt_to_phys(void *va)
{
    return (ULONG)va + VIRT_RAM_BASE;
}
