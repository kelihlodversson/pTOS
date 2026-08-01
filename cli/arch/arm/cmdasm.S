/*
 * cmdasm.S - assembler startup code of EmuCON
 *
 * Copyright (C) 2013-2017 The EmuTOS development team
 *
 * Authors:
 *  RFB    Roger Burrows
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"
#include "../../../aes/asmstruct.h"

#define COMASTACKSIZE   4096    /* in bytes: must be multiple of 4 */

        .globl  _coma_start
        .extern _cmdmain

        .text

_coma_start:
        // Our basepage address is in r0.

        // Set our temporary user stack.
        ldr     sp, =comaustack

        // Mshrink to the needed size for TEXT+DATA+BSS plus 256 bytes.
        ldr     r3, [r0, #PD_p_tlen]
        ldr     r2, [r0, #PD_p_dlen]
        add     r3, r3, r2
        ldr     r2, [r0, #PD_p_blen]
        add     r3, r3, r2
        add     r3, r3, #SIZEOF_PD      // size of basepage itself
        mov     r2, r0
        mov     r1, #0
        mov     r0, #0x4a               // Mshrink(0, basepage, newsize)
        svc     1

        bl      _cmdmain                // run the command interpreter

        mov     r0, #0                  // Pterm0()
        svc     1

        .bss

        // Space for the stack:
        .align 4
        .space  COMASTACKSIZE
comaustack:
