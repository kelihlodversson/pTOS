/*
 * cartstub.S - ROM that only hands over to the cartridge
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * A complete operating system lives on a diagnostic cartridge (for
 * example the TARGET_CART image of pTOS), and this is all the ROM the
 * machine needs: the vectors the CPU reads at reset, and the same
 * hand-over to the cartridge that startup.S makes, with the same
 * contract.  The cartridge gets control with the machine as the reset
 * left it (no memory configured yet) and a return address in a6.
 *
 * There is no operating system to fall back on.  When the cartridge is
 * missing, or returns, the screen border flashes red.
 */

        .equ    cart_base, 0x00fa0000  // Base of ROM port cartridge
        .equ    cart_magic, 0xfa52235f // diagnostic cartridge magic number
        .equ    palette0, 0x00ff8240   // shifter colour 0, the border colour

        .text

/*
 * OS header with the layout of a TOS ROM, as in startup.S.  At reset the CPU
 * reads its initial SSP and PC from the first two longs of the ROM, which
 * shadow address 0: bra.s plus the version word act as the SSP, reseth is
 * the PC.  Emulators identify what a ROM is for by the version word, and
 * check that os_beg is the address the ROM is mapped at.
 */
        .equ    rom_base, 0x00fc0000

os_header:
        bra.s   main
        .dc.w   0x0104          // os_version: TOS version
        .dc.l   main            // reseth: reset handler
        .dc.l   rom_base        // os_beg: base of the OS
        .dc.l   rom_base        // os_end: no BSS, no RAM used
        .dc.l   main            // os_res1
        .dc.l   0               // os_magic: no GEM
        .dc.l   0x10282026      // os_date: BCD
        .dc.w   0               // os_conf: US, NTSC
        .dc.w   0               // os_dosdate
        .dc.l   0               // os_root
        .dc.l   0               // os_kbshift
        .dc.l   0               // os_run
        .ascii  "ETOS"          // as in startup.S

main:
        move.w  #0x2700,sr      // interrupts off, supervisor mode
        reset                   // reset the peripherals

        cmp.l   #cart_magic,cart_base   // is a diagnostic cartridge present?
        jne     nocart

        lea     nocart(pc),a6   // return address
        jmp     cart_base+4     // execute the cartridge

nocart:
        lea     palette0,a0
flash:
        move.w  #0x0700,(a0)    // red
        bsr.s   delay
        move.w  #0x0000,(a0)    // black
        bsr.s   delay
        bra.s   flash

delay:
        move.l  #0x20000,d0
1:      subq.l  #1,d0
        bne.s   1b
        rts
