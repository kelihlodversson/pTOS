/*
 * startup.S - ARM startup code for standalone test programs
 *
 * Receives the basepage in r0, sets up a stack, calls Mshrink(),
 * then calls main() with no arguments.  On return, calls Pterm0().
 *
 * Reuses the same basepage layout and GEMDOS calling convention as
 * cli/arch/arm/cmdasm.S (EmuCON) and util/arch/m68k/minicrt.S.
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"

#define STACK_SIZE  4096

/* PD field offsets, matching util/arch/m68k/minicrt.S */
#define PD_p_tlen   0x0c
#define PD_p_dlen   0x14
#define PD_p_blen   0x1c
#define SIZEOF_PD   0x100

#define MSHRINK     0x4a

        .globl  _start
        .extern _main

        .text

_start:
        ldr     sp, =ustack

        /* Mshrink to TEXT+DATA+BSS+basepage */
        ldr     r3, [r0, #PD_p_tlen]
        ldr     r2, [r0, #PD_p_dlen]
        add     r3, r3, r2
        ldr     r2, [r0, #PD_p_blen]
        add     r3, r3, r2
        add     r3, r3, #SIZEOF_PD
        mov     r2, r0
        mov     r1, #0
        mov     r0, #MSHRINK
        svc     1

        bl      _main

        mov     r0, #0                  /* Pterm0() */
        svc     1

        .bss
        .align 4
        .space  STACK_SIZE
ustack:
