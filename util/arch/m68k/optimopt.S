/*
 * optimopt.S - misc. optimised assembler routines
 *
 * Copyright (C) 2002-2020 The EmuTOS development team
 *
 * Authors:
 *  LVL   Laurent Vogel
 *  RFB   Roger Burrows
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"

        .globl  _scasb
        .globl  _expand_string

/*
 * const char *scasb(const char *str, char c)
 * returns the pointer to the first occurrence of char c in string str,
 * or the pointer to the ending '\0' of this string.
 * (see also strchr, which returns NULL instead when the char is not found)
 *
 */

_scasb:
        /*
         * scasb(char *p, char b) takes one pointer argument and one
         * scalar argument. Under -mfastcall p arrives in a0 (already
         * where this code needs it) and b arrives in d0, not on the
         * stack; move it to d1, which is what the loop below reads (see
         * the identical fix in stringasm.S's _strchr).
         */
#if CONF_WITH_MFASTCALL
        move.l  d0,d1
#else
        /*
         * b is a 32-bit int (no -mshort), so its stack slot is 4 bytes at
         * 8(sp); the byte compared below is b's low byte, which lives in
         * the low word of that slot, i.e. at 10(sp), not 8(sp).
         */
        move.l  4(sp),a0
        move.w  10(sp),d1
#endif
scasb_loop:
        move.b  (a0)+,d0
        jeq     scasb_end
        cmp.b   d1,d0
        jne     scasb_loop
scasb_end:
        subq.l  #1,a0
        move.l  a0,d0
        rts

/*
 *  WORD expand_string(WORD *dest,const char *src)
 *
 *  expand an unsigned byte string to an array of words, returning the
 *  length of the source string.  note that this code is optimised for
 *  the 68000: in order to obtain the fastest code, we copy the null
 *  byte too, but it is not included in the length value returned.
 */

_expand_string:
        /*
         * expand_string(WORD *dest, BYTE *src) takes two pointer
         * arguments. Under -mfastcall dest arrives in a0 and src in a1
         * (the reverse of what this code's registers mean below), not
         * on the stack; swap them into the a0=source/a1=dest convention
         * the loop expects.
         */
#if CONF_WITH_MFASTCALL
        exg     a0,a1            // a0 -> source, a1 -> dest
#else
        movea.l 8(sp),a0        // a0 -> source
        movea.l 4(sp),a1        // a1 -> dest
#endif
        move.l  a0,d0           // remember source start
        clr.w   d1              // work reg
copy_loop:
        move.b  (a0)+,d1        // 68000: 8 cycles
        move.w  d1,(a1)+        // 8 cycles
        jne     copy_loop       // 10 cycles when taken
        sub.l   d0,a0
        move.l  a0,d0
        subq.l  #1,d0           // d0.l = length
        rts
