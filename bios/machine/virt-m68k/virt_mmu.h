/*
 * virt_mmu.h - the higher-half kernel of QEMU's m68k 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef VIRT_MMU_H
#define VIRT_MMU_H

#ifdef MACHINE_VIRT_M68K

/*
 * The image that requires the PMMU (CONF_REQUIRE_PMMU, #564) is a higher-half
 * kernel (CONF_WITH_HIGHER_HALF, #569; doc/mmu-address-space.txt).  The RAM of
 * the board is at physical 0, and the 68040's MMU is switched on by the start
 * (virt_mmu_early.S, called from startup.S), which maps:
 *
 *   - the RAM from its first byte at VIRT_WINDOW_VBASE: the kernel is linked
 *     there, except for the TOS low memory (emutos.ld);
 *   - the same RAM at virtual 0, which is the physical address.  That is the
 *     pool the memory of the programs is taken from (the addresses Malloc()
 *     and Pexec() give out stay positive LONGs, not kernel addresses), it
 *     holds the TOS low memory -- the vectors, the system variables, the BIOS
 *     stack and the variables that must be below 32 KiB -- at the addresses
 *     they have on every m68k machine, and it is where the code runs while
 *     the MMU is switched on and off (a software reset);
 *   - the devices, at the physical addresses they have, 0xff000000 and up
 *     (kernel half addresses already), through a transparent translation
 *     register: supervisor-only and not cached;
 *   - everything else is unmapped and faults.
 * The page of the TOS low memory (the first 4 KiB: the vectors, the system
 * variables and the BIOS stack) is supervisor-only.  The window is not yet:
 * the desktop and EmuCON run in user mode from the kernel image (#568).
 *
 * The tables are at the top of the RAM, above phystop, which the start lowers
 * for them.
 */
#ifdef __ASSEMBLER__
#define VIRT_UL(x)              x
#else
#define VIRT_UL(x)              x##UL
#endif

#define VIRT_WINDOW_VBASE       VIRT_UL(0x80000000)

/* The most RAM the kernel uses: what the window holds.  Also keeps phystop a
 * positive LONG, clear of the devices. */
#define VIRT_MAX_RAM_SIZE       VIRT_UL(0x40000000)

#ifndef __ASSEMBLER__

/*
 * Translates the address of RAM -- an address in the window, as the address
 * of a static or stack variable of the kernel is, or one in the pool, as the
 * buffer of a program is -- into the physical address that a DMA-capable
 * device (such as a virtio-mmio transport) must be given instead.  Devices
 * are not behind the MMU, so they only ever see physical addresses.  Not
 * meaningful for peripheral MMIO addresses (the result is 0).
 */
ULONG virt_to_phys(const void *va);

#if CONF_WITH_PROCESS_ASPACES
/* Before a reset (panicasm.S): the tree with the identity mapping is loaded again. */
void machine_reset_prepare(void);
#endif

/* The pool address (lower half) of the byte after the kernel image, whose
 * address in the window is given: the pool starts there (bios/biosmem.c). */
UBYTE *virt_pool_start(UBYTE *kernel_end);

#endif /* __ASSEMBLER__ */

#endif /* MACHINE_VIRT_M68K */

#endif /* VIRT_MMU_H */
