/*
 * atari_mmu.h - the Atari ROM that runs in the upper half
 * (CONF_ATARI_HIGHER_HALF, #726; doc/mmu-address-space.txt)
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef ATARI_MMU_H
#define ATARI_MMU_H

#if CONF_ATARI_HIGHER_HALF

/*
 * The 68030 MMU is on from the first instruction of the ROM (startup.S), with
 * the static tree of bios/arch/m68k/atari_mmu.S:
 *
 *   - the physical memory from address 0, the ST-RAM, the ROM at 0x00e00000
 *     and the TT-RAM, at ATARI_WINDOW_VBASE: the kernel is linked there (the
 *     ROM at 0x80e00000), except for the TOS low memory;
 *   - the same memory at its physical addresses: the TOS low memory -- the
 *     vectors, the system variables, the BIOS stack and the variables that
 *     must be below 32 KiB -- is there, as on every Atari, and so is the pool
 *     that the memory of the programs and the screen are taken from;
 *   - the I/O area, supervisor-only and not cached.
 *
 * What a device is given (the screen, the DMA of the disks) is a physical
 * address; virt_to_phys() makes it of a kernel address.
 */
#define ATARI_WINDOW_VBASE      0x80000000UL

#ifndef __ASSEMBLER__

/*
 * The physical address of RAM or ROM, given its address in the window or in
 * the pool (which is the physical address): what a DMA device must be given.
 */
ULONG virt_to_phys(const void *va);

/* The pool address (lower half) of the byte after the kernel image, whose
 * address in the window is given: the pool starts there (bios/biosmem.c). */
UBYTE *virt_pool_start(UBYTE *kernel_end);

#endif /* __ASSEMBLER__ */

#endif /* CONF_ATARI_HIGHER_HALF */

#endif /* ATARI_MMU_H */
