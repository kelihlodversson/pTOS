/*
 * virt_memmap.h - fixed physical addresses of the QEMU ARM 'virt' board
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef VIRT_MEMMAP_H
#define VIRT_MEMMAP_H

#ifdef MACHINE_VIRT_ARM

/* constants that startup.S uses too: no C-only UL suffix there */
#ifdef __ASSEMBLER__
#define VIRT_UL(x)  x
#else
#define VIRT_UL(x)  x##UL
#endif

/* Where RAM starts on this board.  Unlike every other machine this port
 * supports, address 0 is NOT RAM here (it is flash) -- see the boot
 * sequence in startup.S for how the fixed low-address TOS system
 * variables are made to work regardless. */
#define VIRT_RAM_BASE       VIRT_UL(0x40000000)

/* The RAM the port assumes when the device tree does not say how much there is,
 * and the most it can use: the low virtual window that holds the fixed-address
 * TOS system variables (see virt_mmu.c) has to stop where the devices start. */
#define VIRT_DEFAULT_RAM_SIZE   VIRT_UL(0x08000000)
#define VIRT_MAX_RAM_SIZE       VIRT_UL(0x08000000)

#define VIRT_GIC_DIST_BASE  0x08000000UL
#define VIRT_GIC_CPU_BASE   0x08010000UL
#define VIRT_UART0_BASE     0x09000000UL
#define VIRT_PCIE_MMIO_BASE     0x10000000UL
#define VIRT_PCIE_MMIO_SIZE     0x2eff0000UL
#define VIRT_PCIE_PIO_BASE      0x3eff0000UL
#define VIRT_PCIE_PIO_SIZE      0x00010000UL
#define VIRT_PCIE_ECAM_BASE     0x3f000000UL
#define VIRT_PCIE_ECAM_SIZE     0x01000000UL

/* virtio-mmio: 32 transports, 0x200 bytes apart, starting at GIC SPI 16
 * (see hw/arm/virt.c: base_memmap[VIRT_MMIO], irqmap[VIRT_MMIO],
 * NUM_VIRTIO_TRANSPORTS). GIC INTIDs are SPI number + 32, so transport i's
 * IRQ is GIC INTID 48+i -- see virt_connect_irq() in virt_pic.c (extended
 * for this in Task 2). */
#define VIRT_VIRTIO_MMIO_BASE    0x0a000000UL
#define VIRT_VIRTIO_MMIO_STRIDE  0x200UL
#define VIRT_VIRTIO_MMIO_COUNT   32
#define VIRT_VIRTIO_IRQ_BASE     48

#endif /* MACHINE_VIRT_ARM */

#endif /* VIRT_MEMMAP_H */
