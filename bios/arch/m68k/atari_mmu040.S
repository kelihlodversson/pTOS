/*
 * atari_mmu040.S - the static 68040 MMU tree of the higher-half Atari ROM
 * (CONF_ATARI_HIGHER_HALF_68040, #728; doc/mmu-address-space.txt)
 *
 * The 68040 counterpart of atari_mmu.S: the tree is in the ROM, so that the
 * MMU can be switched on by the first instructions of startup.S, before the
 * RAM is configured.  The used and modified bits of every descriptor are set
 * already, so that the MMU never has to write one back into the ROM.
 *
 * The 68040 has no early termination: a three level tree of a root table of
 * 128 entries (32 MiB each), pointer tables of 128 entries (256 KiB each) and
 * page tables of 64 pages (4 KiB each, P clear in the TC).  So only the first
 * 16 MiB of the physical memory (the ST-RAM of a Falcon or a TT, the ROM at
 * 0x00e00000 and the I/O area that the ST sees at 0x00f00000) are mapped, as
 * the tree of the 68030 does, and they are mapped twice, with the same tables:
 *
 *   virtual                       physical        cache  user
 *   0x00000000-0x00efffff         the same        WT     no with aspaces, else yes
 *   0x00f00000-0x00ffffff         the same        no     no   (the I/O area, 24 bits)
 *   0x80000000-0x80efffff         0x00000000-     as above   (the window)
 *   0x80f00000-0x80ffffff         0x00f00000-     no     no
 *
 * The I/O area at 0xff000000-0xffffffff, where the 16-bit absolute addresses
 * of the ST I/O registers go (0xffff8000 and up), is a transparent window of
 * the supervisor (DTT0, set by startup.S): serialized, not cached.  The first
 * page is not for a program, as in the other tree.  Nothing else is mapped:
 * not the TT-RAM, which a 68040 TT has above 16 MiB: its size is not known when
 * the ROM is built, so aspace_hw_init() (aspace_hw.c) puts it in the window,
 * in a root in the RAM that it copies this one into.  ITT0, ITT1 and DTT1 are off.
 *
 * With CONF_WITH_PROCESS_ASPACES every page is supervisor-only, and the tree
 * is the boot tree and the source of the kernel half of the others
 * (aspace_hw.c copies its upper 64 root entries into every root).
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"

#if CONF_ATARI_HIGHER_HALF && CONF_ATARI_HIGHER_HALF_68040

        .globl  C_SYM(atari_mmu_root)
        .globl  C_SYM(atari_mmu_dtt0)

// root and pointer table descriptors: resident (UDT = 2), used
UDT             = 0x0a
// page descriptors: resident (PDT = 1), used, modified, supervisor; the cache
// mode is write-through (CM = 0), or not cached and serialized (CM = 2)
PDT             = 0x01
PTE_U           = 0x08
PTE_M           = 0x10
PTE_CM_NC       = 0x40
PTE_S           = 0x80

#if CONF_WITH_PROCESS_ASPACES
PAGE            = PDT + PTE_U + PTE_M + PTE_S
#else
PAGE            = PDT + PTE_U + PTE_M
#endif
PAGE_SUPER      = PDT + PTE_U + PTE_M + PTE_S
PAGE_IO         = PDT + PTE_U + PTE_M + PTE_S + PTE_CM_NC

WINDOW          = 0x80000000

        .section .mmutree,"a"
        .balign 512

// 0xff000000-0xffffffff, supervisor, not cached, serialized: E, S = 01, CM = 10
C_SYM(atari_mmu_dtt0):
        .dc.l   0xff00a040

        .balign 512
// the root table: 128 entries of 32 MiB
C_SYM(atari_mmu_root):
        .dc.l   atari_mmu_ptr - WINDOW + UDT            // 0x00000000: the first 32 MiB
        .rept   63
        .dc.l   0
        .endr
        .dc.l   atari_mmu_ptr - WINDOW + UDT            // 0x80000000: the same, in the window
        .rept   63
        .dc.l   0
        .endr

// the pointer table: 128 entries of 256 KiB, 64 of them with a page table
        .balign 512
atari_mmu_ptr:
        .set    i,0
        .rept   64
        .dc.l   atari_mmu_pages - WINDOW + 256 * i + UDT
        .set    i,i+1
        .endr
        .rept   64
        .dc.l   0
        .endr

// the page tables: 64 of 64 pages, 4 KiB each, the first 16 MiB
        .balign 256
atari_mmu_pages:
        .dc.l   PAGE_SUPER                              // the first page: not for a program
        .set    i,1
        .rept   0xf00 - 1                               // 0x00001000-0x00efffff
        .dc.l   (i << 12) + PAGE
        .set    i,i+1
        .endr
        .rept   0x100                                   // 0x00f00000-0x00ffffff: I/O
        .dc.l   (i << 12) + PAGE_IO
        .set    i,i+1
        .endr

#endif /* CONF_ATARI_HIGHER_HALF && CONF_ATARI_HIGHER_HALF_68040 */
