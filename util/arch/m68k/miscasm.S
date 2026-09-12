/*
 * miscasm.S - Some small helper functions in assembler
 *
 * Copyright (C) 2001 by following authors
 * Copyright (C) 2013-2022 by The EmuTOS development team.
 *
 * Authors:
 *  SCC     Steven C. Cavender
 *  LVL     Laurent Vogel
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
        .globl  _trap1_pexec    // Reentrant Pexec call
_trap1_pexec:
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

#if USE_STOP_INSN_TO_FREE_HOST_CPU

/* void stop_until_interrupt(void)
 * Stop the CPU until an interrupt occurs.
 * This may save some host CPU time on emulators (i.e. ARAnyM).
 */
        .globl  _stop_until_interrupt
_stop_until_interrupt:
        move.w  sr,d0
        move.w  d0,d1           // Backup
#ifdef __mcoldfire__
        andi.l  #0x0700,d1      // Isolate IPL bits
#else
        andi.w  #0x0700,d1      // Isolate IPL bits
#endif

        // Convert the IPL into an offset in the stop block below
#ifdef __mcoldfire__
        lsr.l   #8-3,d1         // 8 bits right, then multiply by 4
        jmp     stop0(pc,d1.l)
#else
        lsr.w   #8-3,d1         // 8 bits right, then multiply by 4
        jmp     stop0(pc,d1.w)
#endif

        // Each stop case must be exactly 8 byte long.
        // Preserving sr is not strictly mandatory, but:
        // - it allows preserving additional special bits (trace?)
        // - it is a padding instruction to reach a block multiple of 2 bytes

stop0:
        stop    #0x2000
        move.w  d0,sr
        rts

stop1:
        stop    #0x2100
        move.w  d0,sr
        rts

stop2:
        stop    #0x2200
        move.w  d0,sr
        rts

stop3:
        stop    #0x2300
        move.w  d0,sr
        rts

stop4:
        stop    #0x2400
        move.w  d0,sr
        rts

stop5:
        stop    #0x2500
        move.w  d0,sr
        rts

stop6:
        stop    #0x2600
        move.w  d0,sr
        rts

stop7:
        stop    #0x2700
        move.w  d0,sr

        // Fall through _just_rts

#endif /* USE_STOP_INSN_TO_FREE_HOST_CPU */

// The RTS below is shared for other purposes.
        .globl  _just_rts
_just_rts:
        rts

/*
 *  WORD mul_div_round(WORD mult1,WORD mult2,WORD divisor)
 *
 *  returns (mult1 * mult2 / divisor), rounded away from zero
 *
 *  this is actually calculated as:
 *      ((mult1 * mult2 * 2 / divisor) +/- 1) / 2
 *  where mult1 & divisor are signed 16-bit integers
 *        mult2 is a signed 15-bit integer
 *
 *  this version of the code is derived (with some tweaks) from the
 *  original imported AES code (function mul_div() in aes/gsx2.S)
 *
 *  if you do not need rounding, use mul_div() from intmath.h instead!
 */
        .globl  _mul_div_round
/*
 * mul_div_round(WORD mult1, WORD mult2, WORD divisor) takes three scalar
 * arguments and had no -mfastcall branch at all: under fastcall they
 * arrive in d0/d1/d2 (only the low word of each is defined -- see this
 * file's protect_w/_ww "sign-extended to a full slot" comments -- which
 * is exactly what the word-sized ops below already read), not tightly
 * packed on the stack. Its non-fastcall stack path was also stale: with
 * -mshort no longer used for the kernel build (#300), each WORD argument
 * now gets its own 4-byte slot instead of being packed 2 bytes apart, so
 * mult1/mult2/divisor's low words moved from 4/6/8(sp) to 6/10/14(sp).
 */
_mul_div_round:
#ifdef __mcoldfire__
#if CONF_WITH_MFASTCALL
        add.l   d1,d1           // d1 = mult2 * 2
        muls.w  d0,d1           // d1 = mult1 * mult2 * 2
        divs.w  d2,d1           // d1 = (mult1 * mult2 * 2) / divisor
        move.l  d1,d0
#else
        moveq.l #0,d0
        move.w  10(sp),d0       // d0 = mult2
        add.l   d0,d0           // d0 = mult2 * 2
        muls.w  6(sp),d0        // d0 = mult1 * mult2 * 2
        divs.w  14(sp),d0       // d0 = (mult1 * mult2 * 2) / divisor
#endif
        ext.l   d0
        jmi     mdrminus
        addq.l  #1,d0           // d0 = (mult1 * mult2 * 2) / divisor + 1
        asr.l   #1,d0           // d0 = ((mult1 * mult2 * 2) / divisor + 1) / 2
        rts
mdrminus:
        subq.l  #1,d0           // d0 = (mult1 * mult2 * 2) / divisor - 1
        asr.l   #1,d0           // d0 = ((mult1 * mult2 * 2) / divisor - 1) / 2
        rts
#else
#if CONF_WITH_MFASTCALL
        add.w   d1,d1           // d1 = mult2 * 2
        muls.w  d0,d1           // d1 = mult1 * mult2 * 2
        divs.w  d2,d1           // d1 = (mult1 * mult2 * 2) / divisor
        move.w  d1,d0
#else
        move.w  10(sp),d0       // d0 = mult2
        add.w   d0,d0           // d0 = mult2 * 2
        muls.w  6(sp),d0        // d0 = mult1 * mult2 * 2
        divs.w  14(sp),d0       // d0 = (mult1 * mult2 * 2) / divisor
#endif
        jmi     mdrminus
        addq.w  #1,d0           // d0 = (mult1 * mult2 * 2) / divisor + 1
        asr.w   #1,d0           // d0 = ((mult1 * mult2 * 2) / divisor + 1) / 2
        rts
mdrminus:
        subq.w  #1,d0           // d0 = (mult1 * mult2 * 2) / divisor - 1
        asr.w   #1,d0           // d0 = ((mult1 * mult2 * 2) / divisor - 1) / 2
        rts
#endif

/* ==== Glue for external vectors ===========================================
 *
 * EmuTOS uses d0-d1/a0-a1 as scratch registers. However the original TOS
 * used d0-d2/a0-a2 as scratch registers. For this reason, when jumping into
 * external user-supplied code we need to save/restore d2/a2. The routines
 * below provide the necessary assembler support.
 */

        .globl   _protect_v
        .globl   _protect_w
        .globl   _protect_ww
        .globl   _protect_wlwwwl

/*
 * LONG protect_v(LONG (*func)(void));
 *
 * These four wrappers weren't wired up for -mfastcall at all: their
 * bodies unconditionally read every argument off a plain-ABI stack
 * frame, but a -mfastcall caller passes func in a0 and the scalar
 * arguments in d0/d1/d2, spilling only what doesn't fit to the stack
 * (confirmed against this GCC fork's own -mfastcall codegen for these
 * exact argument shapes). Left as-is, every argument read here would
 * come from wherever the caller's own stack happened to have -- e.g.
 * a stale value from an earlier call, which is not a hypothetical: this
 * is what let a garbled "func" pointer through protect_wlwwwl's caller
 * (bios.c's hard-disk read/write path, hdv_rw) into a wild jsr.
 *
 * Fix: under -mfastcall, build the exact same plain-ABI frame each
 * body already expects (func/args at the same fixed offsets, one full
 * 4-byte slot each, matching how a plain-ABI caller pads WORD args --
 * verified against this compiler's own non-fastcall codegen for the
 * same signatures) by pushing the incoming registers, then bsr into
 * the unchanged body under a new label. Only pop what this wrapper
 * itself pushed before returning: any argument that didn't fit in
 * a0/d0/d1/d2 arrived spilled on our own caller's stack (caller-cleanup,
 * same convention as this file's other -mfastcall fixes), and must be
 * left there for the caller to pop after we return, not touched here.
 */
#if CONF_WITH_MFASTCALL
_protect_v:
        move.l   a0,-(sp)                // func
        bsr      protect_v_stackbody
        lea      4(sp),sp                // drop func pushed above (no caller stack args)
        rts

protect_v_stackbody:
#else
_protect_v:
#endif
        move.l   4(sp),a0
        move.l   a2,-(sp)
        move.l   d2,-(sp)
        jsr      (a0)
        move.l   (sp)+,d2
        move.l   (sp)+,a2
        rts

/*
 * LONG protect_w(LONG (*func)(WORD), WORD);
 */
#if CONF_WITH_MFASTCALL
_protect_w:
        ext.l    d0                      // a, sign-extended to a full slot
        move.l   d0,-(sp)                // a
        move.l   a0,-(sp)                // func
        bsr      protect_w_stackbody
        lea      8(sp),sp                // drop func/a pushed above (no caller stack args)
        rts

protect_w_stackbody:
#else
_protect_w:
#endif
        move.l   4(sp),a0
        /*
         * a's own slot is 4 bytes wide now (no -mshort), so its low word
         * -- the value the jsr target's classic 2-byte-packed ABI below
         * actually wants -- is at 10(sp), not 8(sp).
         */
        move.w   10(sp),d0
        move.l   a2,-(sp)
        move.l   d2,-(sp)
        move.w   d0,-(sp)
        jsr      (a0)
        addq.l   #2,sp
        move.l   (sp)+,d2
        move.l   (sp)+,a2
        rts

/*
 * LONG protect_ww(LONG (*func)(WORD), WORD, WORD);
 */
#if CONF_WITH_MFASTCALL
_protect_ww:
        ext.l    d1                      // b, sign-extended to a full slot
        move.l   d1,-(sp)                // b
        ext.l    d0                      // a, sign-extended to a full slot
        move.l   d0,-(sp)                // a
        move.l   a0,-(sp)                // func
        bsr      protect_ww_stackbody
        lea      12(sp),sp               // drop func/a/b pushed above (no caller stack args)
        rts

protect_ww_stackbody:
#else
_protect_ww:
#endif
        move.l   4(sp),a0
        /*
         * a and b used to be two tightly packed WORDs sharing one 4-byte
         * span at 8(sp), so a single move.l here could read both at once
         * and re-push them unchanged for the jsr target's classic
         * 2-byte-packed ABI below. Without -mshort each now has its own
         * 4-byte slot, so that longword must be rebuilt explicitly from
         * their low words (a's at 10(sp), b's at 14(sp)) instead.
         */
        move.w   10(sp),d0
        swap     d0
        move.w   14(sp),d0
        move.l   a2,-(sp)
        move.l   d2,-(sp)
        move.l   d0,-(sp)
        jsr      (a0)
        addq.l   #4,sp
        move.l   (sp)+,d2
        move.l   (sp)+,a2
        rts

/*
 * LONG protect_wlwwwl(LONG (*func)(), WORD, LONG, WORD, WORD, WORD, LONG);
 *
 * Only the first 3 scalar args (a, b, c) fit in d0/d1/d2 alongside func
 * in a0; d, e and f spill to our caller's stack, each already sign-
 * extended to a full 4-byte slot by the caller (same convention as
 * every other -mfastcall stack-spill fix in this tree), at 4(sp)/8(sp)/
 * 12(sp) relative to our own entry sp.
 */
#if CONF_WITH_MFASTCALL
_protect_wlwwwl:
        move.l   12(sp),-(sp)            // f, copied from our caller's spilled arg
        move.l   12(sp),-(sp)            // e, ditto (offset shifted by the f push above)
        move.l   12(sp),-(sp)            // d, ditto (offset shifted twice now)
        ext.l    d2                      // c, sign-extended to a full slot
        move.l   d2,-(sp)                // c
        move.l   d1,-(sp)                // b (already a full LONG)
        ext.l    d0                      // a, sign-extended to a full slot
        move.l   d0,-(sp)                // a
        move.l   a0,-(sp)                // func
        bsr      protect_wlwwwl_stackbody
        lea      28(sp),sp               // drop func/a/b/c/d/e/f pushed above; the
                                          // caller's own spilled d/e/f are untouched
                                          // and remain its to pop
        rts

protect_wlwwwl_stackbody:
#else
_protect_wlwwwl:
#endif
        /*
         * a/c/d/e used to be four tightly packed WORDs and b/f two LONGs,
         * all sharing one contiguous 16-byte span at 8(sp) -- movem could
         * blit that whole span into d0-d1/a0-a1 and back out unchanged,
         * since the source already matched the jsr target's classic
         * 2-byte-packed ABI byte-for-byte. Without -mshort each WORD now
         * has its own 4-byte slot, so that trick no longer produces the
         * right bytes: rebuild the 16-byte classic frame explicitly from
         * each argument's own slot (WORD args from their low word) and
         * push it right-to-left, exactly as protect_w/_ww do above.
         *
         * The jsr target itself (e.g. bios.c's hdv_rw) is always called
         * via this same classic ABI regardless of CONF_WITH_MFASTCALL,
         * matching every other protect_* vector (bconin_vec, hdv_mediach,
         * hdv_bpb, bell_hook, userterm, ...): these are frozen, user-
         * replaceable TOS hardware vectors that a third-party driver can
         * install into without ever knowing this kernel's own -mfastcall
         * setting, so the call convention here can't depend on it either.
         */
        move.l   a2,-(sp)
        move.l   d2,-(sp)
        move.l   36(sp),-(sp)   // f (full LONG, from its own slot)
        move.w   38(sp),-(sp)   // e (low word of its slot)
        move.w   36(sp),-(sp)   // d (low word of its slot)
        move.w   34(sp),-(sp)   // c (low word of its slot)
        move.l   30(sp),-(sp)   // b (full LONG, from its own slot)
        move.w   32(sp),-(sp)   // a (low word of its slot)
        move.l   28(sp),a0      // func
        jsr      (a0)
        lea      16(sp),sp
        move.l   (sp)+,d2
        move.l   (sp)+,a2
        rts
