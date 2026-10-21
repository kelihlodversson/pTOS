/*
 * virt_mmu_early.S - the 68040 MMU of the higher-half kernel of QEMU's m68k
 * 'virt' machine: builds the translation tables and switches the MMU on
 * (see virt_mmu.h for what is mapped, and doc/mmu-address-space.txt)
 *
 * Called by startup.S with the MMU off (or on, after a software reset) from
 * the low alias of the RAM, where the kernel has been loaded: the part of
 * the start that runs before the MMU is on is position independent.  Like
 * the code before it, this is in .text, which is linked in the window, so it
 * must not use an absolute address of the kernel -- only literal constants
 * and the pc-relative branches of the assembler.
 *
 * The 68040 has a three level tree: a root table of 128 entries (32 MiB
 * each), pointer tables of 128 entries (256 KiB each) and page tables of 64
 * entries (4 KiB pages).  One tree for the supervisor and the user (SRP and
 * URP are the same); the RAM is in it twice, at 0 and at the window, and the
 * two parts share their pointer tables and page tables.
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"
#include "virt_mmu.h"

#if CONF_WITH_HIGHER_HALF && defined(MACHINE_VIRT_M68K)

        .globl  C_SYM(virt_mmu_early)

/*
 * The 68040 instructions are spelled as words: the file is assembled for the
 * -m68020 the image is built for, which does not know them.
 */
#define MOVEC_D0_URP    .dc.l 0x4e7b0806
#define MOVEC_D0_SRP    .dc.l 0x4e7b0807
#define MOVEC_D1_ITT0   .dc.l 0x4e7b1004
#define MOVEC_D1_ITT1   .dc.l 0x4e7b1005
#define MOVEC_D1_DTT0   .dc.l 0x4e7b1006
#define MOVEC_D1_DTT1   .dc.l 0x4e7b1007
#define VME_CPUSHA_BC   .dc.w 0xf4f8
#define VME_PFLUSHA         .dc.w 0xf518

// descriptor bits
UDT_RESIDENT    = 2             // pointer and root level: valid
PDT_RESIDENT    = 1             // page level: valid; cache mode 0 (write-through)
PTE_SUPER       = 0x80          // supervisor only
PTE_GLOBAL      = 0x400         // the same in every address space

TC_ENABLE       = 0x8000        // E; P clear: 4 KiB pages

// dtt0: the devices, 0xff000000 and up, where they are, for the supervisor
// only (S = 01), not cached and serialized (CM = 10)
DTT0_DEVICES    = 0xff000000 + 0x8000 + 0x2000 + 0x40

ROOT_WINDOW     = 4 * (VIRT_WINDOW_VBASE >> 25)    // the window's root entries

        .text

/*
 * ULONG virt_mmu_early(ULONG ram_size)
 *
 * d0 = the size of the RAM, the bytes of it that are there from physical 0.
 * Returns, in d0, the end of the RAM that is left for the OS, which is the
 * start of the tables: they are at the top of the RAM, and the caller uses
 * the value as phystop.
 */
C_SYM(virt_mmu_early):
        movem.l d2-d7/a2-a4,-(sp)

        // translation off, in case it is on (a software reset), and the
        // transparent windows off
        moveq   #0,d1
        MOVEC_D1_TC
        VME_PFLUSHA
        MOVEC_D1_ITT0
        MOVEC_D1_ITT1
        MOVEC_D1_DTT0
        MOVEC_D1_DTT1

        // d6 = the RAM, in whole pages, and no more than the window holds
        and.l   #-4096,d0
        cmp.l   #VIRT_MAX_RAM_SIZE,d0
        jls     size_ok
        move.l  #VIRT_MAX_RAM_SIZE,d0
size_ok:
        move.l  d0,d6

        move.l  d6,d1
        lsr.l   #8,d1
        lsr.l   #4,d1                   // d1 = pages
        move.l  d1,d2
        add.l   #63,d2
        lsr.l   #6,d2                   // d2 = page tables, 256 bytes each
        move.l  d2,d3
        add.l   #127,d3
        lsr.l   #7,d3                   // d3 = pointer tables, 512 bytes each

        // d4 = the size of the tables: a root table and the others
        move.l  d3,d4
        lsl.l   #8,d4
        lsl.l   #1,d4
        add.l   #512,d4
        move.l  d2,d5
        lsl.l   #8,d5
        add.l   d5,d4

        // at the top of the RAM, on a 512 byte boundary
        move.l  d6,d5
        sub.l   d4,d5
        and.l   #-512,d5
        move.l  d5,a0                   // a0 = root table
        lea     512(a0),a1              // a1 = pointer tables
        move.l  d3,d4
        lsl.l   #8,d4
        lsl.l   #1,d4
        lea     (a1,d4.l),a2            // a2 = page tables
        move.l  d2,d4
        lsl.l   #8,d4
        lea     (a2,d4.l),a3            // a3 = the end of the tables

        // all invalid
        move.l  a0,a4
vme_clear:
        clr.l   (a4)+
        cmp.l   a3,a4
        jcs     vme_clear

        // the pages: the RAM, page n at n * 4096; the first one is the TOS
        // low memory, and supervisor only
        move.l  a2,a4
        moveq   #0,d4
vme_page:
        cmp.l   d6,d4
        jcc     vme_pages_done
        move.l  d4,d5
        or.l    #PDT_RESIDENT+PTE_GLOBAL,d5
        tst.l   d4
        jne     vme_page_set
        or.l    #PTE_SUPER,d5
vme_page_set:
        move.l  d5,(a4)+
        add.l   #4096,d4
        jra     vme_page
vme_pages_done:

        // the pointer tables: one entry for each page table, in order
        move.l  a2,d4
        or.l    #UDT_RESIDENT,d4
        move.l  a1,a4
        move.l  d2,d7
vme_ptr:
        move.l  d4,(a4)+
        add.l   #256,d4
        subq.l  #1,d7
        jne     vme_ptr

        // the root table: each pointer table twice, for the RAM at 0 and in
        // the window
        move.l  a1,d4
        or.l    #UDT_RESIDENT,d4
        move.l  a0,a4
        lea     ROOT_WINDOW(a0),a3
        move.l  d3,d7
vme_root:
        move.l  d4,(a4)+
        move.l  d4,(a3)+
        add.l   #512,d4
        subq.l  #1,d7
        jne     vme_root

        // switch on: one tree for the supervisor and the user
        move.l  a0,d6                   // the result
        move.l  a0,d0
        MOVEC_D0_URP
        MOVEC_D0_SRP
        move.l  #DTT0_DEVICES,d0
        MOVEC_D0_DTT0
        VME_CPUSHA_BC
        VME_PFLUSHA
        nop
        move.l  #TC_ENABLE,d0
        MOVEC_D0_TC
        nop                             // the next instruction is fetched through it

        move.l  d6,d0
        movem.l (sp)+,d2-d7/a2-a4
        rts

#endif /* CONF_WITH_HIGHER_HALF && MACHINE_VIRT_M68K */
