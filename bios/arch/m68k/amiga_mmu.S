/*
 * amiga_mmu.S - the static 68030 MMU tree of the higher-half Amiga ROM
 * (CONF_AMIGA_HIGHER_HALF, #737; doc/mmu-address-space.txt)
 *
 * The Amiga counterpart of atari_mmu.S, and built the same way: the tree is in
 * the ROM, so that the MMU can be switched on by the first instructions of
 * startup.S, before the RAM is configured.  Every descriptor has the used (and,
 * for pages, modified) bit set already, so that the MMU never has to write one
 * back, which it cannot do in a ROM.
 *
 * TC: PS = 12 (4 KiB pages), IS = 0, TIA = 7, TIB = 7, TIC = 6, TID = 0.  A
 * root table (a) of 128 entries of 32 MiB, a table (b) of 128 entries of 256
 * KiB and a table (c) of 64 pages.  The physical memory is the address space of
 * the Amiga; what a 32-bit CPU finds above the 24 bits (the Zorro III boards
 * and RAM, the motherboard and processor slot RAM, the Zorro III autoconfig
 * space) is probed by the ROM and answered by the bus, not by a bus error, so
 * it is mapped as it is, not cached:
 *
 *   virtual                       physical        cache  user
 *   0x00000000-0x00000fff         the same        yes    no   (the TOS low memory's
 *                                                            first page: vectors, sysvars, stack)
 *   0x00001000-0x009fffff         the same        yes    yes  (chip RAM and Zorro II RAM)
 *   0x00a00000-0x00efffff         the same        no     no   (the CIAs, the custom chips, the clock,
 *                                                            the Zorro II I/O area and the autoconfig
 *                                                            space, the ranges of the slow RAM)
 *   0x00f00000-0x00ffffff         the same        yes    yes  (the ROM at 0x00fc0000)
 *   0x01000000-0x7fffffff         the same        no     yes  (the RAM and boards of a 32-bit Amiga)
 *   0xfe000000-0xffffffff         the same        no     no   (the Zorro III autoconfig space)
 *   0x80000000-0x80ffffff         0x00000000-     as the first ranges  (the window)
 *
 * Nothing else is mapped.  TT0 and TT1 are off.
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"

#if CONF_AMIGA_HIGHER_HALF

        .globl  C_SYM(amiga_mmu_root)
        .globl  C_SYM(amiga_mmu_tc)
        .globl  C_SYM(amiga_mmu_crp)

// the first long of a long-format descriptor: no limit (0x7fff), the bits
// FreeMiNT sets in a working 68030 tree, and used (and modified) set
DT_PAGE         = 1
DT_TABLE        = 3             // valid, 8-byte entries
D_WP            = 0x04
D_USED          = 0x08
D_MOD           = 0x10
D_CI            = 0x40          // page descriptors only
D_SUPER         = 0x100
D_BASE          = 0x7fff0000 + 0xfc00

PAGE            = D_BASE + DT_PAGE + D_USED + D_MOD
PAGE_SUPER      = PAGE + D_SUPER
PAGE_IO         = PAGE + D_SUPER + D_CI
PAGE_CI         = PAGE + D_CI
TABLE           = D_BASE + DT_TABLE + D_USED

WINDOW          = 0x80000000

// A section of its own: the tables must be on 16-byte boundaries, which the
// SUBALIGN of the linker script would not let a .balign in .text do (emutos.ld)
        .section .mmutree,"a"
        .balign 16

C_SYM(amiga_mmu_tc):
        .dc.l   0x80c07760      // E, PS = 12, IS = 0, TIA/TIB/TIC/TID = 7/7/6/0
// the root pointer: no limit, 8-byte descriptors, the table at its physical address
C_SYM(amiga_mmu_crp):
        .dc.l   0x7fff0003, amiga_mmu_a - WINDOW

        .balign 16
// (a) 128 entries of 32 MiB
C_SYM(amiga_mmu_root):
amiga_mmu_a:
        .dc.l   TABLE, amiga_mmu_b - WINDOW             // 0x00000000: the 24-bit address space
        .set    i,1
        .rept   63                                      // 0x02000000-0x7fffffff: as it is
        .dc.l   PAGE_CI, i << 25
        .set    i,i+1
        .endr
        .dc.l   TABLE, amiga_mmu_b - WINDOW             // 0x80000000: the same, in the window
        .rept   62                                      // 0x82000000-0xfdffffff: nothing
        .dc.l   0, 0
        .endr
        .dc.l   PAGE_IO, 127 << 25                      // 0xfe000000-0xffffffff: as it is

// (b) 128 entries of 256 KiB: the first 32 MiB
        .balign 16
amiga_mmu_b:
        .dc.l   TABLE, amiga_mmu_c - WINDOW             // 0x00000000-0x0003ffff
        .set    i,1
        .rept   39                                      // 0x00040000-0x009fffff: RAM
        .dc.l   PAGE, i << 18
        .set    i,i+1
        .endr
        .rept   20                                      // 0x00a00000-0x00efffff: I/O
        .dc.l   PAGE_IO, i << 18
        .set    i,i+1
        .endr
        .rept   4                                       // 0x00f00000-0x00ffffff: the ROM
        .dc.l   PAGE, i << 18
        .set    i,i+1
        .endr
        .rept   64                                      // 0x01000000-0x01ffffff: as it is
        .dc.l   PAGE_CI, i << 18
        .set    i,i+1
        .endr

// (c) 64 pages of 4 KiB: the first 256 KiB
        .balign 16
amiga_mmu_c:
        .dc.l   PAGE_SUPER, 0                           // the first page: not for a program
        .set    i,1
        .rept   63
        .dc.l   PAGE, i << 12
        .set    i,i+1
        .endr

#endif /* CONF_AMIGA_HIGHER_HALF */
