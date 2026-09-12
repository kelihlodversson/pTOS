/*
 * stringasm.S - simple implementation of <string.h>-like routines
 *
 * Copyright (C) 2002-2020 The EmuTOS development team
 *
 * Authors:
 *  LVL   Laurent Vogel
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"

        .globl  C_SYM(strlencpy)
        .globl  C_SYM(strchr)

/*
 * WORD strlencpy(char *RESTRICT dest, const char *RESTRICT src)
 * as strcpy but returns the length of the string instead.
 */

C_SYM(strlencpy):
        /*
         * strlencpy(char *RESTRICT dest, const char *RESTRICT src) has two
         * pointer arguments. Under -mfastcall they already arrive exactly
         * where this code needs them -- dest in a0, src in a1 -- so no
         * register setup is needed at all; only the plain stack ABI below
         * needs to read them off the caller's frame (see the identical
         * fix in aes/arch/m68k/gemasm.S's _psetup/_switchto).
         */
#if !CONF_WITH_MFASTCALL
        movea.l 8(sp),a0
        movea.l 4(sp),a1
#endif
        moveq   #0,d0
lstcpy_loop:
#ifdef __mcoldfire__
        addq.l  #1,d0
#else
        addq.w  #1,d0
#endif
        move.b  (a0)+,(a1)+
        jne     lstcpy_loop
#ifdef __mcoldfire__
        subq.l  #1,d0
#else
        subq.w  #1,d0
#endif
        rts

/*
 * char *strchr(const char *s, int c)
 *
 * returns a pointer to the first occurrence of char c in string s, or NULL.
 */

C_SYM(strchr):
        /*
         * strchr(const char *s, int c) takes one pointer argument and one
         * int argument. Under -mfastcall s arrives in a0 (already where
         * this code needs it -- no setup needed) and c arrives in d0, not
         * on the stack; move it to d1, which is what the loop below reads
         * (see the identical fix in this file's strlencpy()).
         */
#if CONF_WITH_MFASTCALL
        move.l  d0,d1
#else
        /*
         * c is a 32-bit int (no -mshort), so its stack slot is 4 bytes at
         * 8(sp); the byte compared below is c's low byte, which lives in
         * the low word of that slot, i.e. at 10(sp), not 8(sp).
         */
        move.l  4(sp),a0
        move.w  10(sp),d1
#endif
strchr_loop:
        move.b  (a0)+,d0
        jeq     strchr_null
        cmp.b   d1,d0
        jne     strchr_loop
        subq.l  #1,a0
        move.l  a0,d0
        rts
strchr_null:
        /*
         * a0 was left one past the NUL by the (a0)+ above, i.e. non-NULL;
         * the match-found path below returns via a0 for free because
         * subq.l operates on a0 itself, but this path must clear a0
         * explicitly so a -mfastcall caller (pointer return in a0) also
         * sees NULL, not a stale non-NULL address.
         */
        suba.l  a0,a0
        moveq.l #0,d0
        rts
