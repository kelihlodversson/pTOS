/*
 * memory.c - Raspberry Pi memory and MMU initialization
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
#include "arch/arm/arm_boot.h"
#include "raspi_io.h"
#include "raspi_int.h"
#include "raspi_mbox.h"
#include "raspi_mmu.h"
#include "raspi_memory.h"
#include "raspi_memmap.h"
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

#if !CONF_WITH_HIGHER_HALF
static void init_mmu(ULONG memory_size);
#endif

#if CONF_WITH_MMU_TEXT_PROTECT
/* page-granularity table covering section 0 (0x0-0xFFFFF), so ranges
 * within the first megabyte can be marked read-only individually.
 * Populated by init_mmu(), narrowed later (after boot-time
 * initializers have finished writing their targets) by
 * raspi_mmu_protect_range(). */
#define L2_ENTRIES (ARMV6MMU_LEVEL2_COARSE_PAGE_TABLE_SIZE / sizeof(struct TARMV6MMU_LEVEL2_EXT_SMALL_PAGE_DESCRIPTOR))
#if CONF_WITH_HIGHER_HALF
/* in the megabyte of the tables, built by raspi_mmu_bootstrap(); the window
 * is the first megabyte of the RAM, and the addresses are those of the kernel */
static struct TARMV6MMU_LEVEL2_EXT_SMALL_PAGE_DESCRIPTOR *text_protect_l2;
#define TEXT_PROTECT_BASE   RASPI_WINDOW_VBASE
#else
static struct TARMV6MMU_LEVEL2_EXT_SMALL_PAGE_DESCRIPTOR
    text_protect_l2[L2_ENTRIES]
    __attribute__((aligned(ARMV6MMU_LEVEL2_COARSE_PAGE_TABLE_SIZE)));
#define TEXT_PROTECT_BASE   0UL
#endif

/*
 * mark [start, end) read-only at page granularity. Both must fall
 * within section 0 (the first megabyte) - the only section init_mmu()
 * has broken out into a page table. Rounds start down / end up to
 * page boundaries, so it may protect a little more than asked.
 */
void raspi_mmu_protect_range(ULONG start, ULONG end)
{
    ULONG protect_start, protect_end;
    unsigned p;

    start -= TEXT_PROTECT_BASE;
    end -= TEXT_PROTECT_BASE;
    protect_start = start & ~(SMALL_PAGE_SIZE - 1);
    protect_end   = (end + SMALL_PAGE_SIZE - 1) & ~(SMALL_PAGE_SIZE - 1);

    /* text_protect_l2[] only has entries for section 0 (the first
     * megabyte); silently protecting just the subset that fits would
     * leave the rest of [start,end) writable while looking enabled -
     * defeating the point of a feature whose whole job is to fail loudly. */
    if (protect_start >= SECTION_SIZE || protect_end > SECTION_SIZE)
        panic("raspi_mmu_protect_range(%p,%p): outside the first megabyte\n",
              (void*)(start + TEXT_PROTECT_BASE), (void*)(end + TEXT_PROTECT_BASE));

    for (p = 0; p < L2_ENTRIES; p++)
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

    KDEBUG(("mmu: write-protecting [%p,%p)\n", (void*)(protect_start + TEXT_PROTECT_BASE), (void*)(protect_end + TEXT_PROTECT_BASE)));
}
#endif /* CONF_WITH_MMU_TEXT_PROTECT */

extern char sysvars_start[];
extern char sysvars_end[];

static UBYTE* coherent_buffer;
#if !CONF_WITH_HIGHER_HALF
struct TARMV6MMU_LEVEL1_SECTION_DESCRIPTOR* raspi_page_table0;
#endif

UBYTE* raspi_get_coherent_buffer(int tag)
{
    return coherent_buffer + (tag * 4096);
}

#if CONF_WITH_HIGHER_HALF
/*
 * The MMU is on, and the kernel runs in the window, when this is called: the
 * start (startup.S, raspi_mmu_boot.c) switched it on, cleared the BSS, and
 * found phystop, which is the top of the pool: the RAM below the megabyte
 * of the tables.  What is left is to find that megabyte, through the window.
 */
void raspi_vcmem_init(void)
{
#if CONF_WITH_PROCESS_ASPACES
    UBYTE *tables = phystop;            /* (in the window already) */
#else
    UBYTE *tables = phystop + RASPI_WINDOW_VBASE;
#endif

    /*
     * Describe the board before anything reads a peripheral register: the
     * mailbox calls need the peripheral and GPU bases.
     */
    raspi_board_init();

    coherent_buffer = tables + RASPI_COHERENT_OFFSET;
#if CONF_WITH_MMU_TEXT_PROTECT
    text_protect_l2 = (struct TARMV6MMU_LEVEL2_EXT_SMALL_PAGE_DESCRIPTOR *)
                      (tables + RASPI_L2_OFFSET);

    /* [_text, _etext) is .text+.rodata only - see init_mmu() below */
    raspi_mmu_protect_range((ULONG)_text,
        (ULONG)_etext & ~(SMALL_PAGE_SIZE - 1));
#endif
}

/*
 * The physical address of RAM the CPU uses: in the window (the kernel's own
 * memory) or in the pool at virtual 0 (the memory of the programs, which is
 * at its physical address).  Not meaningful for anything else (peripherals,
 * unmapped).  That is what a bus master must be given instead of the
 * pointer (raspi_dma_bus()).
 */
ULONG virt_to_phys(const void *va)
{
    ULONG a = (ULONG)(uintptr_t)va;

    if (a >= RASPI_WINDOW_VBASE && a - RASPI_WINDOW_VBASE < RASPI_WINDOW_SIZE)
        return a - RASPI_WINDOW_VBASE;
#if !CONF_WITH_PROCESS_ASPACES
    if (a < RASPI_WINDOW_SIZE)
        return a;
#endif
    KDEBUG(("virt_to_phys(%p): not RAM\n", va));
    return 0;
}

/*
 * The pool address (lower half) of the first byte after the kernel: the
 * memory after the kernel image is the pool, reached through the lower half.
 */
UBYTE *virt_pool_start(UBYTE *kernel_end)
{
#if CONF_WITH_PROCESS_ASPACES
    return kernel_end;          /* the memory of the kernel is all in the window */
#else
    return kernel_end - RASPI_WINDOW_VBASE;
#endif
}

#if CONF_WITH_PROCESS_ASPACES
extern ULONG boot_ttbr0;        /* startup.S */

/* how the table walk reads the table (raspi_mmu_boot.c) */
#ifdef TARGET_RPI1
#define RASPI_TTBR_MODE ( ARM_TTBR_INNER_CACHEABLE | ARM_TTBR_OUTER_NON_CACHEABLE)
#else
#define RASPI_TTBR_MODE ( ARM_TTBR_INNER_WRITE_BACK | ARM_TTBR_OUTER_WRITE_BACK)
#endif

/*
 * A reset comes back to startup.S with the MMU on, and goes to the physical
 * alias of the code, which only the table the MMU was first switched on with
 * has: load it before the jump (the tables of the processes have no identity
 * mapping).
 */
void machine_reset_prepare(void)
{
    ULONG zero = 0;

    __asm__ volatile ("mcr p15, 0, %0, c2, c0, 0" : : "r" (boot_ttbr0 | RASPI_TTBR_MODE) : "memory");
    __asm__ volatile ("mcr p15, 0, %0, c7, c5, 4" : : "r" (zero) : "memory");   /* flush the prefetch buffer */
    __asm__ volatile ("mcr p15, 0, %0, c8, c7, 0" : : "r" (zero) : "memory");   /* the whole TLB */
    __asm__ volatile ("mcr p15, 0, %0, c7, c10, 4" : : "r" (zero) : "memory");  /* data synchronization barrier */
    __asm__ volatile ("mcr p15, 0, %0, c7, c5, 4" : : "r" (zero) : "memory");
}
#endif
#else
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


#endif /* !CONF_WITH_HIGHER_HALF */


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

#define DATA_CACHE_LINE_LENGTH      32

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
