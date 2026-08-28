/*
 * raspi_board.c - the board pTOS is running on
 *
 * Copyright (C) 2018-2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*
 * The single place where a Raspberry Pi model is turned into the addresses
 * and interrupt numbers the drivers use.  Every driver reads raspi_board
 * instead of a TARGET_RPI* conditional, so replacing the table below with
 * something that asks the firmware (PROPTAG_GET_BOARD_REVISION) or parses
 * a device tree does not touch the drivers at all.
 *
 * Until then the values still come from the configured target: the boot
 * firmware already picks the image by file name (kernel.img, kernel7.img,
 * kernel8-32.img, kernel7l.img) and the models need different -march
 * settings anyway, so a single image for all of them is not on the table.
 */

#include "config.h"
#ifndef MACHINE_RPI
#error This file must only be compiled for raspberry PI targets
#endif

#include "portab.h"
#include "raspi_io.h"
#include "raspi_int.h"

raspi_board_t raspi_board;

static const raspi_board_t this_board =
{
#if defined(TARGET_RPI1)
    "BCM2835",
    0x20000000UL,               /* peripherals */
    0,                          /* the ARM1176 has no local registers */
#   ifdef GPU_L2_CACHE_ENABLED
    GPU_CACHED_BASE,
#   else
    GPU_UNCACHED_BASE,
#   endif
    RASPI_TIMER_SYSTIMER,
    ARM_IRQ_TIMER3,
    0,                          /* no generic timer */
    0,
#elif defined(TARGET_RPI2) || defined(TARGET_RPI3)
    /*
     * The BCM2836 and BCM2837 differ only in the CPU core, which is a
     * compile time matter, so they share a description here.
     */
#   if defined(TARGET_RPI2)
    "BCM2836",
#   else
    "BCM2837",
#   endif
    0x3f000000UL,               /* peripherals */
    0x40000000UL,               /* local registers */
    GPU_UNCACHED_BASE,
    RASPI_TIMER_SYSTIMER,
    ARM_IRQ_TIMER3,
    19200000UL,                 /* generic timer, if ever selected */
    0x6AAAAABUL,
#elif defined(TARGET_RPI4)
    "BCM2711",
    0xfe000000UL,               /* peripherals */
    0xff800000UL,               /* local registers */
    GPU_UNCACHED_BASE,
    /*
     * The BCM2711 system timer is not usable the way the older SoCs' is,
     * so the tick comes from the ARM generic timer instead.
     */
    RASPI_TIMER_GENERIC,
    0,                          /* PPI 30 is connected directly to the GIC */
    0,                          /* CNTFRQ supplies the counter frequency */
    0,                          /* BCM2711 has no usable local prescaler */
#else
#error No description for the configured Raspberry Pi model
#endif
};

/*
 * Called from raspi_vcmem_init(), after the BSS has been cleared and
 * before anything touches a peripheral.
 */
void raspi_board_init(void)
{
    raspi_board = this_board;
}
