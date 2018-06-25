/*
 * setjmp_rpi.S - EmuTOS's own copy of an ANSI standard feature
 *
 * Copyright (C) 2002-2017 The EmuTOS development team
 *
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#include "asmdefs.h"

/*
 * structure of jmp_buf:
 *   0: 11 callee-preserved registers.
 */
        .globl  _setjmp
        .globl  _longjmp

        .text


/* --------------------------------------------------------------------
                 int setjmp (jmp_buf);
   -------------------------------------------------------------------- */
.balign 4
_setjmp:

	/* Save all the callee-preserved registers into the jump buffer.  */
	stmia		a1, { v1-v7, fp, ip, sp, lr }
	/* When setting up the jump buffer return 0.  */
	mov		a1, #0

	bx lr

/* --------------------------------------------------------------------
		volatile void longjmp (jmp_buf, int);
   -------------------------------------------------------------------- */
_longjmp:

    mov         a3, a1
	/* Put the return value into the integer result register.
	   But if it is zero then return 1 instead.  */
	movs		a1, a2
	moveq		a1, #1
	/* Restore the registers, retrieving the state when setjmp() was called. (Assigning the saved lr directly to pc causing a return)  */
	ldmia		a3, { v1-v7, fp, ip, sp, pc }
