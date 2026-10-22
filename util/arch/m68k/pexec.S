/*
 * pexec.S - the reentrant Pexec() call (m68k)
 *
 * Split from miscasm.S so that a program that is not the kernel (EmuDesk,
 * CONF_EXTERNAL_DESK) can have it without the rest of that file, which
 * calls into the kernel.
 *
 * Copyright (C) 2001 by following authors
 * Copyright (C) 2013-2022 by The EmuTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"

/*
 *  GEMDOS Pexec() call. Reentrant: unlike the variadic C trap1() this
 *  used to sit alongside (removed for #300 -- GEMDOS calls on m68k now
 *  go through the typed trap1_v()/trap1_w()/... macros in asm.h
 *  instead), its arguments are explicitly typed, so it saves/restores
 *  d2/a2 on the stack around the trap rather than in static storage.
 */
        .globl  C_SYM(trap1_pexec)    // Reentrant Pexec call
C_SYM(trap1_pexec):
#if CONF_WITH_MFASTCALL
        /*
         * Build the GEMDOS wire frame trap #1 expects (opcode, mode,
         * path, tail, env). Under -mfastcall a call to
         * trap1_pexec(WORD mode, const char*, const char*, const char*)
         * arrives with mode in d0, path in a0, tail in a1, and only env
         * (the 4th argument -- fastcall has just two address registers)
         * spilled to the stack, at 4(sp) relative to our own entry sp.
         * Read env into a scratch register (d1, otherwise unused by
         * this signature) before the d2/a2 save below touches the
         * stack and shifts that offset, then build the frame directly
         * against sp so it stays contiguous for the trap.
         */
        move.l  4(sp),d1        // env, from the caller's stack-spilled 4th arg
#ifdef __mcoldfire__
        move.l  a2,-(sp)
        move.l  d2,-(sp)
#else
        movem.l d2/a2,-(sp)
#endif
        move.l  d1,-(sp)        // env
        move.l  a1,-(sp)        // tail
        move.l  a0,-(sp)        // path
        move.w  d0,-(sp)        // mode, low word
        move.w  #0x4b,-(sp)
        trap    #1              // Pexec
        lea     16(sp),sp
#ifdef __mcoldfire__
        move.l  (sp)+,d2
        move.l  (sp)+,a2
#else
        movem.l (sp)+,d2/a2
#endif
#else
#ifdef __mcoldfire__
        move.l  a2,-(sp)
        move.l  d2,-(sp)
#else
        movem.l d2/a2,-(sp)
#endif
        /*
         * Re-push each parameter, tightly packed, to build the GEMDOS
         * wire frame trap #1 expects (opcode, mode, path, tail, env).
         * The offsets below read them back off *our own caller's*
         * frame, i.e. how GCC laid out a call to
         * trap1_pexec(short mode, const char*, const char*, const char*)
         * -- and with -mshort no longer used for the kernel m68k build
         * (#300), that caller-side frame is no longer tightly packed
         * either: GCC now gives "mode" a full 4-byte slot instead of 2,
         * shifting every offset below by 2 from what tight packing
         * would give (22->24 for the three pointer args, 24->26 for
         * mode's low word -- mode's sign/zero-extended value always
         * lands in the low word of its 4-byte slot regardless of
         * sign, so a plain word read still recovers it correctly).
         */
        move.l  24(sp),-(sp)    // Push parameter on the stack again for pexec
        move.l  24(sp),-(sp)
        move.l  24(sp),-(sp)
        move.w  26(sp),-(sp)
        move.w  #0x4b,-(sp)
        trap    #1              // Pexec
        lea     16(sp),sp
#ifdef __mcoldfire__
        move.l  (sp)+,d2
        move.l  (sp)+,a2
#else
        movem.l (sp)+,d2/a2
#endif
#endif
        rts
