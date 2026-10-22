/*
 * setjmp.S - EmuTOS's own copy of an ANSI standard feature
 *
 * Copyright (C) 2002-2018 The EmuTOS development team
 * Copyright (C) 2018-2026 The pTOS development team
 *
 * Authors:
 *  LVL   Laurent Vogel
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#include "asmdefs.h"

/*
 * structure of jmp_buf:
 *   0: 11 callee-preserved core registers.
 *  44: with the hard-float ABI, the 8 callee-preserved VFP registers
 *      d8-d15 too: code compiled for it may keep values in them, and a
 *      longjmp() past the frames that saved them would lose those values
 *      (see include/setjmp.h for the size).
 */
        .globl  C_SYM(setjmp)
        .globl  C_SYM(longjmp)

        .text


/* --------------------------------------------------------------------
                 int setjmp (jmp_buf);
   -------------------------------------------------------------------- */
.balign 4
C_SYM(setjmp):

    /* Save all the callee-preserved registers into the jump buffer.  */
    stmia       a1!, { v1-v7, fp, ip, sp, lr }
#ifdef __ARM_PCS_VFP
    vstmia      a1, { d8-d15 }
#endif
    /* When setting up the jump buffer return 0.  */
    mov         a1, #0

    bx lr

/* --------------------------------------------------------------------
        volatile void longjmp (jmp_buf, int);
   -------------------------------------------------------------------- */
C_SYM(longjmp):

    mov         a3, a1
    /* Put the return value into the integer result register.
       But if it is zero then return 1 instead.  */
    movs        a1, a2
    moveq       a1, #1
#ifdef __ARM_PCS_VFP
    add         a4, a3, #44
    vldmia      a4, { d8-d15 }
#endif
    /* Restore the registers, retrieving the state when setjmp() was called. (Assigning the saved lr directly to pc causing a return)  */
    ldmia       a3, { v1-v7, fp, ip, sp, pc }
