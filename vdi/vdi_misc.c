/*
 * vdi_misc.c - everything, what does not fit in elsewhere
 *
 * Copyright 1982 by Digital Research Inc.  All rights reserved.
 * Copyright 1999 by Caldera, Inc. and Authors:
 * Copyright 2002-2021 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "emutos.h"
#include "asm.h"
#include "intmath.h"
#include "biosbind.h"
#include "tosvars.h"
#include "vdi_defs.h"
#include "vdi_backend.h"
#include "lineavars.h"
#include "biosext.h"

static BOOL in_proc;                   /* flag, if we are still running */

#ifdef __arm__
static void tick_int(int u);
#else
/*
 * tick_int_etv_entry (util/arch/m68k/miscasm.S) is what actually gets
 * installed into etv_timer via Setexc() below: see the comment there
 * for why tick_int() itself -- an ordinary internal C function -- can't
 * be called directly through that frozen, classic-ABI TOS vector. That
 * trampoline is the only caller outside this file, so tick_int() only
 * needs external linkage here, not on __arm__.
 */
void tick_int(int u);
extern void tick_int_etv_entry(void);
#endif



/*
 * arb_corner - copy and sort (arbitrate) the corners
 *
 * raster (ll, ur) format is desired.
 */
void arb_corner(Rect * rect)
{
    /* Fix the x coordinate values, if necessary. */
    if (rect->x1 > rect->x2) {
        WORD temp = rect->x1;
        rect->x1 = rect->x2;
        rect->x2 = temp;
    }

    /* Fix the y coordinate values, if necessary. */
    if (rect->y1 > rect->y2) {
        WORD temp = rect->y1;
        rect->y1 = rect->y2;
        rect->y2 = temp;
    }
}



/*
 * arb_line - copy and sort (arbitrate) the lines coordinates
 *
 * traditional (ll, ur) format is desired.
 */
void arb_line(Line * line)
{
    /* Fix the x coordinate values, if necessary. */
    if (line->x1 > line->x2) {
        WORD temp = line->x1;
        line->x1 = line->x2;
        line->x2 = temp;
    }

    /* Fix the y coordinate values, if necessary. */
    if (line->y1 < line->y2) {
        WORD temp = line->y1;
        line->y1 = line->y2;
        line->y2 = temp;
    }
}



/*
 * tick_int -  VDI Timer interrupt routine
 *
 * etv_timer points here directly on __arm__; on m68k it points to the
 * tick_int_etv_entry trampoline above, which tail-calls into this.
 */
#ifdef __arm__
static
#endif
void tick_int(int u)
{
    if (!in_proc) {
        in_proc = 1;                    /* set flag, that we are running */
                                        /* MAD: evtl. registers to stack */
        /*
         * tim_addr/tim_chain are frozen, user-replaceable TOS vectors
         * (Vex_timv() lets any program install its own handler in
         * tim_addr): call them via the classic tightly-packed ABI
         * always, using protect_wv(), same as every other protect_*
         * vector in bios.c -- not as an ordinary compiled C call, which
         * on m68k would use whatever ABI this kernel build was compiled
         * with instead of the documented TOS convention a genuine
         * external handler expects. protect_wv() (unlike protect_w())
         * is declared void-returning, matching ETV_TIMER_T's own void
         * return exactly -- only the int-vs-WORD parameter width still
         * needs a cast, the same int/WORD mismatch is inherent to
         * ETV_TIMER_T itself (include/biosdefs.h) for every frozen
         * timer vector, not something specific to this call site.
         */
#ifdef __arm__
        (*linea_vars.tim_addr)(u);                 /* call the timer vector */
#else
        protect_wv((void(*)(WORD))linea_vars.tim_addr, (WORD)u);
#endif
                                        /* and back from stack */
    }
    in_proc = 0;                        /* allow yet another trip through */
                                        /* MAD: evtl. registers to stack */
#ifdef __arm__
    (*linea_vars.tim_chain)(u);         /* call the old timer vector too */
#else
    protect_wv((void(*)(WORD))linea_vars.tim_chain, (WORD)u);
#endif
                                        /* and back from stack */
}



/*
 * vdi_vex_timv - exchange timer interrupt vector
 *
 * entry:          new vector in CONTRL[7-8]
 * exit:           old vector in CONTRL[9-10]
 */
void vdi_vex_timv(Vwk * vwk)
{
    disable_interrupts();

    CONTRL->ptr2 = linea_vars.tim_addr;
    linea_vars.tim_addr = CONTRL->ptr1;

    enable_interrupts();

    INTOUT[0] = (WORD)Tickcal();        /* ms between timer C calls */
    CONTRL->nintout = 1;
}



/*
 * do_nothing - doesn't do much  :-)
 */

static void do_nothing_int(int u)
{
    (void)u;
}



/*
 * timer_init - initialize the timer
 *
 * initially set timer vector to dummy, save old vector
 */
void timer_init(void)
{
//    WORD old_sr;

    in_proc = 0;                        /* no vblanks in process */

    /* Now initialize the lower level things */
    linea_vars.tim_addr = do_nothing_int;          /* tick points to rts */

    disable_interrupts();
#ifdef __arm__
    linea_vars.tim_chain = (void(*)(int))          /* save old vector */
    Setexc(0x100, (long)tick_int);      /* set etv_timer to tick_int */
#else
    linea_vars.tim_chain = (void(*)(int))          /* save old vector */
    Setexc(0x100, (long)tick_int_etv_entry);   /* set etv_timer to tick_int */
#endif
    enable_interrupts();

}



/*
 * timer_exit - de-initialize the time
 *
 * reactivate the old saved vector
 */
void timer_exit(void)
{
//    WORD old_sr;

    disable_interrupts();
//    old_sr = set_sr(0x2700);            /* disable interrupts */
    Setexc(0x100, (long)linea_vars.tim_chain);     /* set etv_timer to tick_int */
    enable_interrupts();
//    set_sr(old_sr);                     /* enable interrupts */
}



/*
 * get_start_addr - return memory address for column x, row y
 *
 * NOTE: the input x value may be negative (for example, this happens
 * when handling a slanting wideline starting at pixel 0 of a row).  This
 * value must be right-shifted to obtain an offset in bytes.
 * According to the C standard, the result of right-shifting a negative
 * value is implementation-defined.  GCC has the correct behaviour from
 * our point of view: high-order bits are 1-filled, so the number remains
 * negative.
 */
UWORD * get_start_addr(const WORD x, const WORD y)
{
#if CONF_WITH_VDI_BACKEND_DISPATCH
    const vdi_backend_ops *backend = vdi_screen_backend();

    /*
     * Unlike the direct-call cases below, this path builds only for
     * configurations with more than one renderer, which have none of
     * cartridge_defconfig's byte-budget pressure -- so guard against
     * vdi_backend_select() returning NULL for a descriptor no backend
     * supports, rather than dereferencing it.
     */
    if (!backend)
        return NULL;
    return backend->get_start_addr(x, y);
#elif CONF_WITH_VDI_BACKEND_TRUECOLOR
    /*
     * Truecolor-only build: call the packed backend's address arithmetic
     * directly -- vdi_screen_backend() and its self-init check have no
     * caller left, so the machinery is compiled out entirely.
     */
    return truecolor_get_start_addr(x, y);
#else
    /*
     * Planar-only build: call the planar address arithmetic directly
     * instead of paying for an indirect call the result of which is
     * already known at compile time. This matters on cartridge_defconfig,
     * whose 128 KB image has essentially no spare room for dispatch
     * overhead that can only ever resolve one way.
     */
    return planar_get_start_addr(x, y);
#endif
}

UWORD *planar_get_start_addr(WORD x, WORD y)
{
    UBYTE *addr;

    addr = v_bas_ad;                    /* start of screen */
    addr += (x&0xfff0)>>shift_offset[linea_vars.v_planes]; /* add x coordinate part of addr */
    addr += (LONG)y * linea_vars.v_lin_wr;         /* add y coordinate part of addr */
    return (UWORD*)addr;
}
