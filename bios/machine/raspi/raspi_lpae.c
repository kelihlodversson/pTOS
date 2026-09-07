/*
 * raspi_lpae.c - RPi4 ARMv7 LPAE bootstrap mapping
 */

#include "config.h"

#if !defined(TARGET_RPI4) || !CONF_WITH_ARM_LPAE
#error This file must only be compiled for the RPi4 LPAE configuration
#endif

#include "portab.h"
#include "raspi_lpae.h"
#include "raspi_mmu.h"
#if CONF_WITH_RASPI_UART0
#include "raspi_uart.h"
#endif
#include "processor.h"
#include "asm.h"

#define LPAE_L1_ENTRIES             4U
#define LPAE_L2_ENTRIES             512U
#define LPAE_TABLE_DESCRIPTOR       0x3ULL
#define LPAE_BLOCK_DESCRIPTOR       0x1ULL
#define LPAE_DESCRIPTOR_AF          (1ULL << 10)
#define LPAE_DESCRIPTOR_SH_INNER    (3ULL << 8)
#define LPAE_DESCRIPTOR_SH_OUTER    (2ULL << 8)
#define LPAE_DESCRIPTOR_AP_RW        (1ULL << 6)
#define LPAE_DESCRIPTOR_XN          (1ULL << 54)
#define LPAE_ATTR_DEVICE            (0ULL << 2)
#define LPAE_ATTR_NORMAL            (1ULL << 2)
#define LPAE_ATTR_NORMAL_NC         (2ULL << 2)
#define LPAE_BLOCK_MASK             0x000000ffffe00000ULL
#define LPAE_PCIE_VIRT_BASE         0xf8000000UL
#define LPAE_PCIE_PHYS_BASE         0x0000000600000000ULL
#define LPAE_PCIE_SIZE              0x04000000UL
#define LPAE_MAIR0                  0x0044ff00UL
#define LPAE_TTBCR_EAE              0x80000000UL
#define LPAE_TTBCR_SH0_INNER        (3UL << 12)
#define LPAE_TTBCR_ORGN0_WBWA       (1UL << 10)
#define LPAE_TTBCR_IRGN0_WBWA       (1UL << 8)

static UQUAD lpae_block(ULONG virt, UQUAD phys, ULONG memory_size)
{
    UQUAD descriptor;

    descriptor = (phys & LPAE_BLOCK_MASK) | LPAE_BLOCK_DESCRIPTOR |
                 LPAE_DESCRIPTOR_AF;
    if (virt < memory_size) {
        descriptor |= LPAE_ATTR_NORMAL | LPAE_DESCRIPTOR_SH_INNER |
                      LPAE_DESCRIPTOR_AP_RW;
    } else if (virt == memory_size) {
        /* Page tables and VideoCore buffers need RAM transactions, not
         * device transactions, but must remain outside the data cache. */
        descriptor |= LPAE_DESCRIPTOR_XN | LPAE_ATTR_NORMAL_NC |
                      LPAE_DESCRIPTOR_SH_OUTER | LPAE_DESCRIPTOR_AP_RW;
    } else {
        descriptor |= LPAE_DESCRIPTOR_XN | LPAE_ATTR_DEVICE |
                      LPAE_DESCRIPTOR_SH_OUTER;
    }
    return descriptor;
}

void raspi_lpae_init_mmu(ULONG memory_size, ULONG table_base)
{
    UQUAD *l1;
    UQUAD *l2;
    UQUAD phys;
    ULONG aux_control;
    ULONG control;
    unsigned i;
    unsigned j;

    l1 = (UQUAD *)table_base;
    l2 = (UQUAD *)(table_base + 0x1000UL);

#if CONF_WITH_RASPI_UART0
    raspi_uart0_init();
#endif
    invalidate_data_cache_all();

    for (i = 0; i < LPAE_L1_ENTRIES; i++) {
        l1[i] = ((UQUAD)(ULONG)&l2[i * LPAE_L2_ENTRIES]) | LPAE_TABLE_DESCRIPTOR;
        for (j = 0; j < LPAE_L2_ENTRIES; j++) {
            ULONG virt;

            virt = ((ULONG)i << 30) | ((ULONG)j << 21);
            phys = (UQUAD)virt;
            if ((virt >= LPAE_PCIE_VIRT_BASE) &&
                (virt < LPAE_PCIE_VIRT_BASE + LPAE_PCIE_SIZE))
                phys = LPAE_PCIE_PHYS_BASE + (virt - LPAE_PCIE_VIRT_BASE);
            l2[i * LPAE_L2_ENTRIES + j] = lpae_block(virt, phys, memory_size);
        }
    }

    flush_data_cache_all();

    asm volatile ("mrc p15, 0, %0, c1, c0, 1" : "=r" (aux_control));
    aux_control |= ARM_AUX_CONTROL_SMP;
    asm volatile ("mcr p15, 0, %0, c1, c0, 1" : : "r" (aux_control));
    data_sync_barrier();
    flush_prefetch_buffer();

    asm volatile ("mcr p15, 0, %0, c2, c0, 2" : :
                  "r" (LPAE_TTBCR_EAE | LPAE_TTBCR_SH0_INNER |
                       LPAE_TTBCR_ORGN0_WBWA | LPAE_TTBCR_IRGN0_WBWA));
    data_sync_barrier();
    flush_prefetch_buffer();
    asm volatile ("mcr p15, 0, %0, c10, c2, 0" : : "r" (LPAE_MAIR0));
    data_sync_barrier();
    flush_prefetch_buffer();

    asm volatile ("mcrr p15, 0, %0, %1, c2" : :
                  "r" (table_base), "r" (0UL));
    asm volatile ("mcrr p15, 1, %0, %1, c2" : :
                  "r" (table_base), "r" (0UL));

    asm volatile ("mcr p15, 0, %0, c7, c5, 0" : : "r" (0));
    asm volatile ("mcr p15, 0, %0, c7, c5, 6" : : "r" (0));
    asm volatile ("mcr p15, 0, %0, c8, c7, 0" : : "r" (0));
    data_sync_barrier();
    flush_prefetch_buffer();

    asm volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r" (control));
    control &= ~(ARM_CONTROL_STRICT_ALIGNMENT);
    control &= ~ARM_CONTROL_WRITE_XN;
    control |= ARM_CONTROL_MMU | ARM_CONTROL_L1_CACHE |
               ARM_CONTROL_L1_INSTRUCTION_CACHE | ARM_CONTROL_BRANCH_PREDICTION;
    asm volatile ("mcr p15, 0, %0, c1, c0, 0" : : "r" (control) : "memory");
    flush_prefetch_buffer();
}
