/*
 * memory.S - memory initialization
 *
 * Copyright (C) 2001-2017 by Authors:
 *
 * Authors:
 *  MAD  Martin Doering
 *  PES  Petr Stehlik
 *  jfdn Jean-François DEL NERO
 *  VRI  Vincent Rivière
 *  RFB  Roger Burrows
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"
#include "memory.h"

// ==== Definitions ==========================================================

        .globl  meminit                 // memory initialization

// ==== References ===========================================================

        .extern _warm_magic
        .extern _stktop

        // functions for detecting hardware
        .extern _check_read_byte
        .extern _bzero

        // ST-RAM settings
        .extern memctrl
        .extern _phystop
        .extern memvalid
        .extern memval2
        .extern memval3

        // TT-RAM settings
        .extern _ramtop
        .extern _ramvalid

        // Overall memory map
        .extern sysvars_start // start of system variables
        .extern sysvars_end   // end of system variables
        .extern __bss         // start of BSS segment
        .extern __ebss        // end of BSS segment

// ===========================================================================
// ==== meminit - Reuse or detect RAM settings, then initialize RAM areas ====
// ===========================================================================
meminit:

        // We will end up moving sp, so we save away lr into r11
        mov r11, lr

        /* We will accumulate meminit_flags in this register */
        mov r7,  #0                  // _meminit_flags = 0


// ===========================================================================
// ==== memchk - Check if old memory config can be used ======================
// ===========================================================================
// Inputs:
//   r7.b: _meminit_flags (input/output)
// Outputs:
//   r6.b: old memctrl
//   r5.l: old _phystop
//   r3.l: old _ramvalid (#if CONF_WITH_TTRAM)
//   r4.l: old _ramtop (#if CONF_WITH_TTRAM)
memchk:

        ldr	r1,	=0x752019f3
        ldr     r0, =memvalid
        ldr     r0, [r0]
        cmp     r1, r0
        bne     memconf

        ldr	r1,	=0x237698aa
        ldr     r0, =memval2
        ldr     r0, [r0]
        cmp     r1, r0
        bne     memconf

        ldr	r1,	=0x5555aaaa
        ldr     r0, =memval3
        ldr     r0, [r0]
        cmp     r1, r0
        bne     memconf

        // The old RAM settings are valid, load them into registers.
        // They will stay in safe place in case we clear the sysvars.
        ldr     r6, =memctrl
        ldrb    r6, [r6]

        ldr     r5, =_phystop
        ldr     r5, [r5]

// ===========================================================================
// ==== The ST-RAM configuration is valid ====================================
// ===========================================================================
// Inputs:
//   r7.b: _meminit_flags (input/output)
//   r6.b: old memctrl
//   r5.l: old _phystop
//   r3.l: old _ramvalid (#if CONF_WITH_TTRAM)
//   r4.l: old _ramtop (#if CONF_WITH_TTRAM)

memory_reused:
#if CONF_DETECT_FIRST_BOOT_WITHOUT_MEMCONF
        // If _warm_magic == WARM_MAGIC, this is explicitly not a first boot
        ldr	r1,	=0x5741524D         // WARM_MAGIC
        ldr     r0, =_warm_magic
        ldr     r0, [r0]
        cmp     r1, r0
        beq     .warm_magic_done        // Indeed, this is not a first boot

        // _warm_magic != WARM_MAGIC: this is a first boot
        orr     r7,r7,#MEMINIT_FIRST_BOOT
.warm_magic_done:
#endif
        b     stram_detected

// ===========================================================================
// ==== memconf - Detect RAM and configure the memory controller =============
// ===========================================================================
// Inputs:
//   r7.b: _meminit_flags (input/output)
// Outputs:
//   r6.b: new value of 0xffff8001 (maybe fake), to put into memctrl.
//   r5.l: detected end of the ST-RAM, to put into _phystop.
memconf:
        ldr	r6,	=0xffff8001

        // TODO: Hard coded RAM size for now assuming 256Mb with 64Mb for GPU
        ldr r5, =(0x100000 * (256-64))

memconf_done:
        // Since the memory was not valid, this is a first boot
        orr     r7, r7, #MEMINIT_FIRST_BOOT

// ===========================================================================
// ==== End of ST-RAM detection ==============================================
// ===========================================================================
/*
 * At this point:
 * - The size of the ST-RAM has been detected
 * - The MMU has been properly initalized
 * - The ST-RAM is ready to be used
 *
 * r7.b = _meminit_flags
 * r5.l = new phystop
 * r6.b = new memctrl
 * r3.l = old _ramvalid (#if CONF_WITH_TTRAM)
 * r4.l = old _ramtop (#if CONF_WITH_TTRAM)
 */
stram_detected:
        // The previous temporary stack pointer may have moved
        // during early detections, so we reinitialize it now.
        // After that, normal stack usage is allowed.
        ldr     sp, =_stktop

        tst     r7, #MEMINIT_FIRST_BOOT // first boot ?
        beq     any_boot                // no: skip first boot initialization

// ===========================================================================
// ==== This part is executed only on first boot =============================
// ===========================================================================

        /* Clear the sysvars */
        ldr     r0, =sysvars_start
        ldr     r1, =sysvars_end
        bl      _bzero

        /* Store the ST-RAM parameters in the ST-RAM itself */
        ldr     r0, =memctrl
        strb    r6, [r0]
        ldr     r0, =_phystop
        str     r5, [r0]

        /* Make ST-RAM configuration valid */
        ldr     r0, =memvalid
        ldr	r1,	=0x752019f3
        str     r1, [r0]

        ldr     r0, =memval2
        ldr	r1,	=0x237698aa
        str     r1, [r0]

        ldr     r0, =memval3
        ldr	r1,	=0x5555aaaa
        str     r1, [r0]

#if CONF_DETECT_FIRST_BOOT_WITHOUT_MEMCONF
        ldr     r0, =_warm_magic
        ldr	r1,	=0x5741524D
        str     r1, [r0]
#endif


// ===========================================================================
// ==== This part is executed on any boot (first boot or reset) ==============
// ===========================================================================
any_boot:
        // Clear the BSS segment.
        // Our stack is explicitly set outside the BSS, so this is safe:
        // bzero() will be able to return.
        ldr     r0, =__bss
        ldr     r1, =__ebss
        sub     r1, r1, r0
        bl      _bzero

        bx      r11 // return from meminit
