/*
 * raspi_io.h - board description for the Raspberry Pi
 *
 * Copyright (C) 2018-2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef RASPI_IO_H
#define RASPI_IO_H

#ifdef MACHINE_RPI

#include "arm_boot.h"

#define GPU_L2_CACHE_ENABLED

#define GPU_IO_BASE         0x7e000000
#define GPU_CACHED_BASE     0x40000000
#define GPU_UNCACHED_BASE   0xc0000000

/* Kind of timer driving the 200 Hz tick.  See raspi_init_system_timer(). */
#define RASPI_TIMER_SYSTIMER    0   /* BCM2835 system timer, compare 3 */
#define RASPI_TIMER_GENERIC     1   /* ARM generic timer, physical counter */

/*
 * Everything the OS needs to know about the board it woke up on.
 *
 * These are all properties of the hardware rather than of the code, and
 * every one of them is a value that the VideoCore firmware or a flattened
 * device tree could supply at run time.  They used to be #defines selected
 * by TARGET_RPI*; collecting them in one structure is what makes it
 * possible to fill them in from such a source later on, without touching
 * the drivers that read them.
 *
 * The structure lives in the BSS and is filled in by raspi_board_init(),
 * which runs before the first peripheral access.
 */
typedef struct {
    const char *name;       /* SoC name, reported by machine_name() */
    ULONG io_base;          /* ARM view of the peripheral block */
    ULONG local_base;       /* per core registers, 0 if the SoC has none */
    ULONG gpu_mem_base;     /* bus address window the GPU sees RAM through */
    UWORD timer_kind;       /* one of RASPI_TIMER_* above */
    UWORD timer_irq;        /* interrupt raised by the 200 Hz tick */
    ULONG timer_freq;       /* generic timer counter frequency, in Hz */
    ULONG timer_prescaler;  /* generic timer ARM local prescaler */
} raspi_board_t;

extern raspi_board_t raspi_board;

void raspi_board_init(void);

#define ARM_IO_BASE     (raspi_board.io_base)
#define ARM_IO_END      (ARM_IO_BASE + 0xFFFFFF)
#define GPU_MEM_BASE    (raspi_board.gpu_mem_base)

static inline ULONG phys_to_bus(ULONG phys)
{
    return GPU_MEM_BASE | phys;
}

static inline ULONG bus_to_phys(ULONG bus)
{
    return bus & ~GPU_MEM_BASE;
}

#endif /* MACHINE_RPI */
#endif /* RASPI_IO_H */
