/*
 * raspi_mmu_boot.c - the MMU bring-up of the higher-half kernel on the
 * Raspberry Pi
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_RPI
#error This file must only be compiled for raspberry PI targets
#endif

#if CONF_WITH_HIGHER_HALF

#include "portab.h"
#include "raspi_mmu.h"
#include "raspi_memmap.h"
#include "raspi_memory.h"
#include "processor_arm.h"
#include "asm.h"

#define MEGABYTE            0x100000UL
#define KERNEL_TABLE_ENTRIES    4096
#define HALF_ENTRIES        2048

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

/*
 * The scratch memory of the first mailbox call, in the RAM below the kernel
 * (which is loaded at 0x8000): the early stack in startup.S is just below it.
 * The firmware keeps its boot data (the ATAG list at 0x100) lower down.
 */
#define EARLY_BUFFER        0x7000UL

/* what is assumed when the VideoCore does not say how much RAM there is */
#define DEFAULT_RAM_SIZE    0x04000000UL

/* the uncached alias of the RAM, the way the VideoCore sees it */
#define BUS_UNCACHED        0xc0000000UL

/* the mailbox registers, as the hardware has them: the MMU is not on yet */
#define MAILBOX_BASE        (RASPI_IO_PA + 0xB880UL)
#define MAILBOX0_READ       (*(volatile ULONG *)(MAILBOX_BASE + 0x00))
#define MAILBOX0_STATUS     (*(volatile ULONG *)(MAILBOX_BASE + 0x18))
#define MAILBOX1_WRITE      (*(volatile ULONG *)(MAILBOX_BASE + 0x20))
#define MAILBOX1_STATUS     (*(volatile ULONG *)(MAILBOX_BASE + 0x38))
#define MAILBOX_STATUS_EMPTY    0x40000000UL
#define MAILBOX_STATUS_FULL     0x80000000UL
#define MAILBOX_PROP_OUT        8UL
#define PROPTAG_GET_ARM_MEMORY  0x00010005UL
#define MAILBOX_WAIT_LOOPS      20000000UL

/*
 * The layout of the megabyte at the top of the RAM that the MMU tables and
 * the buffers the VideoCore writes to are in (raspi_memory.h):
 *
 *   +0x0000  the table of the kernel half (TTBR1), 16 KiB
 *   +0x4000  the table of the lower half (TTBR0), 8 KiB
 *   +0x6000  the page table that makes the first megabyte of the window
 *            writable page by page, 1 KiB (CONF_WITH_MMU_TEXT_PROTECT)
 *   +0x8000  the coherent buffers
 */
#define KTABLE_OFFSET       0x0000UL
#define UTABLE_OFFSET       0x4000UL
#define L2TABLE_OFFSET      RASPI_L2_OFFSET

typedef struct TARMV6MMU_LEVEL1_SECTION_DESCRIPTOR section_t;

/* the memory types of a section */
enum { RASPI_MAP_RAM, RASPI_MAP_ORDERED, RASPI_MAP_DEVICE };

/*
 * How much RAM the ARM has, from the VideoCore.  The same call
 * raspi_prop_get_tags() makes later on, with nothing but literal constants:
 * this runs before the MMU is on, and neither it nor anything it calls may
 * use the address of a global variable (the kernel is linked in the window).
 * 0 if the VideoCore does not answer.
 */
static ULONG early_arm_memory(void)
{
    volatile ULONG *buf = (volatile ULONG *)EARLY_BUFFER;
    ULONG msg = BUS_UNCACHED | EARLY_BUFFER | MAILBOX_PROP_OUT;
    ULONG loops;
    ULONG size = 0;

    buf[0] = 8 * 4;                     /* size of the buffer */
    buf[1] = 0;                         /* request */
    buf[2] = PROPTAG_GET_ARM_MEMORY;
    buf[3] = 8;                         /* value buffer size */
    buf[4] = 0;                         /* request: no value */
    buf[5] = 0;
    buf[6] = 0;
    buf[7] = 0;                         /* end tag */
    data_sync_barrier();

    for (loops = 0; MAILBOX1_STATUS & MAILBOX_STATUS_FULL; loops++)
        if (loops > MAILBOX_WAIT_LOOPS)
            return 0;
    MAILBOX1_WRITE = msg;

    for (loops = 0; loops < MAILBOX_WAIT_LOOPS; loops++)
    {
        if (MAILBOX0_STATUS & MAILBOX_STATUS_EMPTY)
            continue;
        if (MAILBOX0_READ != msg)
            continue;
        data_mem_barrier();
        if (buf[1] == 0x80000000UL && (buf[4] & 0x80000000UL))
            size = buf[5] + buf[6];     /* base + size */
        break;
    }
    return size;
}

/* fill in a section entry mapping to physical address phys_base */
static void section_entry(section_t *entry, ULONG phys_base, int kind)
{
    entry->Value10 = 2;
    entry->XNBit   = (kind == RASPI_MAP_RAM) ? 0 : 1;
    entry->Domain  = 0;
    entry->IMPBit  = 0;
#if CONF_WITH_PROCESS_ASPACES
    entry->AP      = AP_SYSTEM_ACCESS;      /* the kernel half is not for user mode */
#else
    entry->AP      = AP_ALL_ACCESS;
#endif
    entry->APXBit  = APX_RW_ACCESS;
    entry->NGBit   = 0;
    entry->Value0  = 0;
    entry->SBZ     = 0;
    entry->Base    = ARMV6MMUL1SECTIONBASE(phys_base);
    entry->TEX     = 0;
    entry->SBit    = 1;

    switch (kind)
    {
    case RASPI_MAP_RAM:
        entry->BBit = 1;
        entry->CBit = 1;
        break;
    case RASPI_MAP_ORDERED:
        /* strongly ordered */
        entry->BBit = 0;
        entry->CBit = 0;
        break;
    default:
        /* shared device */
        entry->BBit = 1;
        entry->CBit = 0;
        break;
    }
}

static void clear_entries(volatile ULONG *p, ULONG n)
{
    ULONG i;

    /* what is not mapped faults: a stray pointer is caught, not followed.
     * (volatile: left alone, the compiler may turn this into NEON stores
     * or a call to memset(), and neither is available yet) */
    for (i = 0; i < n; i++)
        p[i] = 0;
}

ULONG raspi_mmu_bootstrap(void)
{
    ULONG ram_top, table, ram_sections, io_sections, i;
    section_t *ktable, *utable;
    ULONG control, aux_control;

    /* C has already written the stack and the mailbox buffer; do not discard
     * them if the firmware entered with the D-cache enabled. */
    flush_data_cache_all();

    ram_top = early_arm_memory();
    if (ram_top == 0)
        ram_top = DEFAULT_RAM_SIZE;
    if (ram_top > RASPI_WINDOW_SIZE)
        ram_top = RASPI_WINDOW_SIZE;    /* what the window holds */
    ram_top &= ~(MEGABYTE - 1);
    if (ram_top < 8 * MEGABYTE)
        ram_top = 8 * MEGABYTE;

    /* the topmost megabyte is for the tables and the coherent buffers */
    table = ram_top - MEGABYTE;
    ktable = (section_t *)(table + KTABLE_OFFSET);
    utable = (section_t *)(table + UTABLE_OFFSET);
    ram_sections = table / MEGABYTE;

    clear_entries((volatile ULONG *)(table + KTABLE_OFFSET), KERNEL_TABLE_ENTRIES);
    clear_entries((volatile ULONG *)(table + UTABLE_OFFSET), HALF_ENTRIES);

    /* the kernel half: the physical-memory window, the RAM from its first
     * byte, the page tables, and the rest of what the ARM sees below its
     * peripherals (the memory of the VideoCore: the framebuffer) */
    io_sections = RASPI_IO_PA / MEGABYTE;
    if (io_sections > RASPI_WINDOW_SIZE / MEGABYTE)
        io_sections = RASPI_WINDOW_SIZE / MEGABYTE;
    for (i = 0; i < io_sections; i++)
    {
        int kind = RASPI_MAP_DEVICE;

        if (i < ram_sections)
            kind = RASPI_MAP_RAM;
        else if (i == ram_sections)
            kind = RASPI_MAP_ORDERED;
        section_entry(&ktable[RASPI_WINDOW_VBASE / MEGABYTE + i], i * MEGABYTE, kind);
    }

    /* ... and the devices */
    for (i = 0; i < 16; i++)
        section_entry(&ktable[RASPI_DEV_VA(RASPI_IO_PA) / MEGABYTE + i],
                      RASPI_IO_PA + i * MEGABYTE, RASPI_MAP_DEVICE);
    if (RASPI_LOCAL_PA)
        section_entry(&ktable[RASPI_DEV_VA(RASPI_LOCAL_PA) / MEGABYTE],
                      RASPI_LOCAL_PA, RASPI_MAP_DEVICE);
#ifdef TARGET_RPI4
    /* the PCIe controller */
    section_entry(&ktable[RASPI_PCIE_REG_PA / MEGABYTE], RASPI_PCIE_REG_PA,
                  RASPI_MAP_DEVICE);
#endif

#if CONF_WITH_MMU_TEXT_PROTECT
    /* the first megabyte of the window in pages, so [_text, _etext) can be
     * made read-only (raspi_mmu_protect_range()); until then they are
     * writable, and map the same memory */
    {
        struct TARMV6MMU_LEVEL2_EXT_SMALL_PAGE_DESCRIPTOR *l2 =
            (struct TARMV6MMU_LEVEL2_EXT_SMALL_PAGE_DESCRIPTOR *)(table + L2TABLE_OFFSET);
        ULONG p;

        clear_entries((volatile ULONG *)(table + L2TABLE_OFFSET),
                      ARMV6MMU_LEVEL2_COARSE_PAGE_TABLE_SIZE / sizeof(ULONG));
        for (p = 0; p < ARMV6MMU_LEVEL2_COARSE_PAGE_TABLE_SIZE /
                        sizeof(struct TARMV6MMU_LEVEL2_EXT_SMALL_PAGE_DESCRIPTOR); p++)
        {
            l2[p].XNBit  = 0;
            l2[p].Value1 = 1;
            l2[p].BBit   = 1;
            l2[p].CBit   = 1;
            l2[p].AP     = AP_ALL_ACCESS;
            l2[p].TEX    = 0;
            l2[p].APXBit = APX_RW_ACCESS;
            l2[p].SBit   = 1;
            l2[p].NGBit  = 0;
            l2[p].Base   = ARMV6MMUL2SMALLPAGEBASE(SMALL_PAGE_SIZE * p);
        }

        /* the coarse page table descriptor (type 1, domain 0): the address of
         * the table, which is 1 KiB aligned, and the type in the low bits */
        *(volatile ULONG *)(table + KTABLE_OFFSET +
                            (RASPI_WINDOW_VBASE / MEGABYTE) * sizeof(ULONG))
            = (table + L2TABLE_OFFSET) | 1;
    }
#endif /* CONF_WITH_MMU_TEXT_PROTECT */

    /* the lower half: the same RAM at virtual 0, the pool the memory of the
     * programs is taken from (their addresses are not kernel addresses:
     * positive LONGs).  Where the RAM is at its physical address, so for
     * the code that is executing there right now, pre-MMU, and must keep
     * executing from immediately after the MMU-enable write below, until
     * startup.S's explicit jump into the window, and again on a reset. */
    for (i = 0; i < ram_sections; i++)
        section_entry(&utable[i], i * MEGABYTE, RASPI_MAP_RAM);
#if CONF_WITH_MMU_TEXT_PROTECT
    /* the first megabyte holds the kernel text in this alias too: it shares
     * the page table of the window, so raspi_mmu_protect_range() makes both
     * views read-only, and a write through the low address faults */
    *(volatile ULONG *)(table + UTABLE_OFFSET) = (table + L2TABLE_OFFSET) | 1;
#endif

    flush_data_cache_all();

    asm volatile ("mrc p15, 0, %0, c1, c0,  1" : "=r" (aux_control));
#ifdef TARGET_RPI1
    aux_control |= ARM_AUX_CONTROL_CACHE_SIZE;    /* restrict cache size (no page coloring) */
#else
    aux_control |= ARM_AUX_CONTROL_SMP;
#endif
    asm volatile ("mcr p15, 0, %0, c1, c0,  1" : : "r" (aux_control));

    /* TTBCR.N = 1: TTBR0 translates the lower 2 GiB, TTBR1 the upper 2 GiB
     * (doc/mmu-address-space.txt) */
    asm volatile ("mcr p15, 0, %0, c2, c0,  2" : : "r" (1));
    asm volatile ("mcr p15, 0, %0, c2, c0,  0" : : "r" ((ULONG)utable | TTBR_MODE));
    asm volatile ("mcr p15, 0, %0, c2, c0,  1" : : "r" ((ULONG)ktable | TTBR_MODE));

    /* domain 0 is a client of the tables */
    asm volatile ("mcr p15, 0, %0, c3, c0,  0" : : "r" (DOMAIN_CLIENT << 0));

    /* required if the MMU was previously enabled and not properly reset */
    asm volatile ("mcr p15, 0, %0, c7, c5,  0" : : "r" (0));    /* invalidate the I-cache */
    flush_branch_target_cache();
    asm volatile ("mcr p15, 0, %0, c8, c7,  0" : : "r" (0));    /* invalidate unified TLB */
    data_sync_barrier();
    flush_prefetch_buffer();

    asm volatile ("mrc p15, 0, %0, c1, c0,  0" : "=r" (control));
    control &= ~ARM_CONTROL_STRICT_ALIGNMENT;
#ifdef TARGET_RPI1
    control |= ARM_CONTROL_UNALIGNED_PERMITTED;
#endif
    control |= MMU_MODE;
    asm volatile ("mcr p15, 0, %0, c1, c0,  0" : : "r" (control) : "memory");

    /* the next instruction fetch has to see the new translation regime */
    flush_prefetch_buffer();

    return ram_top;
}

#endif /* CONF_WITH_HIGHER_HALF */
