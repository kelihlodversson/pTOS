/*
 * setjmp.h - EmuTOS own copy of an ANSI standard header
 *
 * Copyright (c) 2002 EmuTOS development team
 *
 * Authors:
 *  LVL   Laurent Vogel
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef H_SETJMP_
#define H_SETJMP_

#include "portab.h"
 
typedef long jmp_buf[13];

int setjmp(jmp_buf state);
void longjmp(jmp_buf state, WORD value) NORETURN;

#endif /* H_SETJMP_ */
