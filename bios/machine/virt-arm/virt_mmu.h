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
 * Builds a 4096-entry (4 GiB, 1 MiB/section) level-1 page table at
 * pagetable_phys and enables the MMU:
 *   - virtual [0, ram_size_bytes) maps to physical [VIRT_RAM_BASE,
 *     VIRT_RAM_BASE + ram_size_bytes) -- this is what makes the fixed
 *     low TOS system-variable addresses (see tosvars.ld) resolve to
 *     real RAM.
 *   - every other virtual address is identity-mapped (virtual ==
 *     physical), which is how the peripheral drivers (virt_uart.c, the
 *     future virt_pic.c/virt_timer.c) already address the GIC and the
 *     UART via VIRT_GIC_*_BASE/VIRT_UART0_BASE literal constants.
 *
 * Must be called with the MMU off, from startup.S, with a physical
 * (not linked/virtual) stack pointer already set up.  Neither this
 * function nor anything it calls may reference a global or static C
 * variable: at the point it runs, no linked (virtual) address is
 * valid yet, only literal constants and its own parameters/locals.
 */
void virt_mmu_bootstrap(ULONG ram_size_bytes, void *pagetable_phys);

#endif /* MACHINE_VIRT_ARM */

#endif /* VIRT_MMU_H */
