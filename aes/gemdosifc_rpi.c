/*
 * gemdosif_rpi.c Raspberry pi version of the GEMDOS interface
 *
 * Copyright 2002-2017, The EmuTOS development team
 *           1999, Caldera Thin Clients, Inc.
 *           1987, Digital Research Inc.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "obdefs.h"
#include "struct.h"
#include "basepage.h"
#include "../bios/vectors.h"
#include "../bios/tosvars.h"
#include "gemdosif.h"
#include "gemfmlib.h"
#include "biosbind.h"
#include "asm.h"

int criterr_handler(WORD error, WORD drive);
typedef int (*criterr_handler_t)(WORD, WORD);


static ULONG save_cpsr;
static criterr_handler_t save_etv_critic; /* save area for character-mode critical error vector */
WORD enable_ceh;   /* flag to enable gui critical error handler */

/*
 * This table converts an error number to an (internal-only) alert
 * number: entry[n-1] contains the alert number to use for error -n.
 *
 * The alert number is used by eralert() in gemfmlib.c to index into
 * arrays in that module.  Thus this table must be synchronized with
 * gemfmlib.c.
 */
const WORD err_tbl[17] = {
        4,1,1,2,1,1,2,2,     /* errors -1 to -8 */
        4,2,2,2,0,3,4,2,     /* errors -9 to -16 */
        5                   /* error -17 (EOTHER, currently not implemented) */
};

static inline ULONG read_cpsr(void)
{
    unsigned int res;
    asm volatile (
        "mrs     %0, cpsr"
        : "=r"(res)
    );
    return res;
}

static inline void write_cpsr(ULONG cpsr)
{
    asm volatile (
        "msr     cpsr_c, %0"
        :
        : "r"(cpsr)
    );
}

/* disable interrupts */
ULONG disable_interrupts(void)
{
    save_cpsr = read_cpsr();
    asm volatile ("cpsid if");
    return save_cpsr;
}

/* restore interrupt mask as it was before disable_interrupts() */
void enable_interrupts(void)
{
    write_cpsr(save_cpsr);
}

/*
 * DOS error trapping code: restores aestrap & critical error vector
 *
 * called when entering graphics mode to run a graphics application
 * after a character mode application (which may have stepped on the
 * AES trap and/or the critical error vector)
 */

void retake(void)
{
    VEC_AES = aestrap;
    Setexc(0x0101, (long)criterr_handler);
}

/*
 * restore the previous critical error handler
 *
 * called when leaving graphics mode, either to run a character-based
 * application, or during desktop shutdown
 */
void giveerr(void)
{
    Setexc(0x0101, (long)save_etv_critic);
}

/*
 * install the GEM critical error handler
 *
 * called during desktop startup, just before entering graphics mode
 */
void takeerr(void)
{
    enable_ceh = 0;    /* initialise flag for criterr_handler */
    save_etv_critic = (criterr_handler_t)Setexc(0x0101, -1); /* save current error vector */
    Setexc(0x0101, (long)criterr_handler); /* set new vector */
}

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
 * until the desktop is fully initialised.  We use the _enable_ceh flag,
 * which is set to non-zero just before the main loop in deskmain().
 */


static ULONG* save_super;
int criterr_handler(WORD error, WORD drive)
{
    if(!enable_ceh)
    {
        return save_etv_critic(error, drive);
    }
    int retval;

    save_super = rlr->p_uda->u_spsuper; /* Super stack */

    /* TODO: run main part of error handler on a private stack */
    WORD error_index = ~error;
    if (error_index > 16 || error_index < 0)
        error_index = 0;

    retval = eralert(err_tbl[error_index], drive) ? 0x00010000 : 0;

    rlr->p_uda->u_spsuper = save_super;

    return retval;
}

PFVOID savetrap2;

void unset_aestrap(void)
{
    VEC_AES = savetrap2;
}

void set_aestrap(void)
{
    savetrap2 = VEC_AES;
    VEC_AES = aestrap;
}

/*
* determine if trap #2 has been intercepted by someone else (e.g. NVDI)
*
* return 1 if intercepted, else 0
*/
BOOL aestrap_intercepted(void)
{
    return ((LONG)VEC_AES) < os_beg || ((LONG)VEC_AES) >= ((LONG)_etext);
}
#if 0


_far_bcha:
        move.l  sp,gstksave
        lea     gstack,sp
#ifdef __mcoldfire__
        lea     -24(sp),sp
        movem.l d0-d2/a0-a2,(sp)
#else
        movem.l d0-d2/a0-a2,-(sp)
#endif
        move.w  d0,-(sp)
        jsr     _b_click
        addq.l  #2,sp
#ifdef __mcoldfire__
        movem.l (sp),d0-d2/a0-a2
        lea     24(sp),sp
#else
        movem.l (sp)+,d0-d2/a0-a2
#endif
        movea.l gstksave,sp
        rts

_far_mcha:
        move.l  sp,gstksave
        lea     gstack,sp
#ifdef __mcoldfire__
        lea     -24(sp),sp
        movem.l d0-d2/a0-a2,(sp)
#else
        movem.l d0-d2/a0-a2,-(sp)
#endif

        move.w  d1,-(sp)
        move.w  d0,-(sp)
        move.l  #_mchange,-(sp)
        jsr     _forkq
        addq.l  #8,sp
#ifdef __mcoldfire__
        movem.l (sp),d0-d2/a0-a2
        lea     24(sp),sp
#else
        movem.l (sp)+,d0-d2/a0-a2
#endif
        movea.l gstksave,sp
        rts

#if CONF_WITH_VDI_EXTENSIONS
/* AES mouse wheel handler called by the VDI */
_aes_wheel:
        move.l  sp,gstksave
        lea     gstack,sp
#ifdef __mcoldfire__
        lea     -24(sp),sp
        movem.l d0-d2/a0-a2,(sp)
#else
        movem.l d0-d2/a0-a2,-(sp)
#endif

        move.w  d1,-(sp)
        move.w  d0,-(sp)
        move.l  #_wheel_change,-(sp)
        jsr     _forkq
        addq.l  #8,sp
#ifdef __mcoldfire__
        movem.l (sp),d0-d2/a0-a2
        lea     24(sp),sp
#else
        movem.l (sp)+,d0-d2/a0-a2
#endif
        movea.l gstksave,sp
        rts
#endif


/*
;
;       drawrat(newx, newy)
;
*/
_drawrat:
        move.w  4(sp),d0
        move.w  6(sp),d1
        move.l  _drwaddr,-(sp)
        rts                     /* Jump to vector stored in _drwaddr */


_justretf:
        rts


_tikcod:
        move.l  sp,tstksave
        lea     tstack,sp
        tst.l   _CMP_TICK
        jeq     L2234
        addq.l  #1,_NUM_TICK
        subq.l  #1,_CMP_TICK
        jne     L2234

        move.l  _NUM_TICK,-(sp)
        move.l  #_tchange,-(sp)
        jsr     _forkq
        addq.l  #8,sp
L2234:
        move.w  #1,-(sp)
        jsr     _b_delay
        addq.l  #2,sp
        movea.l tstksave,sp
        move.l  _tiksav,-(sp)
        rts                     /* Jump to vector stored in _tiksav */



SECTION_RODATA



.bss

savesr:
        .ds.w    1

save_etv_critic:
        .ds.l    1      /* save area for character-mode critical error vector */

savetrap2:
        .ds.l    1

_drwaddr:
        .ds.l    1

_tikaddr:
        .ds.l    1
gstksave:
        .ds.l    1

tstksave:
        .ds.l    1

_tiksav:
        .ds.l    1
_NUM_TICK:
        .ds.l    1
_CMP_TICK:
        .ds.l    1
_enable_ceh:
        .ds.w    1      /* flag to enable gui critical error handler */

/*
 *  data areas used by the critical error handler
 */
save_spsuper:
        .ds.l    1              /* save area for contents of UDA_SPSUPER */
save_spcriterr:
        .ds.l    1              /* save area for stack ptr on entry */

/*
 *  the following private stack used to be 512 words, but that wasn't
 *  enough in all cases after some updates around nov/2016 increased
 *  just_draw()'s stack usage slightly.
 */
        .ds.w   768             /* private stack */
criterr_stack:

/*
 * miscellaneous stacks
 */
        .ds.b    0x80
gstack:                         /* gsx stack for mouse */

        .ds.b    0x80
tstack:                         /* tick stack */
        .ds.l    1
#endif
