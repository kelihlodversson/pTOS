/*
 * setjmp.h - EmuTOS's own version of the ANSI standard header
 *
 * Copyright (C) 2002-2020 The EmuTOS development team
 *
 * Authors:
 *  LVL   Laurent Vogel
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef SETJMP_H
#define SETJMP_H

/*
 * enough for the callee-saved registers of each architecture's setjmp():
 * m68k 13, x86-64 8, ARM 11, plus d8-d15 (16 words) with the hard-float
 * ABI (util/arch/arm/setjmp.S)
 */
#if defined(__arm__) && defined(__ARM_PCS_VFP)
typedef long jmp_buf[27];
#else
typedef long jmp_buf[13];
#endif

int setjmp(jmp_buf state);
void longjmp(jmp_buf state, WORD value) NORETURN;

#endif /* SETJMP_H */
