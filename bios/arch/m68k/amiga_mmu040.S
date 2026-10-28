/*
 * amiga_mmu040.S - the static 68040 and 68060 MMU tree of the higher-half
 * Amiga ROM (CONF_AMIGA_HIGHER_HALF_68040, #737; doc/mmu-address-space.txt)
 *
 * The counterpart of amiga_mmu.S for the MMU of a 68040 or a 68060, and of
 * atari_mmu040.S for the Amiga: the tree is in the ROM, so that the MMU can be
 * switched on by the first instructions of startup.S, before the RAM is
 * configured.  The used and modified bits of every descriptor are set already,
 * so that the MMU never has to write one back into the ROM.
 *
 * The 68040 has no early termination: a three level tree of a root table of
 * 128 entries (32 MiB each), pointer tables of 128 entries (256 KiB each) and
 * page tables of 64 pages (4 KiB each, P clear in the TC).  So only the first
 * 16 MiB of the physical memory, the 24-bit address space of the Amiga, are
 * mapped, and they are mapped twice, with the same tables:
 *
 *   virtual                       physical        cache  user
 *   0x00000000-0x00000fff         the same        WT     no
 *   0x00001000-0x009fffff         the same        WT     yes  (chip RAM and Zorro II RAM)
 *   0x00c00000-0x00dbffff         the same        no     yes  (the slow RAM, or a mirror of the
 *                                                            custom chips)
 *   0x00f00000-0x00ffffff         the same        WT     yes  (the ROM)
 *   0x80000000-0x80ffffff         0x00000000-     as above   (the window)
 *   0xf0a00000-0xf0efffff         0x00a00000-     no     no   (the I/O window: PCMCIA, the CIAs, the
 *                                                            custom chips, the clock, Gayle, the Zorro II
 *                                                            I/O area and autoconfig space; bios/amiga_io.h)
 *
 * The I/O is not in the lower half, which belongs to the processes once they
 * have address spaces: the kernel reaches it through its own window, which
 * shares the page tables of the 16 MiB.
 *
 * The Zorro III autoconfig space at 0xff000000-0xffffffff is a transparent
 * window (DTT0, set by startup.S): supervisor-only, serialized, not cached.
 * The RAM above the 24 bits is not mapped (and not probed, bios/amiga.c).
 * ITT0, ITT1 and DTT1 are off.  The RAM is write-through, as the ST-RAM of the
 * Atari is: the chip set reads and writes it behind the data cache.
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"

#if CONF_AMIGA_HIGHER_HALF && CONF_AMIGA_HIGHER_HALF_68040

        .globl  C_SYM(amiga_mmu_root)
        .globl  C_SYM(amiga_mmu_dtt0)

// root and pointer table descriptors: resident (UDT = 2), used
UDT             = 0x0a
// page descriptors: resident (PDT = 1), used, modified, supervisor; the cache
// mode is write-through (CM = 0), or not cached and serialized (CM = 2)
PDT             = 0x01
PTE_U           = 0x08
PTE_M           = 0x10
PTE_CM_NC       = 0x40
PTE_S           = 0x80

PAGE            = PDT + PTE_U + PTE_M
PAGE_SUPER      = PDT + PTE_U + PTE_M + PTE_S
PAGE_IO         = PDT + PTE_U + PTE_M + PTE_S + PTE_CM_NC
PAGE_CI         = PDT + PTE_U + PTE_M + PTE_CM_NC

WINDOW          = 0x80000000

        .section .mmutree,"a"
        .balign 512

// 0xff000000-0xffffffff, supervisor, not cached, serialized: E, S = 01, CM = 10
C_SYM(amiga_mmu_dtt0):
        .dc.l   0xff00a040

        .balign 512
// the root table: 128 entries of 32 MiB
C_SYM(amiga_mmu_root):
        .dc.l   amiga_mmu_ptr - WINDOW + UDT            // 0x00000000: the first 32 MiB
        .rept   63
        .dc.l   0
        .endr
        .dc.l   amiga_mmu_ptr - WINDOW + UDT            // 0x80000000: the same, in the window
        .rept   55
        .dc.l   0
        .endr
        .dc.l   amiga_mmu_io_ptr - WINDOW + UDT         // 0xf0000000: the I/O window
        .rept   7
        .dc.l   0
        .endr

// the pointer table: 128 entries of 256 KiB, 64 of them with a page table
        .balign 512
amiga_mmu_ptr:
        .set    i,0
        .rept   40                                      // 0x00000000-0x009fffff: RAM
        .dc.l   amiga_mmu_pages - WINDOW + 256 * i + UDT
        .set    i,i+1
        .endr
        .rept   8                                       // 0x00a00000-0x00bfffff: (the I/O window)
        .dc.l   0
        .set    i,i+1
        .endr
        .rept   7                                       // 0x00c00000-0x00dbffff: the slow RAM
        .dc.l   amiga_mmu_pages - WINDOW + 256 * i + UDT
        .set    i,i+1
        .endr
        .rept   5                                       // 0x00dc0000-0x00efffff: (the I/O window)
        .dc.l   0
        .set    i,i+1
        .endr
        .rept   4                                       // 0x00f00000-0x00ffffff: the ROM
        .dc.l   amiga_mmu_pages - WINDOW + 256 * i + UDT
        .set    i,i+1
        .endr
        .rept   64
        .dc.l   0
        .endr

// the pointer table of the I/O window: the page tables of the I/O area
        .balign 512
amiga_mmu_io_ptr:
        .rept   40
        .dc.l   0
        .endr
        .set    i,40
        .rept   20                                      // 0x00a00000-0x00efffff
        .dc.l   amiga_mmu_pages - WINDOW + 256 * i + UDT
        .set    i,i+1
        .endr
        .rept   68
        .dc.l   0
        .endr

// the page tables: 64 of 64 pages, 4 KiB each, the first 16 MiB
        .balign 256
amiga_mmu_pages:
        .dc.l   PAGE_SUPER                              // the first page: not for a program
        .set    i,1
        .rept   0xa00 - 1                               // 0x00001000-0x009fffff: RAM
        .dc.l   (i << 12) + PAGE
        .set    i,i+1
        .endr
        .rept   0x200                                   // 0x00a00000-0x00bfffff: I/O
        .dc.l   (i << 12) + PAGE_IO
        .set    i,i+1
        .endr
        .rept   0x1c0                                   // 0x00c00000-0x00dbffff: the slow RAM
        .dc.l   (i << 12) + PAGE_CI
        .set    i,i+1
        .endr
        .rept   0x140                                   // 0x00dc0000-0x00efffff: I/O
        .dc.l   (i << 12) + PAGE_IO
        .set    i,i+1
        .endr
        .rept   0x100                                   // 0x00f00000-0x00ffffff: the ROM
        .dc.l   (i << 12) + PAGE
        .set    i,i+1
        .endr

#endif /* CONF_AMIGA_HIGHER_HALF && CONF_AMIGA_HIGHER_HALF_68040 */
