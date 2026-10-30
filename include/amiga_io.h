/*
 * amiga_io.h - where the Amiga I/O is, for the kernel (#737)
 *
 * The custom chips, the CIAs, the clock, Gayle and the Zorro II autoconfig
 * space are at 0x00a00000-0x00efffff.  In an image that runs in the upper half
 * (CONF_AMIGA_HIGHER_HALF, doc/mmu-address-space.txt) the kernel reaches them
 * by a window of its own in the kernel half, the same range at AMIGA_IO_VBASE,
 * supervisor-only and not cached: so that the lower half of an address space,
 * which belongs to the process, has no I/O in it.  AMIGA_IO(address) is the
 * address an access of the kernel is made at: the physical address itself in
 * the images that are not.  (Written for C and for the assembler: no suffix on
 * the numbers.)
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef AMIGA_IO_H
#define AMIGA_IO_H

#if CONF_AMIGA_HIGHER_HALF
#define AMIGA_IO_VBASE      0xf0000000
#define AMIGA_IO_FIRST      0x00a00000      /* the range that the window has */
#define AMIGA_IO_LAST       0x00efffff
#define AMIGA_IO(address)   ((address) + AMIGA_IO_VBASE)
/* an address of a board, which may be in the I/O range, or in the RAM or the ROM */
#define AMIGA_IO_ADDR(address) \
    (((address) >= AMIGA_IO_FIRST && (address) <= AMIGA_IO_LAST) ? AMIGA_IO(address) : (address))
/* the address on the bus of an address of the kernel in the window of the I/O area (what a board sees) */
#define AMIGA_IO_BUS(address) \
    (((address) >= AMIGA_IO(AMIGA_IO_FIRST) && (address) <= AMIGA_IO(AMIGA_IO_LAST)) ? \
     (address) - AMIGA_IO_VBASE : (address))
#else
#define AMIGA_IO(address)   (address)
#define AMIGA_IO_ADDR(address) (address)
#define AMIGA_IO_BUS(address)  (address)
#endif

#endif /* AMIGA_IO_H */
