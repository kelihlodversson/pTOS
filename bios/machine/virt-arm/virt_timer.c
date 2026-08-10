/*
 * virt_timer.c - ARM generic timer periodic tick for QEMU's ARM
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
#include "virt_pic.h"
#include "virt_timer.h"
#include "vectors.h"
#include "asm.h"
#if CONF_WITH_VIRT_UART
#include "virt_uart.h"
#endif

#define HZ                    200     /* ticks per second, matches the Atari 200 Hz timer C */
#define VIRT_TIMER_PPI_PHYS   30      /* non-secure physical timer, fixed by the GIC/generic-timer binding */

static ULONG ticks_per_hz;

static void virt_timer_tick(void)
{
    ULONG cval_low, cval_high;
    UQUAD cval;

#if CONF_SERIAL_CONSOLE && CONF_WITH_VIRT_UART
    virt_uart0_poll_rx();
#endif

    /* virt_timer_init() connects and enables this IRQ well before
     * mfp.c's init_system_timer() sets vector_5ms (that happens much
     * later in bios_init()'s sequence) -- so the first several ticks
     * can legitimately arrive with vector_5ms still NULL. Guard it,
     * unlike raspi's raspi_timer3_handler() which can call vector_5ms()
     * unconditionally because raspi only ever connects/enables its
     * timer IRQ from inside init_system_timer(), after vector_5ms is
     * already set. */
    if (vector_5ms)
        vector_5ms();

    asm volatile ("mrrc p15, 2, %0, %1, c14" : "=r" (cval_low), "=r" (cval_high));
    cval = ((UQUAD) cval_high << 32 | cval_low) + ticks_per_hz;
    asm volatile ("mcrr p15, 2, %0, %1, c14" :: "r" ((ULONG)(cval & 0xffffffffUL)),
                                                "r" ((ULONG)(cval >> 32)));
}

void virt_timer_init(void)
{
    ULONG cntfrq;
    ULONG cval_low, cval_high;
    UQUAD cval;

    asm volatile ("mrc p15, 0, %0, c14, c0, 0" : "=r" (cntfrq));
    ticks_per_hz = cntfrq / HZ;

    virt_connect_irq(VIRT_TIMER_PPI_PHYS, virt_timer_tick);

    asm volatile ("mrrc p15, 0, %0, %1, c14" : "=r" (cval_low), "=r" (cval_high));
    cval = ((UQUAD) cval_high << 32 | cval_low) + ticks_per_hz;
    asm volatile ("mcrr p15, 2, %0, %1, c14" :: "r" ((ULONG)(cval & 0xffffffffUL)),
                                                "r" ((ULONG)(cval >> 32)));
    asm volatile ("mcr p15, 0, %0, c14, c2, 1" :: "r" (1));   /* CNTP_CTL: ENABLE */
    flush_prefetch_buffer();    /* ISB: the timer is live from here on */
}
