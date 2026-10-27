/*
 * atari_mmu.S - the static 68030 MMU tree of the higher-half Atari ROM
 * (CONF_ATARI_HIGHER_HALF, #726; doc/mmu-address-space.txt)
 *
 * The tree is in the ROM, so that the MMU can be switched on by the first
 * instructions of startup.S, before the RAM is configured: nothing is built at
 * run time.  Every descriptor has the used (and, for pages, modified) bit set
 * already, so that the MMU never has to write one back, which it cannot do in
 * a ROM.
 *
 * TC: PS = 12 (4 KiB pages), IS = 0, TIA = 7, TIB = 7, TIC = 6, TID = 0.  A
 * root table (a) of 128 entries of 32 MiB, tables (b) of 128 entries of 256
 * KiB and a table (c) of 64 pages.  Page descriptors of the upper levels
 * map their whole range (early termination).  Long-format descriptors, which
 * have the S bit.
 *
 *   virtual                       physical        cache  user
 *   0x00000000-0x00000fff         the same        yes    no   (the TOS low memory's
 *                                                            first page: vectors, sysvars, stack)
 *   0x00001000-0x00efffff         the same        yes    yes  (ST-RAM, the ROM at 0x00e00000)
 *   0x00f00000-0x00ffffff         the same        no     no   (the I/O area, 24 bits)
 *   0x01000000-0x7fffffff         the same        yes    yes  (TT-RAM and the like)
 *   0x80000000-0x81ffffff         0x00000000-     as the range above
 *   0x82000000-0x9fffffff         0x02000000-     yes    yes  (TT-RAM, in the window)
 *   0xff000000-0xffffffff         0x00000000-     no     no   (the I/O area, as the ST sees it
 *                                                            at 0xffff8000: an alias)
 *
 * Nothing else is mapped.  TT0 and TT1 are off.
 *
 * With CONF_WITH_PROCESS_ASPACES (#727) every page above is supervisor-only,
 * and the tree is only what the machine starts with, what a reset needs back
 * and, in its upper half (root entries 64-127), the kernel half that
 * aspace_hw030.c copies into the root of every address space; the user half
 * of a process is its own.
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"

#if CONF_ATARI_HIGHER_HALF && !CONF_ATARI_HIGHER_HALF_68040

        .globl  C_SYM(atari_mmu_root)
        .globl  C_SYM(atari_mmu_tc)
        .globl  C_SYM(atari_mmu_crp)

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

#if CONF_WITH_PROCESS_ASPACES
// With an address space for every process (#727) this tree is the boot tree
// and the source of the kernel half of every other one (aspace_hw030.c): all
// of it is the kernel's, none of it is for a program
PAGE            = D_BASE + DT_PAGE + D_USED + D_MOD + D_SUPER
PAGE_SUPER      = PAGE                  // (the bit is added once: twice would carry out of it)
PAGE_IO         = PAGE + D_CI
#else
PAGE            = D_BASE + DT_PAGE + D_USED + D_MOD
PAGE_SUPER      = PAGE + D_SUPER
PAGE_IO         = PAGE + D_SUPER + D_CI
#endif
TABLE           = D_BASE + DT_TABLE + D_USED

WINDOW          = 0x80000000

// A section of its own: the tables must be on 16-byte boundaries, which the
// SUBALIGN of the linker script would not let a .balign in .text do (emutos.ld)
        .section .mmutree,"a"
        .balign 16

C_SYM(atari_mmu_tc):
        .dc.l   0x80c07760      // E, PS = 12, IS = 0, TIA/TIB/TIC/TID = 7/7/6/0
// the root pointer: no limit, 8-byte descriptors, the table at its physical address
C_SYM(atari_mmu_crp):
        .dc.l   0x7fff0003, atari_mmu_a - WINDOW

        .balign 16
// (a) 128 entries of 32 MiB
C_SYM(atari_mmu_root):
atari_mmu_a:
        .dc.l   TABLE, atari_mmu_b0 - WINDOW            // 0x00000000: ST-RAM, the ROM, the I/O
        .set    i,1
        .rept   63                                      // 0x02000000-0x7fffffff
        .dc.l   PAGE, i << 25
        .set    i,i+1
        .endr
        .dc.l   TABLE, atari_mmu_b0 - WINDOW            // 0x80000000: the same, in the window
        .set    i,1
        .rept   15                                      // 0x82000000-0x9fffffff
        .dc.l   PAGE, i << 25
        .set    i,i+1
        .endr
        .rept   47                                      // 0xa0000000-0xfdffffff: nothing
        .dc.l   0, 0
        .endr
        .dc.l   TABLE, atari_mmu_b1 - WINDOW            // 0xfe000000: the I/O area

// (b0) 128 entries of 256 KiB: the first 32 MiB
        .balign 16
atari_mmu_b0:
        .dc.l   TABLE, atari_mmu_c0 - WINDOW            // 0x00000000-0x0003ffff
        .set    i,1
        .rept   59                                      // 0x00040000-0x00efffff
        .dc.l   PAGE, i << 18
        .set    i,i+1
        .endr
        .rept   4                                       // 0x00f00000-0x00ffffff: I/O
        .dc.l   PAGE_IO, i << 18
        .set    i,i+1
        .endr
        .rept   64                                      // 0x01000000-0x01ffffff
        .dc.l   PAGE, i << 18
        .set    i,i+1
        .endr

// (b1) 128 entries of 256 KiB: 0xfe000000-0xffffffff
        .balign 16
atari_mmu_b1:
        .rept   64                                      // 0xfe000000-0xfeffffff: nothing
        .dc.l   0, 0
        .endr
        .set    i,0
        .rept   64                                      // 0xff000000-0xffffffff: the I/O area
        .dc.l   PAGE_IO, i << 18
        .set    i,i+1
        .endr

// (c0) 64 pages of 4 KiB: the first 256 KiB
        .balign 16
atari_mmu_c0:
        .dc.l   PAGE_SUPER, 0                           // the first page: not for a program
        .set    i,1
        .rept   63
        .dc.l   PAGE, i << 12
        .set    i,i+1
        .endr

#endif /* CONF_ATARI_HIGHER_HALF && !CONF_ATARI_HIGHER_HALF_68040 */
