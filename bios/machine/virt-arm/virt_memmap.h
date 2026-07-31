/*
 * virt_memmap.h - fixed physical addresses of the QEMU ARM 'virt' board
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef VIRT_MEMMAP_H
#define VIRT_MEMMAP_H

#ifdef MACHINE_VIRT_ARM

/* Where RAM starts on this board.  Unlike every other machine this port
 * supports, address 0 is NOT RAM here (it is flash) -- see the boot
 * sequence in startup.S for how the fixed low-address TOS system
 * variables are made to work regardless. */
#define VIRT_RAM_BASE       0x40000000UL

#define VIRT_GIC_DIST_BASE  0x08000000UL
#define VIRT_GIC_CPU_BASE   0x08010000UL
#define VIRT_UART0_BASE     0x09000000UL

#endif /* MACHINE_VIRT_ARM */

#endif /* VIRT_MEMMAP_H */
