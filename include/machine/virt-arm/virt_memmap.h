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

/* The RAM the port assumes when the device tree does not say how much there
 * is, and the most it uses.  The RAM is mapped at virtual 0 (see virt_mmu.h),
 * so it can be as big as the address space below VIRT_RAM_BASE, where the
 * first megabytes of the RAM are mapped a second time, at their physical
 * address, for the MMU switches at boot and reset.  The limit also keeps
 * phystop a positive LONG, like on virt-m68k. */
#define VIRT_DEFAULT_RAM_SIZE   VIRT_UL(0x08000000)
#define VIRT_MAX_RAM_SIZE       VIRT_UL(0x40000000)

/*
 * The devices.  Their hardware (physical) addresses are the ones named
 * VIRT_*_PA; the code reaches them through virtual addresses, since the
 * RAM takes the low part of the address space (see virt_mmu.h):
 *
 *   hardware 0x00000000 - 0x0fffffff  ->  virtual 0xf0000000 - 0xffffffff
 *       (the GIC, the UART, the virtio-mmio transports, ...)
 *   hardware 0x10000000 - 0x3fffffff  ->  virtual 0xc0000000 - 0xefffffff
 *       (the PCIe MMIO window, the PIO window and the ECAM)
 *
 * VIRT_DEV_VA() converts a hardware address, and the VIRT_*_BASE names below
 * are already virtual.  Only the early boot code that runs with the MMU off
 * (virt_uart.c, virt_mmu.c) uses the hardware addresses.
 */
#define VIRT_DEV_LOW_END        VIRT_UL(0x10000000)
#define VIRT_DEV_LOW_VBASE      VIRT_UL(0xf0000000)
#define VIRT_DEV_HIGH_VBASE     VIRT_UL(0xc0000000)
#define VIRT_DEV_VA(pa) \
    ((pa) < VIRT_DEV_LOW_END ? (pa) + VIRT_DEV_LOW_VBASE \
                             : (pa) - VIRT_DEV_LOW_END + VIRT_DEV_HIGH_VBASE)

#define VIRT_GIC_DIST_PA    VIRT_UL(0x08000000)
#define VIRT_GIC_CPU_PA     VIRT_UL(0x08010000)
#define VIRT_UART0_PA       VIRT_UL(0x09000000)
#define VIRT_VIRTIO_MMIO_PA VIRT_UL(0x0a000000)

#define VIRT_GIC_DIST_BASE  VIRT_DEV_VA(VIRT_GIC_DIST_PA)
#define VIRT_GIC_CPU_BASE   VIRT_DEV_VA(VIRT_GIC_CPU_PA)
#define VIRT_UART0_BASE     VIRT_DEV_VA(VIRT_UART0_PA)

/* PCIe: these are hardware addresses, the ones seen by the bus and written
 * into the BARs; virt_pci.c converts them with VIRT_DEV_VA() for the CPU. */
#define VIRT_PCIE_MMIO_BASE     VIRT_UL(0x10000000)
#define VIRT_PCIE_MMIO_SIZE     VIRT_UL(0x2eff0000)
#define VIRT_PCIE_PIO_BASE      VIRT_UL(0x3eff0000)
#define VIRT_PCIE_PIO_SIZE      VIRT_UL(0x00010000)
#define VIRT_PCIE_ECAM_BASE     VIRT_UL(0x3f000000)
#define VIRT_PCIE_ECAM_SIZE     VIRT_UL(0x01000000)

/* virtio-mmio: 32 transports, 0x200 bytes apart, starting at GIC SPI 16
 * (see hw/arm/virt.c: base_memmap[VIRT_MMIO], irqmap[VIRT_MMIO],
 * NUM_VIRTIO_TRANSPORTS). GIC INTIDs are SPI number + 32, so transport i's
 * IRQ is GIC INTID 48+i -- see virt_connect_irq() in virt_pic.c (extended
 * for this in Task 2). */
#define VIRT_VIRTIO_MMIO_BASE    VIRT_DEV_VA(VIRT_VIRTIO_MMIO_PA)
#define VIRT_VIRTIO_MMIO_STRIDE  VIRT_UL(0x200)
#define VIRT_VIRTIO_MMIO_COUNT   32
#define VIRT_VIRTIO_IRQ_BASE     48

#endif /* MACHINE_VIRT_ARM */

#endif /* VIRT_MEMMAP_H */
