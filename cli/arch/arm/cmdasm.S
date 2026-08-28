/*
 * cmdasm.S - assembler startup code of EmuCON
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * Authors:
 *  RFB    Roger Burrows
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"

#define COMASTACKSIZE   4096    /* in bytes: must be multiple of 4 */

/* PD field offsets from include/pd.h, hardcoded rather than pulled from
 * aes/asm_struct_gen.h: that header is only generated when CONF_WITH_AES
 * is set (its generator needs AES_STACK_SIZE), but EmuCON must also build
 * with the AES left out.  cli/cmdasm.S (m68k) hardcodes the same offsets
 * for the same reason. */
#define PD_p_tlen       0x0c
#define PD_p_dlen       0x14
#define PD_p_blen       0x1c
#define PD_p_env        0x2c
#define SIZEOF_PD       0x100

        .globl  _coma_start
        .globl  _environment
        .extern _cmdmain

        .text

_coma_start:
        // Our basepage address is in r0.

        // Set our temporary user stack.
        ldr     sp, =comaustack

        // Save the environment string pointer from the basepage, before
        // r0 gets reused below for the Mshrink() call.
        ldr     r1, [r0, #PD_p_env]
        ldr     r2, =_environment
        str     r1, [r2]

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

_environment: // Environment string, from the BASEPAGE
        .space  4

        // Space for the stack:
        .align 4
        .space  COMASTACKSIZE
comaustack:
