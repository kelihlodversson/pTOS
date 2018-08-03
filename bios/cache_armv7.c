// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2010
 * Texas Instruments, <www.ti.com>
 * Aneesh V <aneesh@ti.com>
 */
#include "config.h"
#include "portab.h"
#include "asm.h"

/* CCSIDR */
#define CCSIDR_LINE_SIZE_OFFSET		0
#define CCSIDR_LINE_SIZE_MASK		0x7
#define CCSIDR_ASSOCIATIVITY_OFFSET	3
#define CCSIDR_ASSOCIATIVITY_MASK	(0x3FF << 3)
#define CCSIDR_NUM_SETS_OFFSET		13
#define CCSIDR_NUM_SETS_MASK		(0x7FFF << 13)

#define ARMV7_DCACHE_INVAL_RANGE        1
#define ARMV7_DCACHE_CLEAN_INVAL_RANGE  2


static uint32_t get_ccsidr(void);
static void v7_dcache_clean_inval_range(uint32_t start, uint32_t stop, uint32_t line_len);
static void v7_dcache_inval_range(uint32_t start, uint32_t stop, uint32_t line_len);
static void v7_dcache_maint_range(uint32_t start, uint32_t stop, uint32_t range_op);
static void v7_inval_tlb(void);
void clean_data_cache(void);
void flush_data_cache_all(void);

void v7_outer_cache_enable(void);
void v7_outer_cache_disable(void);
void v7_outer_cache_flush_all(void);
void v7_outer_cache_inval_all(void);
void v7_outer_cache_flush_range(uint32_t start, uint32_t end);
void v7_outer_cache_inval_range(uint32_t start, uint32_t end);


/* Asm functions from cache_v7_asm.S */
void v7_flush_dcache_all(void);
void v7_invalidate_dcache_all(void);

static uint32_t get_ccsidr(void)
{
        uint32_t ccsidr;

        /* Read current CP15 Cache Size ID Register */
        asm volatile ("mrc p15, 1, %0, c0, c0, 0" : "=r" (ccsidr));
        return ccsidr;
}

static void v7_dcache_clean_inval_range(uint32_t start, uint32_t stop, uint32_t line_len)
{
        uint32_t mva;

        /* Align start to cache line boundary */
        start &= ~(line_len - 1);
        for (mva = start; mva < stop; mva = mva + line_len) {
                /* DCCIMVAC - Clean & Invalidate data cache by MVA to PoC */
                asm volatile ("mcr p15, 0, %0, c7, c14, 1" : : "r" (mva));
        }
}

static void v7_dcache_inval_range(uint32_t start, uint32_t stop, uint32_t line_len)
{
        uint32_t mva;

        //if (!check_cache_range(start, stop))
        //        return;

        for (mva = start; mva < stop; mva = mva + line_len) {
                /* DCIMVAC - Invalidate data cache by MVA to PoC */
                asm volatile ("mcr p15, 0, %0, c7, c6, 1" : : "r" (mva));
        }
}

static void v7_dcache_maint_range(uint32_t start, uint32_t stop, uint32_t range_op)
{
        uint32_t line_len, ccsidr;

        ccsidr = get_ccsidr();
        line_len = ((ccsidr & CCSIDR_LINE_SIZE_MASK) >>
                        CCSIDR_LINE_SIZE_OFFSET) + 2;
        /* Converting from words to bytes */
        line_len += 2;
        /* converting from log2(linelen) to linelen */
        line_len = 1 << line_len;

        switch (range_op) {
        case ARMV7_DCACHE_CLEAN_INVAL_RANGE:
                v7_dcache_clean_inval_range(start, stop, line_len);
                break;
        case ARMV7_DCACHE_INVAL_RANGE:
                v7_dcache_inval_range(start, stop, line_len);
                break;
        }

        /* DSB to make sure the operation is complete */
        data_sync_barrier();
}

/* Invalidate TLB */
static void v7_inval_tlb(void)
{
        /* Invalidate entire unified TLB */
        asm volatile ("mcr p15, 0, %0, c8, c7, 0" : : "r" (0));
        /* Invalidate entire data TLB */
        asm volatile ("mcr p15, 0, %0, c8, c6, 0" : : "r" (0));
        /* Invalidate entire instruction TLB */
        asm volatile ("mcr p15, 0, %0, c8, c5, 0" : : "r" (0));
        /* Full system DSB - make sure that the invalidation is complete */
        data_sync_barrier();
        /* Full system ISB - make sure the instruction stream sees it */
        instruction_sync_barrier();
}

void clean_data_cache(void)
{
        v7_invalidate_dcache_all();
        v7_outer_cache_inval_all();
}

/*
 * Performs a clean & invalidation of the entire data cache
 * at all levels
 */
void flush_data_cache_all(void)
{
        v7_flush_dcache_all();
        v7_outer_cache_flush_all();
}

/*
 * Invalidates range in all levels of D-cache/unified cache used:
 * Affects the range [start, stop - 1]
 */
void invalidate_dcache_range(unsigned long start, unsigned long stop)
{
        //check_cache_range(start, stop);

        v7_dcache_maint_range(start, stop, ARMV7_DCACHE_INVAL_RANGE);

        v7_outer_cache_inval_range(start, stop);
}

/*
 * Flush range(clean & invalidate) from all levels of D-cache/unified
 * cache used:
 * Affects the range [start, stop - 1]
 */
void flush_dcache_range(unsigned long start, unsigned long stop)
{
        //check_cache_range(start, stop);

        v7_dcache_maint_range(start, stop, ARMV7_DCACHE_CLEAN_INVAL_RANGE);

        v7_outer_cache_flush_range(start, stop);
}

void flush_data_cache(void *start, long size)
{
    flush_dcache_range((unsigned long)start, (unsigned long)start+size);
}

void invalidate_data_cache(void *start, long size)
{
    invalidate_dcache_range((unsigned long)start, (unsigned long)start+size);
}


void arm_init_before_mmu(void)
{
        v7_outer_cache_enable();
        clean_data_cache();
        v7_inval_tlb();
}

void mmu_page_table_flush(unsigned long start, unsigned long stop)
{
        flush_dcache_range(start, stop);
        v7_inval_tlb();
}

/* Invalidate entire I-cache and branch predictor array */
void invalidate_icache_all(void)
{
        /*
         * Invalidate all instruction caches to PoU.
         * Also flushes branch target cache.
         */
        asm volatile ("mcr p15, 0, %0, c7, c5, 0" : : "r" (0));

        /* Invalidate entire branch predictor array */
        asm volatile ("mcr p15, 0, %0, c7, c5, 6" : : "r" (0));

        /* Full system DSB - make sure that the invalidation is complete */
        data_sync_barrier();

        /* ISB - make sure the instruction stream sees it */
        instruction_sync_barrier();
}

/*  Stub implementations for outer cache operations */
#define __weak		__attribute__((weak))
__weak void v7_outer_cache_enable(void) {}
__weak void v7_outer_cache_disable(void) {}
__weak void v7_outer_cache_flush_all(void) {}
__weak void v7_outer_cache_inval_all(void) {}
__weak void v7_outer_cache_flush_range(uint32_t start, uint32_t end) {}
__weak void v7_outer_cache_inval_range(uint32_t start, uint32_t end) {}
