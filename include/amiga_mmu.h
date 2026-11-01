/*
 * amiga_mmu.h - the Amiga ROM that runs in the upper half
 * (CONF_AMIGA_HIGHER_HALF, #737; doc/mmu-address-space.txt)
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef AMIGA_MMU_H
#define AMIGA_MMU_H

#if CONF_AMIGA_HIGHER_HALF

/*
 * The 68030 MMU is on from the first instruction of the ROM (startup.S), with
 * the static tree of bios/arch/m68k/amiga_mmu.S: the physical memory from
 * address 0 (the RAM, the I/O areas, the ROM at 0x00fc0000) at
 * AMIGA_WINDOW_VBASE, where the kernel is linked (the ROM at 0x80fc0000), and
 * at its physical addresses, where the TOS low memory and the pool of the
 * programs are.
 *
 * What a custom chip is given (the Copper list, the bit planes, the disk DMA)
 * is a physical address; virt_to_phys() makes it of a kernel address.
 */
#define AMIGA_WINDOW_VBASE      0x80000000UL

#ifndef __ASSEMBLER__

/*
 * The physical address of RAM or ROM, given its address in the window or in
 * the pool (which is the physical address): what a DMA device must be given.
 * 0 if it is neither.
 */
ULONG virt_to_phys(const void *va);

#if CONF_WITH_PROCESS_ASPACES
/* Before a reset (panicasm.S): the static tree is loaded again. */
void machine_reset_prepare(void);
#if CONF_AMIGA_HIGHER_HALF_68040
/* The physical address of the root the MMU ran on when the address spaces
 * began (bios/arch/m68k/aspace_hw.c). */
ULONG aspace_hw_boot_root(void);
#endif
#endif

/* The pool address (lower half) of the byte after the kernel image, whose
 * address in the window is given: the pool starts there (bios/biosmem.c). */
UBYTE *virt_pool_start(UBYTE *kernel_end);

#endif /* !__ASSEMBLER__ */

#endif /* CONF_AMIGA_HIGHER_HALF */

#endif /* AMIGA_MMU_H */
