/*
 * virt_mmu.h - static MMU translation table bring-up for QEMU's ARM
 * 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef VIRT_MMU_H
#define VIRT_MMU_H

#ifdef MACHINE_VIRT_ARM

/*
 * Builds the two level-1 page tables (1 MiB sections) at pagetable_phys, a
 * 16 KiB table for the kernel half followed by an 8 KiB one for the lower
 * half, and enables the MMU with TTBCR.N = 1 (TTBR1: the upper 2 GiB, TTBR0:
 * the lower 2 GiB; doc/mmu-address-space.txt):
 *   - the kernel half has the RAM from its first byte at VIRT_WINDOW_VBASE
 *     (the kernel is linked there), and the board's devices, with the
 *     hardware address 0x08000000 and up at virtual 0xf0000000 + that
 *     address, and 0x10000000 and up (PCIe) at 0xc0000000 + (that address -
 *     0x10000000): see VIRT_DEV_VA() in virt_memmap.h.
 *   - the lower half has the same RAM at virtual 0, the pool the memory of
 *     the programs is taken from, and the first 16 MiB of the RAM where they
 *     physically are, for the code executing there when the MMU is switched
 *     on or off (a software reset); this mapping is kept.
 *   - everything else is unmapped and faults.
 *
 * Must be called with the MMU off, from startup.S, with a physical
 * (not linked/virtual) stack pointer already set up.  Neither this
 * function nor anything it calls may reference a global or static C
 * variable: at the point it runs, no linked (virtual) address is
 * valid yet, only literal constants and its own parameters/locals.
 */
void virt_mmu_bootstrap(ULONG ram_size_bytes, void *pagetable_phys);

/*
 * Translates the address of RAM -- an address in the window, as the address
 * of a static or stack variable of the kernel is, or one in the pool, as the
 * buffer of a program is -- into the physical address that a DMA-capable
 * device (such as a virtio-mmio transport) must be given instead.  Devices
 * are not behind this port's MMU, so they only ever see physical addresses.
 * Not meaningful for peripheral MMIO addresses (the result is 0).
 */
ULONG virt_to_phys(const void *va);

/* The pool address (lower half) of the byte after the kernel image, whose
 * address in the window is given: the pool starts there (bios/biosmem.c). */
UBYTE *virt_pool_start(UBYTE *kernel_end);

#endif /* MACHINE_VIRT_ARM */

#endif /* VIRT_MMU_H */
