/*
 * linea.S - linea graphics stuff
 *
 * Copyright (C) 2002-2021 by Authors:
 *
 * Authors:
 *  THH  Thomas Huth
 *  MAD  Martin Doering
 *  LVL  Laurent Vogel
 *  EET  Eero Tamminen
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/* #define ENABLE_KDEBUG */

#include "asmdefs.h"
#include "lineaasm.h"

// ==== References ===========================================================

        .extern C_SYM(sysfonts)
        .extern C_SYM(kprintf)

        .extern C_SYM(linea_show_mouse)
        .extern C_SYM(linea_hide_mouse)
        .extern C_SYM(linea_transform_mouse)

        .extern C_SYM(get_pix)
        .extern C_SYM(put_pix)

        .extern C_SYM(linea_line)
        .extern C_SYM(linea_rect)
        .extern C_SYM(linea_hline)
        .extern C_SYM(linea_polygon)

        .extern C_SYM(linea_blit)
        .extern C_SYM(text_blt)

        .extern C_SYM(cur_replace)
        .extern C_SYM(cur_display)
        .extern C_SYM(linea_raster)
        .extern C_SYM(linea_fill)

// ==== Definitions ==========================================================

        .globl  C_SYM(int_linea)              // entry for linea exception

// ==== Line-A handler ===============================
C_SYM(int_linea):
        /* save d0/d1/a0/a2 as they are needed for sprite undraw/draw */
#ifdef __mcoldfire__
        lea     linea_save,a1
        movem.l d0/d1/a0/a2,(a1)
#else
        movem.l d0/d1/a0/a2,linea_save
#endif
#ifdef __mcoldfire__
        move.l  4(sp),a0        /* Get the opcode address */
#else
        move.l  2(sp),a0        /* Get the opcode address */
#endif
        moveq   #0,d0
        move.w  (a0)+,d0        /* Get the opcode value */
#ifdef __mcoldfire__
        move.l  a0,4(sp)        /* Return after the Line A opcode */
#else
        move.l  a0,2(sp)        /* Return after the Line A opcode */
#endif
#ifdef __mcoldfire__
        /* On ColdFire, all the standard Line A opcodes conflict with
         * valid MAC instructions. Therefore they can't be used.
         * Fortunately, the 0xA92x opcodes are still illegal and trigger
         * the Line A exception. So programs can use 0xA92x instead of
         * 0xA00x to call the Line A. Thus we must keep only the last
         * digit as function number. */
        and.l   #0x00F,d0       /* Keep only the function number */
#else
        and.w   #0xFFF,d0       /* Keep only the function number */
#endif
        cmp.w   #nb_linea,d0
        jhs     wrong_linea

#ifdef ENABLE_KDEBUG
#ifdef __mcoldfire__
        lea     -16(sp),sp
        movem.l d0-d1/a0-a1,(sp)
#else
        movem.l d0-d1/a0-a1,-(sp)
#endif
        move.w  d0,-(sp)
        pea     linea_msg
        jsr     C_SYM(kprintf)              /* Print debugging information */
        addq.l  #6,sp
#ifdef __mcoldfire__
        movem.l (sp),d0-d1/a0-a1
        lea     16(sp),sp
#else
        movem.l (sp)+,d0-d1/a0-a1
#endif
#endif /* ENABLE_KDEBUG */

        lea     linea_vecs,a1
#ifdef __mcoldfire__
        lsl.l   #2,d0
#else
        lsl.w   #2,d0
#endif
        move.l  0(a1,d0),a1
        /* restore d0,d1,a0,a2 in case we're calling sprite undraw/draw */
#ifdef __mcoldfire__
        move.l  a1,-(sp)
        lea     linea_save,a1
        movem.l (a1),d0/d1/a0/a2
        move.l  (sp)+,a1
#else
        movem.l linea_save,d0/d1/a0/a2
#endif
        jsr     (a1)
linea_dispatch_pc:
        rte

wrong_linea:
#ifdef ENABLE_KDEBUG
        move.w  d0,-(sp)
        subq.l  #2,a0
        move.l  a0,-(sp)
        pea     wrong_linea_msg
        jsr     C_SYM(kprintf)
        lea     10(sp),sp
#endif /* ENABLE_KDEBUG */
        rte

        .bss
        .even
linea_save:
        .ds.l   4       /* d0, d1, a0, a2 */

        SECTION_RODATA

#ifdef ENABLE_KDEBUG

wrong_linea_msg:
        .ascii  "pc=0x%08lx: Line-A call number 0x%03x out of bounds\n\0"
        .even

linea_msg:
        .ascii  "Line-A #%x\n\0"
        .even

#endif  /* ENABLE_KDEBUG */

        .text

_linea_0:
        lea     LA(v_planes),a0  // get base address for line a variables
        move.l  a0,d0

        lea     C_SYM(sysfonts),a1   // get pointer to the three system font headers

        lea     linea_vecs,a2   // get pointer to table of the Line-A routines
        rts

hide_mouse:
        move.l  d0,-(sp)
        jsr     C_SYM(linea_hide_mouse)
        move.l  (sp)+,d0
        rts

/*
 * undraw_sprite/draw_sprite/linea_blit call cur_replace(MCS*),
 * cur_display(Mcdb*,MCS*,WORD,WORD) and linea_blit(struct blit_frame*)
 * with their arguments already in the fixed registers the Line-A
 * hardware convention puts them in (see the comment above), which
 * happens to line up with plain cdecl stack order but NOT with
 * -mfastcall: there pointer arguments go in a0, then a1, not on the
 * stack. Route them through those registers instead of pushing under
 * -mfastcall; the plain-ABI push sequence is kept for other targets.
 */
undraw_sprite:
#if CONF_WITH_MFASTCALL
        movea.l a2,a0            /* save area ptr */
        jsr     C_SYM(cur_replace)
#else
        move.l  a2,-(sp)        /* save area ptr */
        jsr     C_SYM(cur_replace)
        addq.l  #4,sp
#endif
        rts

draw_sprite:
#if CONF_WITH_MFASTCALL
        movea.l a2,a1            /* save area ptr; a0 (new sprite), d0 (x)
                                  * and d1 (y) are already where fastcall
                                  * wants them */
        jsr     C_SYM(cur_display)
#else
        /*
         * cur_display(Mcdb*, MCS*, WORD, WORD) is an ordinary internal
         * function compiled under this kernel's own (post -mshort)
         * plain-stack ABI, which gives each WORD arg a full 4-byte slot
         * rather than packing them tightly -- sign-extend x/y before
         * pushing, and clean up 16 bytes, not 12.
         */
        ext.l   d1              /* y coord of hotspot, sign-extended */
        move.l  d1,-(sp)
        ext.l   d0              /* x coord, sign-extended */
        move.l  d0,-(sp)
        move.l  a2,-(sp)        /* save area ptr */
        move.l  a0,-(sp)        /* new sprite */
        jsr     C_SYM(cur_display)
        lea     16(sp),sp
#endif
        rts

linea_blit:
#if CONF_WITH_MFASTCALL
        movea.l a6,a0                   // blit frame structure addr
        jsr     C_SYM(linea_blit)
#else
        move.l  a6,-(sp)                // blit frame structure addr
        jsr     C_SYM(linea_blit)
        addq.l  #4,sp
#endif
        rts

        SECTION_RODATA

/* Some VDI C-functions can be used directly as-is because:
 * - they don't use the passed VDI workstation pointer
 * - parameters for line-A go in the same Intin/Ptsin/Control arrays
 * These functions are show, hide and transform (mouse) cursor.
 * However, as a compatibility feature for poorly-written programs,
 * hide mouse has an assembler wrapper that preserves d0, since Atari
 * TOS does that (probably inadvertently), and some programs rely on it.
 *
 * A couple of functions have line-A specific C-implementations.
 * They are put_pix, get_pix.
 *
 * Several functions need a C-wrapper around utility functions shared
 * with VDI.  They are linea_line, linea_hline, linea_rect, linea_polygon,
 * linea_raster, linea_fill.  Their arguments are in global variables/arrays.
 *
 * Bitblt (linea_blit) has both a small C-wrapper and an ASM-wrapper, as
 * the pointer to its argument array is given in a register.
 *
 * The assembly "text_blt" routine can be called directly because
 * it uses line-A variables which VDI just sets up before
 * calling it.
 *
 * The undraw_sprite and draw_sprite routines are passed their arguments
 * in registers.  Because lineA routines may be called directly (not via
 * the lineA interface), we must preserve those registers until the actual
 * routine is entered.  These routines are ASM wrappers for VDI utility
 * functions.
 *
 * Neochrome Master and old GFA programs use all of the lineA routines,
 * and many (even new) freeware games still use bitblt.
 *
 * It's probably only safe to run line-A-using programs in (1-4 plane)
 * ST/e modes.  Such programs can e.g. leave too little space for
 * sprite backup buffer in higher resolutions.
 */
        .even   // Mandatory in ELF section .rodata
linea_vecs:
        .dc.l   _linea_0
        .dc.l   C_SYM(put_pix)        /* $1 - put pixel */
        .dc.l   C_SYM(get_pix)        /* $2 - get pixel */
        .dc.l   C_SYM(linea_line)     /* $3 - draw line */
        .dc.l   C_SYM(linea_hline)    /* $4 - horizontal line */
        .dc.l   C_SYM(linea_rect)     /* $5 - filled rectangle */
        .dc.l   C_SYM(linea_polygon)  /* $6 - draw 1 line of polygon */
        .dc.l   linea_blit      /* $7 - area blit */
        .dc.l   C_SYM(text_blt)       /* $8 - text blitting */
        .dc.l   C_SYM(linea_show_mouse)      /* $9 - show mouse */
        .dc.l   hide_mouse      /* $A - hide mouse cursor */
        .dc.l   C_SYM(linea_transform_mouse) /* $B - transform mouse */
        .dc.l   undraw_sprite   /* $C - undraw sprite */
        .dc.l   draw_sprite     /* $D - draw sprite */
        .dc.l   C_SYM(linea_raster)   /* $E - copy raster form */
        .dc.l   C_SYM(linea_fill)     /* $F - flood fill */
linea_ents:
        /* Number of implemented Line A routines */
        .equ    nb_linea,(linea_ents-linea_vecs)/4
