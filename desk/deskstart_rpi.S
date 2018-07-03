/*
 * deskstart.S - assembler startup code of the desktop
 *
 * Copyright (C) 1987 Digital Research Inc.
 *               2001 Lineo, Inc.
 *               2002-2017 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"
#include "../aes/asmstruct.h"

#define DESKSTACKSIZE   2048    /* in bytes: must be multiple of 4 */

        .globl  _deskstart
#if CONF_DEBUG_DESK_STACK
        .globl  _deskstackbottom
#endif

        .extern _deskmain

        .text

_deskstart:
        // our basepage address is in r0.

        // set our temporary user stack
        ldr     sp, =deskustack

        // mshrink to the needed size for TEXT+DATA+BSS plus 256 bytes.
        ldr     r3, [r0, #PD_p_tlen]
        ldr     r2, [r0, #PD_p_dlen]
        add     r3, r3, r2
        ldr     r2, [r0, #PD_p_blen]
        add     r3, r3, r2
        add     r3, r3, #SIZEOF_PD
        mov     r2, r0
        mov     r1, #0
        mov     r0, #0x4a           // MSrink(0, basepage, newsize)
        svc     1


        bl      _deskmain               // Run the desktop

        mov     r0, #0                  // Pterm
        svc     1

.bss

// Space for the Stack:
_deskstackbottom:
        .ds.b   DESKSTACKSIZE
deskustack:
