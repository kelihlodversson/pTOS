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
        ldr     sp, =_stktop
        bl _raspi_vcmem_init


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
        bx      r11 // return from meminit
