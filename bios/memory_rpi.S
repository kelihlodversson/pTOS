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

        .extern _stktop

        // functions for detecting hardware
        .extern _check_read_byte
        .extern _bzero

        // ST-RAM settings
        .extern _phystop

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
// ==== memconf - Detect RAM and configure the memory controller =============
// ===========================================================================
// Inputs:
//   r7.b: _meminit_flags (input/output)
// Outputs:
//   r5.l: detected end of the ST-RAM, to put into _phystop.
memconf:
        // TODO: Hard coded RAM size for now assuming 256Mb with 64Mb for GPU
        ldr r5, =(0x100000 * (256-64))


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
 */
stram_detected:
        // The previous temporary stack pointer may have moved
        // during early detections, so we reinitialize it now.
        // After that, normal stack usage is allowed.
        ldr     sp, =_stktop

        /* Clear the sysvars */
        ldr     r0, =sysvars_start
        ldr     r1, =sysvars_end
        bl      _bzero

        /* Store the ST-RAM parameters in the ST-RAM itself */
        ldr     r0, =_phystop
        str     r5, [r0]

        // Clear the BSS segment.
        // Our stack is explicitly set outside the BSS, so this is safe:
        // bzero() will be able to return.
        ldr     r0, =__bss
        ldr     r1, =__ebss
        sub     r1, r1, r0
        bl      _bzero

        bx      r11 // return from meminit
