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
#include "kprint.h"

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
 * really is.  The code is running there, at its physical address, when the
 * MMU is switched on, and a software reset comes back to it to switch the
 * MMU off again, so the mapping stays.  The RAM is limited to what fits
 * below VIRT_RAM_BASE (VIRT_MAX_RAM_SIZE), so it never overlaps the RAM's
 * own mapping at virtual 0.
 */
#define IDENTITY_SECTIONS   16

/*
 * The two tables.  TTBCR.N = 1 makes TTBR0 translate the lower 2 GiB and
 * TTBR1 the upper 2 GiB (doc/mmu-address-space.txt): TTBR1 holds the kernel
 * for the whole uptime, and TTBR0 is what a process's own table is later.
 */
#define KERNEL_TABLE_BYTES  16384   /* 4096 entries, 8 KiB of them used */
#define USER_TABLE_BYTES    8192    /* 2048 entries */
#define HALF_ENTRIES        2048

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
    struct TARMV6MMU_LEVEL1_SECTION_DESCRIPTOR *ktable =
        (struct TARMV6MMU_LEVEL1_SECTION_DESCRIPTOR *) pagetable_phys;
    struct TARMV6MMU_LEVEL1_SECTION_DESCRIPTOR *utable =
        (struct TARMV6MMU_LEVEL1_SECTION_DESCRIPTOR *)
            ((ULONG)pagetable_phys + KERNEL_TABLE_BYTES);
    ULONG ram_sections = ram_size_bytes / MEGABYTE;
    ULONG ram_base_section = VIRT_RAM_BASE / MEGABYTE;
    ULONG window_section = VIRT_WINDOW_VBASE / MEGABYTE;
    ULONG i, va_section;
    ULONG control, aux_control;

    flush_data_cache_all();

    /* what is not mapped faults: a stray pointer is caught, not followed.
     * (volatile: left alone, the compiler turns this into NEON stores, and
     * the FPU is not enabled yet) */
    for (i = 0; i < PAGE_TABLE_ENTRIES; i++)
        *(volatile ULONG_ALIAS *)&ktable[i] = 0;
    for (i = 0; i < HALF_ENTRIES; i++)
        *(volatile ULONG_ALIAS *)&utable[i] = 0;

    /* the kernel half (TTBR1): the RAM from its first byte, in the window the
     * kernel is linked in */
    for (i = 0; i < ram_sections; i++)
        section_entry(&ktable[window_section + i], i * MEGABYTE + VIRT_RAM_BASE, TRUE);

    /* ... and the devices, out of the window's way */
    for (i = 0; i < ram_base_section; i++)
        if (device_section(i, &va_section))
            section_entry(&ktable[va_section], i * MEGABYTE, FALSE);

    /* the lower half (TTBR0): the RAM at virtual 0 as well, the pool the
     * memory of the programs comes from (their addresses are not kernel
     * addresses: positive LONGs) */
    for (i = 0; i < ram_sections; i++)
        section_entry(&utable[i], i * MEGABYTE + VIRT_RAM_BASE, TRUE);

    /* the first part of the RAM where it physically is, for the code that
     * is executing there right now, pre-MMU, and must keep executing from
     * immediately after the MMU-enable write below, until startup.S's
     * explicit jump into the window, and again on a reset. */
    for (i = 0; i < IDENTITY_SECTIONS && i < ram_sections; i++)
        section_entry(&utable[ram_base_section + i],
                      (ram_base_section + i) * MEGABYTE, TRUE);

    flush_data_cache_all();

    asm volatile ("mrc p15, 0, %0, c1, c0,  1" : "=r" (aux_control));
    aux_control |= ARM_AUX_CONTROL_SMP;
    asm volatile ("mcr p15, 0, %0, c1, c0,  1" : : "r" (aux_control));

    asm volatile ("mcr p15, 0, %0, c2, c0,  2" : : "r" (1));  /* TTBCR.N = 1 */
    asm volatile ("mcr p15, 0, %0, c2, c0,  0" : : "r" ((ULONG)utable | TTBR_MODE));
    asm volatile ("mcr p15, 0, %0, c2, c0,  1" : : "r" ((ULONG)ktable | TTBR_MODE));
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
 * The physical address of a RAM address the CPU uses: in the window (the
 * kernel's own memory) or in the pool at virtual 0 (the memory of the
 * programs).  Not meaningful for anything else (peripherals, unmapped).
 */
ULONG virt_to_phys(const void *va)
{
    ULONG a = (ULONG)va;

    if (a >= VIRT_WINDOW_VBASE && a - VIRT_WINDOW_VBASE < VIRT_MAX_RAM_SIZE)
        return a - VIRT_WINDOW_VBASE + VIRT_RAM_BASE;
    if (a < VIRT_MAX_RAM_SIZE)
        return a + VIRT_RAM_BASE;
    KDEBUG(("virt_to_phys(%p): not RAM\n", va));
    return 0;
}

/*
 * The pool address of the first byte after the kernel: the memory after the
 * kernel image is the pool, reached through the lower half.
 */
UBYTE *virt_pool_start(UBYTE *kernel_end)
{
    return kernel_end - VIRT_WINDOW_VBASE;
}
