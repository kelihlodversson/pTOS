/*
 * intmask.S - saving and restoring the interrupt mask
 *
 * Copyright 2002-2017, The EmuTOS development team
 *           1999, Caldera Thin Clients, Inc.
 *           1987, Digital Research Inc.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*
 * These lived in aes/arch/m68k/gemdosif.S while the AES was their only
 * caller.  The VDI calls them as well now, in vdi_misc.c and vdi_mouse.c,
 * where they took the place of set_sr() because set_sr() manipulates the
 * m68k status register and has no meaning on ARM.  The VDI is built even in
 * the configurations that leave the AES out, such as the diagnostic
 * cartridge, so the definitions have to come from something always built:
 * the BIOS.
 *
 * There is a single save slot, so these do not nest.
 */

        .globl  _disable_interrupts
        .globl  _enable_interrupts

        .text

/* disable interrupts */
_disable_interrupts:
#ifdef __mcoldfire__
        move.l  d0,-(sp)
        move.w  sr,d0
        move.w  d0,savesr
        ori.l   #0x0700,d0
        move.w  d0,sr
        move.l  (sp)+,d0
#else
        move    sr,savesr
        ori     #0x0700,sr
#endif
        rts

/* restore interrupt mask as it was before disable_interrupts() */
_enable_interrupts:
#ifdef __mcoldfire__
        move.l  d0,-(sp)
        move.w  savesr,d0
        move.w  d0,sr
        move.l  (sp)+,d0
#else
        move    savesr,sr
#endif
        rts

        .bss

savesr:
        .ds.w   1
