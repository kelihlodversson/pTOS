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
#include "../../../aes/asmstruct.h"

#define DESKSTACKSIZE   2048    /* in bytes: must be multiple of 4 */

        .globl  C_SYM(deskstart)
#if CONF_DEBUG_DESK_STACK
        .globl  C_SYM(deskstackbottom)
#endif

        .extern C_SYM(deskmain)

        .text

C_SYM(deskstart):
#if CONF_EXTERNAL_DESK && CONF_WITH_ARM_SHARED_EXEC
        // Standard ELF argc/argv/envp followed by the auxiliary vector.
        ldr     r0, [sp]
        add     r2, sp, #8
        add     r2, r2, r0, lsl #2
        ldr     r1, =C_SYM(desk_environment)
        str     r2, [r1]
1:      ldr     r1, [r2], #4
        cmp     r1, #0
        bne     1b
2:      ldmia   r2!, {r0, r1}
        cmp     r0, #0x5000            // AT_PTOS_BASEPAGE
        moveq   r0, r1
        beq     3f
        cmp     r0, #0
        bne     2b
        mov     r0, #0x4c
        mov     r1, #1
        svc     1
3:
#endif
        // The basepage is in r0.
#if CONF_EXTERNAL_DESK
        ldr     r1, =C_SYM(desk_basepage)       // for the environment (LANG)
        str     r0, [r1]
#endif

        // set our temporary user stack
        ldr     sp, =deskustack

#if !(CONF_EXTERNAL_DESK && CONF_WITH_ARM_SHARED_EXEC)
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
#endif

        bl      C_SYM(deskmain)               // Run the desktop

        mov     r0, #0                  // Pterm
        svc     1

.bss

// Space for the Stack:
.align 4
C_SYM(deskstackbottom):
        .space   DESKSTACKSIZE
deskustack:
