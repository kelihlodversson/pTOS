/*
 * raspi_memmap.h - fixed physical addresses of the Raspberry Pi SoCs, and
 * where the higher-half kernel (CONF_WITH_HIGHER_HALF) sees them
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef RASPI_MEMMAP_H
#define RASPI_MEMMAP_H

#include "config.h"

#ifdef MACHINE_RPI

/* constants that startup.S uses too: no C-only UL suffix there */
#ifdef __ASSEMBLER__
#define RASPI_UL(x)     x
#else
#define RASPI_UL(x)     x##UL
#endif

/*
 * The peripheral block (the UART, the mailbox, the interrupt controller, the
 * eMMC controller, ...) and the block of per core registers, as the
 * hardware has them (the ARM physical address).
 */
#if defined(TARGET_RPI1)
#define RASPI_IO_PA         RASPI_UL(0x20000000)
#define RASPI_LOCAL_PA      RASPI_UL(0)         /* the ARM1176 has none */
#elif defined(TARGET_RPI2) || defined(TARGET_RPI3)
#define RASPI_IO_PA         RASPI_UL(0x3f000000)
#define RASPI_LOCAL_PA      RASPI_UL(0x40000000)
#elif defined(TARGET_RPI4)
#define RASPI_IO_PA         RASPI_UL(0xfe000000)
#define RASPI_LOCAL_PA      RASPI_UL(0xff800000)
#else
#error No memory map for the configured Raspberry Pi model
#endif

/* the registers of the PCIe controller of the BCM2711 (bios/machine/raspi/raspi_pci.c) */
#define RASPI_PCIE_REG_PA   RASPI_UL(0xfd500000)

#if CONF_WITH_HIGHER_HALF
/*
 * Where the RAM is in the kernel half: the physical-memory window
 * (doc/mmu-address-space.txt).  The RAM starts at physical 0 here, so the
 * kernel, loaded at 0x8000, is linked at RASPI_WINDOW_VBASE + 0x8000.  The
 * window is 1 GiB: all the RAM of every model up to the 3B+, and what a
 * Pi 4 uses of its RAM.  It also holds the memory of the VideoCore (the
 * framebuffer), which the ARM sees above its own RAM, below the peripherals.
 */
#define RASPI_WINDOW_VBASE  RASPI_UL(0x80000000)
#define RASPI_WINDOW_SIZE   RASPI_UL(0x40000000)

/*
 * The peripherals and the per core registers are in the device half:
 * virtual = 0xf0000000 | (physical & 0x0fffffff).  The BCM2835 peripherals
 * (0x20000000) are at 0xf0000000, the BCM2836/7 ones (0x3f000000) at
 * 0xff000000 and their local block (0x40000000) at 0xf0000000, the BCM2711
 * ones (0xfe000000, local block and GIC from 0xff800000) at their own
 * addresses, as the PCIe controller (0xfd500000) is.
 */
#define RASPI_DEV_VA(pa)    (RASPI_UL(0xf0000000) | ((pa) & RASPI_UL(0x0fffffff)))
#define RASPI_IO_BASE       RASPI_DEV_VA(RASPI_IO_PA)
#define RASPI_LOCAL_BASE    (RASPI_LOCAL_PA ? RASPI_DEV_VA(RASPI_LOCAL_PA) : RASPI_UL(0))
#else
#define RASPI_IO_BASE       RASPI_IO_PA
#define RASPI_LOCAL_BASE    RASPI_LOCAL_PA
#endif

#endif /* MACHINE_RPI */

#endif /* RASPI_MEMMAP_H */
