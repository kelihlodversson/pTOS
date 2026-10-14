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

/*
 * How many megabytes at the start of the RAM are also mapped where the RAM
 * really is, for the code that is running there, at its physical address,
 * when the MMU is switched on.  startup.S drops them with
 * virt_mmu_drop_identity() after the jump into virtual addressing.
 */
#define IDENTITY_SECTIONS   16

/* is physical section 'i' (1 MB) of the board's devices, and if so where
 * are they in the virtual address space? */
static BOOL device_section(ULONG pa_section, ULONG *va_section)
{
    ULONG pa = pa_section * MEGABYTE;

    if (pa < VIRT_GIC_DIST_PA || pa >= VIRT_RAM_BASE)
        return FALSE;           /* flash, or the RAM itself */
    *va_section = VIRT_DEV_VA(pa) / MEGABYTE;
    return TRUE;
}

/* fill in a section entry mapping to physical address phys_base */
static void section_entry(struct TARMV6MMU_LEVEL1_SECTION_DESCRIPTOR *entry,
                          ULONG phys_base, BOOL is_ram)
{
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

void virt_mmu_bootstrap(ULONG ram_size_bytes, void *pagetable_phys)
{
    struct TARMV6MMU_LEVEL1_SECTION_DESCRIPTOR *table =
        (struct TARMV6MMU_LEVEL1_SECTION_DESCRIPTOR *) pagetable_phys;
    ULONG ram_sections = ram_size_bytes / MEGABYTE;
    ULONG ram_base_section = VIRT_RAM_BASE / MEGABYTE;
    ULONG i, va_section;
    ULONG control, aux_control;

    flush_data_cache_all();

    /* what is not mapped faults: a stray pointer is caught, not followed.
     * (volatile: left alone, the compiler turns this into NEON stores, and
     * the FPU is not enabled yet) */
    for (i = 0; i < PAGE_TABLE_ENTRIES; i++)
        *(volatile ULONG *)&table[i] = 0;

    /* the RAM, where the TOS system variables need it: at virtual 0 */
    for (i = 0; i < ram_sections; i++)
        section_entry(&table[i], i * MEGABYTE + VIRT_RAM_BASE, TRUE);

    /* the first part of the RAM where it physically is, for the code that
     * is executing there right now, pre-MMU, and must keep executing from
     * immediately after the MMU-enable write below, until startup.S's
     * explicit jump into virtual addressing.  It can overlap the RAM above
     * when that is more than 1 GB. */
    for (i = 0; i < IDENTITY_SECTIONS && i < ram_sections; i++)
        section_entry(&table[ram_base_section + i],
                      (ram_base_section + i) * MEGABYTE, TRUE);

    /* the devices, out of the RAM's way */
    for (i = 0; i < ram_base_section; i++)
        if (device_section(i, &va_section))
            section_entry(&table[va_section], i * MEGABYTE, FALSE);

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

/*
 * Back to one mapping of the RAM, at virtual 0, once the code is running
 * there: the sections at the physical address are unmapped, or are the
 * RAM's own mapping again when it reaches that far (> 1 GB).  'ram_size_bytes'
 * is the size given to virt_mmu_bootstrap().  The page table is in the RAM's
 * last megabyte, which is virtual ram_size_bytes - 1 MB.
 */
void virt_mmu_drop_identity(ULONG ram_size_bytes)
{
    struct TARMV6MMU_LEVEL1_SECTION_DESCRIPTOR *table =
        (struct TARMV6MMU_LEVEL1_SECTION_DESCRIPTOR *)
            (ram_size_bytes - MEGABYTE);
    ULONG ram_sections = ram_size_bytes / MEGABYTE;
    ULONG ram_base_section = VIRT_RAM_BASE / MEGABYTE;
    ULONG i;

    for (i = 0; i < IDENTITY_SECTIONS && i < ram_sections; i++)
    {
        ULONG section = ram_base_section + i;

        if (section < ram_sections)
            section_entry(&table[section], section * MEGABYTE + VIRT_RAM_BASE,
                          TRUE);
        else
            *(ULONG *)&table[section] = 0;
    }

    flush_data_cache_all();     /* the table walker must see the new entries */
    asm volatile ("mcr p15, 0, %0, c8, c7,  0" : : "r" (0));  /* invalidate unified TLB */
    flush_branch_target_cache();
    data_sync_barrier();
    flush_prefetch_buffer();
}

ULONG virt_to_phys(void *va)
{
    return (ULONG)va + VIRT_RAM_BASE;
}
