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
        // our basepage address is in r0. Copy it to r11 so it won't get clobbered by other calls below
        mov     r11, r0

        // set our temporary user stack
        ldr     sp, =deskustack

        // mshrink to the needed size for TEXT+DATA+BSS plus 256 bytes.
        ldr     r2, [r11, #PD_p_tlen]
        ldr     r1, [r11, #PD_p_dlen]
        add     r2, r2, r1
        ldr     r1, [r11, #PD_p_blen]
        add     r2, r2, r1
        add     r2, r2, #SIZEOF_PD
        mov     r1, r11
        mov     r0, #0
        mov     r7, #0x4a           // MSrink(0, basepage, newsize)
        svc     1


        bl      _deskmain               // Run the desktop

        mov     r7, #0                  // Pterm
        svc     #1

.bss

// Space for the Stack:
_deskstackbottom:
        .ds.b   DESKSTACKSIZE
deskustack:
