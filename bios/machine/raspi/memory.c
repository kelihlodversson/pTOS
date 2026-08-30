/*
 * raspi_screen.h Raspberry PI framebuffer support
 *
 * Copyright (C) 2013-2018 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/* #define ENABLE_KDEBUG */

#include "config.h"
#ifndef MACHINE_RPI
#error This file must only be compiled for raspberry PI targets
#endif

#include "portab.h"
#include "raspi_io.h"
#include "raspi_int.h"
#include "raspi_mbox.h"
#include "raspi_mmu.h"
#include "raspi_lpae.h"
#include "raspi_memory.h"
#include "tosvars.h"
#include "asm.h"
#include "processor.h"
#include "string.h"
#include "biosext.h"
#include "kprint.h"
#include "bios.h"

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

#if CONF_WITH_MMU_TEXT_PROTECT
/* page-granularity table covering section 0 (0x0-0xFFFFF), so ranges
 * within the first megabyte can be marked read-only individually.
 * Populated by init_mmu(), narrowed later (after boot-time
 * initializers have finished writing their targets) by
 * raspi_mmu_protect_range(). */
static struct TARMV6MMU_LEVEL2_EXT_SMALL_PAGE_DESCRIPTOR
    text_protect_l2[ARMV6MMU_LEVEL2_COARSE_PAGE_TABLE_SIZE / sizeof(struct TARMV6MMU_LEVEL2_EXT_SMALL_PAGE_DESCRIPTOR)]
    __attribute__((aligned(ARMV6MMU_LEVEL2_COARSE_PAGE_TABLE_SIZE)));

/*
 * mark [start, end) read-only at page granularity. Both must fall
 * within section 0 (the first megabyte) - the only section init_mmu()
 * has broken out into a page table. Rounds start down / end up to
 * page boundaries, so it may protect a little more than asked.
 */
void raspi_mmu_protect_range(ULONG start, ULONG end)
{
    ULONG protect_start = start & ~(SMALL_PAGE_SIZE - 1);
    ULONG protect_end   = (end + SMALL_PAGE_SIZE - 1) & ~(SMALL_PAGE_SIZE - 1);
    unsigned p;

    /* text_protect_l2[] only has entries for section 0 (the first
     * megabyte); silently protecting just the subset that fits would
     * leave the rest of [start,end) writable while looking enabled -
     * defeating the point of a feature whose whole job is to fail loudly. */
    if (protect_start >= SECTION_SIZE || protect_end > SECTION_SIZE)
        panic("raspi_mmu_protect_range(%p,%p): outside the first megabyte\n",
              (void*)start, (void*)end);

    for (p = 0; p < ARRAY_SIZE(text_protect_l2); p++)
    {
        ULONG page_addr = SMALL_PAGE_SIZE * p;
        if (page_addr >= protect_start && page_addr < protect_end)
            text_protect_l2[p].APXBit = APX_RO_ACCESS;
    }

    /* the updated descriptors just went through the D-cache like any other
     * write; without cleaning them out, the MMU's table walk can still see
     * the old (writable) descriptor in RAM and the protection would not
     * reliably take effect. */
    flush_data_cache_all();
    asm volatile ("mcr p15, 0, %0, c8, c7, 0" : : "r" (0));   /* invalidate unified TLB */
    data_sync_barrier();
    flush_prefetch_buffer();

    KDEBUG(("mmu: write-protecting [%p,%p)\n", (void*)protect_start, (void*)protect_end));
}
#endif /* CONF_WITH_MMU_TEXT_PROTECT */

extern char sysvars_start[];
extern char sysvars_end[];

static UBYTE* coherent_buffer;
struct TARMV6MMU_LEVEL1_SECTION_DESCRIPTOR* raspi_page_table0;
ULONG raspi_top_of_ram;

UBYTE* raspi_get_coherent_buffer(int tag)
{
    return coherent_buffer + (tag * 4096);
}

extern long start_in_hyp;
void raspi_vcmem_init(void)
{
    /* Preserve the contents of start_in_hyp across clearing the bss segment */
    long start_in_hyp_sv = start_in_hyp;

    /* Likewise for the registers startup.S saved on entry */
    arm_boot_regs_t arm_boot_regs_sv = arm_boot_regs;

    /* Clear the sysvars */
    bzero(sysvars_start, sysvars_end - sysvars_start);

    /*
    * Clear the BSS segment.
    * Our stack is explicitly set outside the BSS, so this is safe:
    * bzero() will be able to return.
    */
    bzero(_bss, _ebss - _bss);
    start_in_hyp = start_in_hyp_sv;
    arm_boot_regs = arm_boot_regs_sv;

    /*
     * Describe the board before anything reads a peripheral register: the
     * mailbox call below already needs the peripheral and GPU bases.
     */
    raspi_board_init();

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
    /*
     * If the mailbox call fails, it leaves init_tags (an uninitialized
     * automatic struct) untouched -- its return value must be checked
     * before trusting init_tags.get_arm_memory below, or raspi_top_of_ram
     * would be derived from stack garbage. Nor can arm_memory_base +
     * arm_memory_size be trusted blindly: both are ULONG (32-bit), so
     * the addition could in principle overflow and wrap to a small
     * value.
     *
     * Either failure is unrecoverable here, not just for the PCIe
     * outbound window's safety check (raspi_pci.c) that motivated
     * adding this check in the first place: a bad raspi_top_of_ram also
     * corrupts phystop below, which init_mmu() uses moments later to
     * place the live MMU page table -- likely outside real RAM, into
     * peripheral/MMIO space, corrupting hardware state and/or crashing
     * in a much more confusing spot than here. panic() (already this
     * file's convention for "unrecoverable, stop now" -- see
     * raspi_mmu_protect_range() above) never returns, so nothing below
     * this point ever runs on either failure path.
     */
    if (!raspi_prop_get_tags(&init_tags, sizeof(init_tags))) {
        panic("raspi_vcmem_init: PROPTAG_GET_ARM_MEMORY mailbox query failed\n");
    }
    if (init_tags.get_arm_memory.value2 > (0xffffffffUL - init_tags.get_arm_memory.value1)) {
        panic("raspi_vcmem_init: reported ARM memory range overflows 32 bits (base=0x%lx size=0x%lx)\n",
              init_tags.get_arm_memory.value1, init_tags.get_arm_memory.value2);
    }
    raspi_top_of_ram = (init_tags.get_arm_memory.value1 + init_tags.get_arm_memory.value2);

    /*
     * LPAE uses 2 MiB blocks.  Reserve an entire block for its page tables
     * and non-cacheable VideoCore mailbox buffer.
     */
#if CONF_WITH_ARM_LPAE
    phystop = (UBYTE *)((raspi_top_of_ram - 2 * MEGABYTE) &
                         ~(2 * MEGABYTE - 1));
#else
    /* Reserve the topmost megabyte for page tables and cache coherent buffers */
    phystop = (UBYTE *)((raspi_top_of_ram - MEGABYTE) & ~(MEGABYTE-1));
#endif

#if CONF_WITH_ARM_LPAE
    coherent_buffer = phystop + RASPI_LPAE_TABLE_SIZE;
#else
    raspi_page_table0 = (struct TARMV6MMU_LEVEL1_SECTION_DESCRIPTOR*)phystop;
    coherent_buffer = phystop + PAGE_TABLE0_SIZE;
#endif

    /* Now the bss has been cleared, we can enable the MMU and caches */
#if CONF_WITH_ARM_LPAE
    raspi_lpae_init_mmu((ULONG)phystop, (ULONG)phystop);
#else
    init_mmu((ULONG)phystop);
#endif
}

static void init_mmu(ULONG memory_size)
{
    unsigned i;

    /* C has already written the stack and globals; do not discard them if
     * the firmware entered with D-cache enabled. */
    flush_data_cache_all();

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

#if CONF_WITH_MMU_TEXT_PROTECT
    /*
     * replace section 0's identity mapping with a coarse (4KB page)
     * table so ranges within the first megabyte can be marked
     * read-only at page granularity while everything else stays
     * writable exactly as before. A write into a protected range now
     * faults immediately at the writing instruction instead of
     * silently corrupting whatever it hits, which only surfaces later
     * (possibly much later) when the corrupted memory is used.
     */
    {
        unsigned p;
        struct TARMV6MMU_LEVEL1_COARSE_PAGE_TABLE_DESCRIPTOR coarse_desc;

        for (p = 0; p < ARRAY_SIZE(text_protect_l2); p++)
        {
            ULONG page_addr = SMALL_PAGE_SIZE * p;
            struct TARMV6MMU_LEVEL2_EXT_SMALL_PAGE_DESCRIPTOR *pg = &text_protect_l2[p];

            pg->XNBit  = 0;
            pg->Value1 = 1;
            pg->BBit   = 1;
            pg->CBit   = 1;
            pg->AP     = AP_ALL_ACCESS;
            pg->TEX    = 0;
            pg->APXBit = APX_RW_ACCESS;   /* narrowed later by raspi_mmu_protect_range() */
            pg->SBit   = 1;
            pg->NGBit  = 0;
            pg->Base   = ARMV6MMUL2SMALLPAGEBASE(page_addr);
        }

        coarse_desc.Value01 = 1;
        coarse_desc.SBZ     = 0;
        coarse_desc.Domain  = 0;
        coarse_desc.IMPBit  = 0;
        coarse_desc.Base    = ARMV6MMUL1COARSEBASE((ULONG)text_protect_l2);

        /* raspi_page_table0[0] is declared as a section descriptor, but a
         * coarse-page-table descriptor is the same 4-byte hardware slot
         * under a different bitfield layout. Reinterpreting it via a
         * pointer cast between the two unrelated struct types would
         * violate strict aliasing; memcpy() is well-defined regardless
         * of the types on either side. */
        memcpy(&raspi_page_table0[0], &coarse_desc, sizeof(coarse_desc));
    }
#endif /* CONF_WITH_MMU_TEXT_PROTECT */

    flush_data_cache_all();

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

#if CONF_WITH_MMU_TEXT_PROTECT
    /* [_text, _etext) is .text+.rodata only - genuinely never written.
     * _bss is NOT a safe upper bound: .data sits between _etext and
     * _bss and, despite emutos.ld's aspiration that it stay empty,
     * currently holds structs with legitimately-mutable fields (e.g.
     * usb/ucd_dwc2.c's dwc2_uif, linked into a list via its ->next
     * field) - protecting up to _bss faults on those.
     *
     * raspi_mmu_protect_range() rounds its end argument UP to a page
     * boundary, which is the right default for a caller that wants at
     * least [start,end) covered - but here it would drag in whatever
     * .data shares _etext's page (e.g. dwc2_uif) if _etext isn't
     * itself page-aligned. Round down explicitly instead: losing at
     * most one page of .rodata protection is harmless.
     */
    raspi_mmu_protect_range((ULONG)_text,
        (ULONG)_etext & ~(SMALL_PAGE_SIZE - 1));
#endif /* CONF_WITH_MMU_TEXT_PROTECT */
}


#ifdef TARGET_RPI1
//
// Cache maintenance operations for ARMv6
//
// NOTE: The following functions should hold all variables in CPU registers. Currently this will be
//   ensured using maximum optimization (see bios/processor.h).
//
//   The following numbers can be determined (dynamically) using CTR.
//   As long we use the ARM1176JZF-S implementation in the BCM2835 these static values will work:
//

#define DATA_CACHE_LINE_LENGTH  32

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
