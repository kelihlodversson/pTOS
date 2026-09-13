/*
 * gemdosif.S - Assembler part of the GEMDOS interface
 *
 * Copyright 2002-2021, The EmuTOS development team
 *           1999, Caldera Thin Clients, Inc.
 *           1987, Digital Research Inc.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"
#include "asmstruct.h"

/*
 * sizes (in words) of the stacks used by:
 *  (1) the far_bcha/far_mcha/aes_wheel functions
 *  (2) the tikcod function
 *
 * as of july/2019, the observed stack usage is about 24 words for
 * the mouse stack and 12 words for the tick stack
 */
#define MOUSE_STACK_SIZE    48
#define TICK_STACK_SIZE     36
        .globl  C_SYM(giveerr)
        .globl  C_SYM(takeerr)
        .globl  C_SYM(retake)
        .globl  C_SYM(unset_aestrap)
        .globl  C_SYM(set_aestrap)
        .globl  C_SYM(far_mcha)
#if CONF_WITH_EXTENDED_MOUSE
        .globl  C_SYM(aes_wheel)
#endif
        .globl  C_SYM(far_bcha)
        .globl  C_SYM(drawrat)
        .globl  C_SYM(justretf)
        .globl  C_SYM(aestrap_intercepted)

        .globl  C_SYM(tiksav)
        .globl  C_SYM(CMP_TICK)
        .globl  C_SYM(NUM_TICK)
        .globl  C_SYM(drwaddr)
        .globl  C_SYM(tikcod)
        .globl  C_SYM(enable_ceh)

/* now defined by the BIOS, in bios/arch/m68k/intmask.S */
        .extern C_SYM(disable_interrupts)
        .extern C_SYM(enable_interrupts)

        .extern C_SYM(eralert)
        .extern C_SYM(rlr)
        .extern C_SYM(super)
        .extern C_SYM(b_click)
        .extern C_SYM(mchange)
        .extern C_SYM(forkq)
#if CONF_WITH_EXTENDED_MOUSE
        .extern C_SYM(wheel_change)
#endif
        .extern C_SYM(tchange)
        .extern C_SYM(b_delay)
        .extern __etext
        .extern C_SYM(os_beg)


/*
 * DOS error trapping code: restores aestrap & critical error vector
 *
 * called when entering graphics mode to run a graphics application
 * after a character mode application (which may have stepped on the
 * AES trap and/or the critical error vector)
 */
C_SYM(retake):
#ifdef __mcoldfire__
        lea     0x88,a0
        move.l  #_aestrap,(a0)
#else
        move.l  #_aestrap,0x88
#endif
        move.l  #criterr_handler,d0
call_err:
        move.l  d0,-(sp)
        move.w  #0x0101,-(sp)       // critical error vector
        move.w  #5,-(sp)            // Setexc
        trap    #13
        addq.l  #8,sp
        rts

/*
 * restore the previous critical error handler
 *
 * called when leaving graphics mode, either to run a character-based
 * application, or during desktop shutdown
 */
C_SYM(giveerr):
        move.l  save_etv_critic,d0
        jra     call_err

/*
 * install the GEM critical error handler
 *
 * called during desktop startup, just before entering graphics mode
 */
C_SYM(takeerr):
        moveq   #0,d0
        move.w  d0,C_SYM(enable_ceh)      // initialise flag for _criterr_handler
        moveq   #-1,d0
        jbsr    call_err            // get current error vector
        move.l  d0,save_etv_critic  //  & save it locally
        move.l  #criterr_handler,d0 // set new vector
        jra     call_err

/*
 * default critical error handler in graphics mode
 *
 * NOTE 1: we call form_alert() from eralert(), without going through
 * the AES trap.  form_alert() may do a context switch, during which
 * the current stack pointer is saved in UDA_SPSUPER.  Although this
 * is restored later when the context is switched back to us, it will
 * remain pointing to the wrong stack (AES routines expect that it
 * always points within the AES stack for the process).  If we are not
 * careful, the next call to an AES routine will crash the system.  So
 * we do the following:
 *  . save UDA_SPSUPER before calling eralert(), and restore it after
 *  . save the current stack pointer, and restore it after
 *  . use our own private stack during critical error processing
 *
 * NOTE 2: for TOS compatibility, we should always call the old handler
 * until the desktop is fully initialised.  We use the enable_ceh flag,
 * which is set to non-zero just before the main loop in deskmain().
 */
criterr_handler:
        tst.w   C_SYM(enable_ceh)         // should we invoke form_alert?
        jne     normal              // yes
        move.l  save_etv_critic,a0  // else go to old handler
        jmp     (a0)
normal:
        move.w  4(sp),d0            // d0 = error code
        move.w  6(sp),d1            // d1 = drive number
        movea.l C_SYM(rlr),a0
        movea.l PD_UDA(a0),a0       // a0 -> UDA
#ifdef __mcoldfire__
        lea     UDA_SPSUPER(a0),a0
        move.l  (a0),save_spsuper
#else
        move.l  UDA_SPSUPER(a0),save_spsuper
#endif
        jbsr    C_SYM(disable_interrupts) // stack switch must be atomic
        move.l  sp,save_spcriterr
        lea     criterr_stack,sp
        jbsr    C_SYM(enable_interrupts)
#ifdef __mcoldfire__
        lea     -36(sp),sp
        movem.l d3-d7/a3-a6,(sp)    // save regs on temp stack
#else
        movem.l d3-d7/a3-a6,-(sp)   // save regs on temp stack
#endif
        movea.l #err_tbl,a1
        suba.l  a0,a0               // assume zeroth entry (error -1)
        move.w  d0,-(sp)            // save original error code on stack
        jeq     alert_lookup        // zero, go do lookup
#ifdef __mcoldfire__
        not.l   d0                  // ones complement error code
#else
        not.w   d0                  // ones complement error code
#endif
        cmpi.w  #err_tbl_last,d0    // outside table?
        jgt     alert_lookup        // yes, assume zeroth entry
        movea.w d0,a0               // else a0 = lookup table index
alert_lookup:
        move.b  0(a0,a1.l),d0       // convert error code to alert number
        /*
         * eralert(WORD n, WORD d) is an ordinary internal C function
         * (aes/gemfmlib.c). Under -mfastcall n/d already sit in d0/d1,
         * so no push is needed at all. Without -mshort the plain ABI
         * gives each its own 4-byte slot instead of packing them
         * tightly -- but as with the super call frame above, each
         * value always lands in the low word of its slot regardless of
         * what's in the upper half, so a plain longword push of d0/d1
         * still works, matching this file's existing convention.
         */
#if CONF_WITH_MFASTCALL
        jsr     C_SYM(eralert)            // issue alert
#else
        move.l  d1,-(sp)            // put drive on stack
        move.l  d0,-(sp)            // and alert number
        jsr     C_SYM(eralert)            // issue alert
        addq.l  #8,sp
#endif
        move.w  (sp)+,d1            // recover original error code
        ext.l   d1
        tst.w   d0                  // did user say Cancel?
        jeq     no_retry            // yes, branch
        move.l  #0x00010000,d1      // else set special retry request value
no_retry:
        move.l  d1,d0               // set return code
#ifdef __mcoldfire__
        movem.l (sp),d3-d7/a3-a6    // restore regs from temp stack
        lea     36(sp),sp
#else
        movem.l (sp)+,d3-d7/a3-a6   // restore regs from temp stack
#endif
        movea.l save_spcriterr,sp   // recover old stack pointer
        movea.l C_SYM(rlr),a0
        movea.l PD_UDA(a0),a0       // a0 -> UDA
#ifdef __mcoldfire__
        lea     UDA_SPSUPER(a0),a0
        move.l  save_spsuper,(a0)
#else
        move.l  save_spsuper,UDA_SPSUPER(a0)
#endif
        rts

// **** Trap entry: ****
_aestrap:
        cmpi.w  #0xC8,d0
        jeq     trapaes
        cmpi.w  #0xC9,d0
        jeq     trapaes
        move.l  savetrap2,-(sp)
        rts

trapaes:
#if CONF_DEBUG_AES_STACK
        lea     -16(sp),sp
        movem.l d0-d1/a0-a1,(sp)
        jsr     _trapaes_debug_enter
        movem.l (sp),d0-d1/a0-a1
        lea     16(sp),sp
#endif

        jbsr    C_SYM(disable_interrupts)     // mask out interrupts
        move.l  usp,a0
#ifdef __mcoldfire__
        lea     -56(a0),a0
        movem.l d1-d7/a0-a6,(a0)        // put registers to user stack
#else
        movem.l d1-d7/a0-a6,-(a0)       // put registers to user stack
#endif
        move.l  a0,usp

        movea.l C_SYM(rlr),a6
        movea.l PD_UDA(a6),a6
        move.l  a0,UDA_SPUSER(a6)
        move.l  sp,UDA_OLDSPSUPER(a6)
        movea.l UDA_SPSUPER(a6),sp
        jbsr    C_SYM(enable_interrupts)      // restore interrupt mask

#if CONF_WITH_MFASTCALL
        /*
         * Under -mfastcall, super(WORD cx, AESPB*) takes cx in d0 and
         * pcrys_blk in a0. d0 already holds cx -- set at _aestrap's
         * entry and untouched since; disable/enable_interrupts above
         * are hand-written (bios/arch/m68k/intmask.S) and provably
         * preserve every register but sr -- so only pcrys_blk needs to
         * move from d1 into a0.
         */
        movea.l d1,a0                   // addr of parameter
        jsr     C_SYM(super)                  // jump to supervisor entry point
#else
        /*
         * Build the call frame GCC expects for super(WORD cx, AESPB*).
         * With -mshort no longer used for the kernel m68k build (#300),
         * GCC gives "cx" a full 4-byte stack slot instead of 2 (its
         * value always lands in the low word of that slot regardless of
         * sign, so a plain word push into the low half still works) --
         * pushing it tightly packed as a bare word here, like the old
         * -mshort ABI did, would misalign pcrys_blk by 2 bytes. See the
         * identical fix in aes/arch/m68k/gemstart.S's _dos_exec.
         */
        move.l  d1,-(sp)                // addr of parameter
        move.l  d0,-(sp)                // 200 or 201, in the low word
        jsr     C_SYM(super)                  // jump to supervisor entry point
        addq.l  #8,sp                   // restore stack pointer
#endif

        jbsr    C_SYM(disable_interrupts)     // mask out interrupts

        movea.l C_SYM(rlr),a0
        movea.l PD_UDA(a0),a0
        move.l  sp,UDA_SPSUPER(a0)
        movea.l UDA_OLDSPSUPER(a0),sp
        movea.l UDA_SPUSER(a0),a0

#ifdef __mcoldfire__
        movem.l (a0),d1-d7/a0-a6
        lea     56(a0),a0
#else
        movem.l (a0)+,d1-d7/a0-a6
#endif
        move.l  a0,usp
        jbsr    C_SYM(enable_interrupts)      // restore interrupt mask

#if CONF_DEBUG_AES_STACK
        lea     -16(sp),sp
        movem.l d0-d1/a0-a1,(sp)
        jsr     _trapaes_debug_exit
        movem.l (sp),d0-d1/a0-a1
        lea     16(sp),sp
#endif
        rte

C_SYM(unset_aestrap):
#ifdef __mcoldfire__
        lea     0x88,a0
        move.l  savetrap2,(a0)
#else
        move.l  savetrap2,0x88
#endif
        rts
C_SYM(set_aestrap):
#ifdef __mcoldfire__
        lea     0x88,a0
        move.l  (a0),savetrap2
        move.l  #_aestrap,(a0)
#else
        move.l  0x88,savetrap2
        move.l  #_aestrap,0x88
#endif
        rts

C_SYM(far_bcha):
        move.l  sp,gstksave
        lea     gstack,sp
#ifdef __mcoldfire__
        lea     -24(sp),sp
        movem.l d0-d2/a0-a2,(sp)
#else
        movem.l d0-d2/a0-a2,-(sp)
#endif
        /*
         * b_click(WORD state) is an ordinary internal C function: under
         * -mfastcall state already sits in d0, exactly where far_bcha
         * itself received it; without -mshort the plain ABI now wants a
         * full 4-byte slot instead of a tightly packed WORD.
         */
#if CONF_WITH_MFASTCALL
        jsr     C_SYM(b_click)
#else
        move.l  d0,-(sp)
        jsr     C_SYM(b_click)
        addq.l  #4,sp
#endif
#ifdef __mcoldfire__
        movem.l (sp),d0-d2/a0-a2
        lea     24(sp),sp
#else
        movem.l (sp)+,d0-d2/a0-a2
#endif
        movea.l gstksave,sp
        rts

C_SYM(far_mcha):
        move.l  sp,gstksave
        lea     gstack,sp
#ifdef __mcoldfire__
        lea     -24(sp),sp
        movem.l d0-d2/a0-a2,(sp)
#else
        movem.l d0-d2/a0-a2,-(sp)
#endif

        /*
         * forkq(FCODE fcode, LONG fdata) is an ordinary internal C
         * function: under -mfastcall fcode goes in a0 and fdata in d0,
         * not on the stack. fdata is built the same way as the
         * plain-ABI push below: d0's word (mouse state) in the high
         * half, d1's word in the low half.
         */
#if CONF_WITH_MFASTCALL
        swap    d0                      // state -> high word of d0
        move.w  d1,d0                   // -> low word of d0
        movea.l #C_SYM(mchange),a0
        jsr     C_SYM(forkq)
#else
        move.w  d1,-(sp)
        move.w  d0,-(sp)
        move.l  #C_SYM(mchange),-(sp)
        jsr     C_SYM(forkq)
        addq.l  #8,sp
#endif
#ifdef __mcoldfire__
        movem.l (sp),d0-d2/a0-a2
        lea     24(sp),sp
#else
        movem.l (sp)+,d0-d2/a0-a2
#endif
        movea.l gstksave,sp
        rts

#if CONF_WITH_EXTENDED_MOUSE
/* AES mouse wheel handler called by the VDI */
C_SYM(aes_wheel):
        move.l  sp,gstksave
        lea     gstack,sp
#ifdef __mcoldfire__
        lea     -24(sp),sp
        movem.l d0-d2/a0-a2,(sp)
#else
        movem.l d0-d2/a0-a2,-(sp)
#endif

        /*
         * forkq(FCODE fcode, LONG fdata): same -mfastcall register
         * convention and fdata layout as far_mcha above.
         */
#if CONF_WITH_MFASTCALL
        swap    d0                      // wheel state -> high word of d0
        move.w  d1,d0                   // -> low word of d0
        movea.l #C_SYM(wheel_change),a0
        jsr     C_SYM(forkq)
#else
        move.w  d1,-(sp)
        move.w  d0,-(sp)
        move.l  #C_SYM(wheel_change),-(sp)
        jsr     C_SYM(forkq)
        addq.l  #8,sp
#endif
#ifdef __mcoldfire__
        movem.l (sp),d0-d2/a0-a2
        lea     24(sp),sp
#else
        movem.l (sp)+,d0-d2/a0-a2
#endif
        movea.l gstksave,sp
        rts
#endif /* CONF_WITH_EXTENDED_MOUSE */

/*
;
;       drawrat(newx, newy)
;
*/
C_SYM(drawrat):
        /*
         * drawrat(WORD newx, WORD newy) is an ordinary internal C
         * function, but drwaddr's target (VDI's own mouse-draw
         * routine, installed via gsx_setmb()) expects newx/newy via the
         * same fixed d0/d1 register convention as mousevec et al, not
         * via C's own stack ABI -- this tail-jump adapts between the
         * two. Under -mfastcall newx/newy already arrive in d0/d1,
         * exactly where drwaddr wants them; without -mshort the plain
         * ABI now gives each its own 4-byte slot instead of packing
         * them tightly.
         */
#if !CONF_WITH_MFASTCALL
        move.w  6(sp),d0
        move.w  10(sp),d1
#endif
        move.l  C_SYM(drwaddr),-(sp)
        rts                     // Jump to vector stored in drwaddr


C_SYM(justretf):
        rts


/*
 * determine if trap #2 has been intercepted by someone else (e.g. NVDI)
 *
 * return 1 if intercepted, else 0
 */
C_SYM(aestrap_intercepted):
        moveq   #1,d0           // assume it's intercepted
        movea.l 0x88,a0
        cmpa.l  C_SYM(os_beg),a0
        jcs     aestrap_done
        cmpa.l  #__etext,a0
        jcc     aestrap_done
        moveq   #0,d0
aestrap_done:
        rts


C_SYM(tikcod):
        move.l  sp,tstksave
        lea     tstack,sp
        tst.l   C_SYM(CMP_TICK)
        jeq     L2234
        addq.l  #1,C_SYM(NUM_TICK)
        subq.l  #1,C_SYM(CMP_TICK)
        jne     L2234

        /*
         * forkq(FCODE fcode, LONG fdata) is an ordinary internal C
         * function: under -mfastcall fcode goes in a0 and fdata in d0,
         * not on the stack. Its WORD return (did it work?) stays in d0
         * either way -- only scalar arguments move under -mfastcall,
         * never a scalar return.
         */
#if CONF_WITH_MFASTCALL
        movea.l #C_SYM(tchange),a0
        move.l  C_SYM(NUM_TICK),d0    // try to create an entry in the fork ring
        jsr     C_SYM(forkq)
#else
        move.l  C_SYM(NUM_TICK),-(sp) // try to create an entry in the fork ring
        move.l  #C_SYM(tchange),-(sp)
        jsr     C_SYM(forkq)
        addq.l  #8,sp
#endif
        tst.w   d0              // did it work?
        jeq     L2234
        addq.l  #1,C_SYM(CMP_TICK)    // no, reset so that we try again on the next tick

L2234:
        /*
         * b_delay(WORD amnt) is an ordinary internal C function: under
         * -mfastcall amnt goes in d0, not on the stack; without
         * -mshort the plain ABI now wants a full 4-byte slot instead of
         * a tightly packed WORD.
         */
#if CONF_WITH_MFASTCALL
        moveq   #1,d0
        jsr     C_SYM(b_delay)
#else
        move.l  #1,-(sp)
        jsr     C_SYM(b_delay)
        addq.l  #4,sp
#endif
        movea.l tstksave,sp
        move.l  C_SYM(tiksav),-(sp)
        rts                     // Jump to vector stored in tiksav

SECTION_RODATA

/*
 * This table converts an error number to an (internal-only) alert
 * number: entry[n-1] contains the alert number to use for error -n.
 *
 * The alert number is used by eralert() in gemfmlib.c to index into
 * arrays in that module.  Thus this table must be synchronized with
 * gemfmlib.c.
 */
err_tbl:
        .dc.b   4,1,1,2,1,1,2,2     // errors -1 to -8
        .dc.b   4,2,2,2,0,3,4,2     // errors -9 to -16
        .dc.b   5                   // error -17 (EOTHER, currently not implemented)
err_tbl_end:
        .dc.b   0                   // dummy, not needed

        .equ    err_tbl_last,(err_tbl_end-err_tbl)

.bss

save_etv_critic:
        .ds.l    1      // save area for character-mode critical error vector

savetrap2:
        .ds.l    1

C_SYM(drwaddr):
        .ds.l    1

gstksave:
        .ds.l    1

tstksave:
        .ds.l    1

C_SYM(tiksav):
        .ds.l    1
C_SYM(NUM_TICK):
        .ds.l    1
C_SYM(CMP_TICK):
        .ds.l    1
C_SYM(enable_ceh):
        .ds.w    1      // flag to enable gui critical error handler

/*
 *  data areas used by the critical error handler
 */
save_spsuper:
        .ds.l    1              // save area for contents of UDA_SPSUPER
save_spcriterr:
        .ds.l    1              // save area for stack ptr on entry

/*
 *  the following private stack used to be 512 words, but that wasn't
 *  enough in all cases after some updates around nov/2016 increased
 *  just_draw()'s stack usage slightly.
 */
        .ds.w   768             // private stack
criterr_stack:

/*
 * miscellaneous stacks
 */

        .ds.w    MOUSE_STACK_SIZE
gstack:                         // gsx stack for mouse

        .ds.b    TICK_STACK_SIZE
tstack:                         // tick stack
        .ds.l    1
