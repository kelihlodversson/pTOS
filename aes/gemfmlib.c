/*      GEMFMLIB.C      03/15/84 - 06/16/85     Gregg Morris            */
/*      merge High C vers. w. 2.2 & 3.0         8/20/87         mdf     */

/*
*       Copyright 1999, Caldera Thin Clients, Inc.
*                 2002-2022 The EmuTOS development team
*
*       This software is licenced under the GNU Public License.
*       Please see LICENSE.TXT for further information.
*
*                  Historical Copyright
*       -------------------------------------------------------------
*       GEM Application Environment Services              Version 2.3
*       Serial No.  XXXX-0000-654321              All Rights Reserved
*       Copyright (C) 1987                      Digital Research Inc.
*       -------------------------------------------------------------
*/

#include "emutos.h"
#include "string.h"
#include "struct.h"
#include "aesdefs.h"
#include "aesext.h"
#include "aesvars.h"
#include "obdefs.h"
#include "gem_rsc.h"
#include "biosbind.h"

#include "gemwmlib.h"
#include "gemrslib.h"
#include "geminput.h"
#include "gemctrl.h"
#include "gemoblib.h"
#include "gemobjop.h"
#include "gemgrlib.h"
#include "gemevlib.h"
#include "gemobed.h"
#include "optimize.h"
#include "gemfmalt.h"
#include "geminit.h"
#include "gemmnlib.h"
#include "gemfmlib.h"
#include "gemformkey.h"
#include "scancode.h"
#if ARCH_X86_64
#include "aes_objects_kernel.h"
#endif




/* Global variables: */
WORD     ml_ocnt;    /* Needs to be 0 initially! */


/* Local variables: */
static OBJECT   *ml_mnhold;
#if ARCH_X86_64
static AES_TREE_STATE *ml_tree_guard;
#endif
static GRECT    ml_ctrl;
static AESPD    *ml_pmown;

/*
 * The following arrays are used by eralert() to generate values to
 * pass to fm_show().  eralert() is passed an alert number N which is
 * used to index into these arrays:
 *  ml_alrt[N]  contains the object number of the alert string within
 *              the resource
 *  ml_pwlv[N]  contains a 2-byte value PPLL where:
 *              LL is the alert level (0-3 currently supported)
 *              PP is a drive letter indicator; if non-zero, a pointer
 *              to the drive letter corresponding to drive number D is
 *              passed to fm_show(); otherwise a null pointer is passed
 *
 * These arrays must be synchronized with "err_tbl" in gemdosif.S.
 */
static const WORD ml_alrt[] =
    {AL00CRT,AL01CRT,AL02CRT,AL03CRT,AL04CRT,AL05CRT};
static const WORD ml_pwlv[] =
    {0x0102,0x0102,0x0102,0x0101,0x0002,0x0001};



void fm_own(WORD beg_ownit)
{
    if (beg_ownit)
    {
        wm_update(BEG_UPDATE);
        if (ml_ocnt == 0)
        {
            ml_mnhold = gl_mntree;
#if ARCH_X86_64
            ml_tree_guard = x86_64_aes_tree_pin(ml_mnhold);
#endif
            gl_mntree = NULL;
            get_ctrl(&ml_ctrl);
            get_mown(&ml_pmown);
            ct_chgown(rlr, &gl_rscreen);
        }
        ml_ocnt++;
    }
    else
    {
        ml_ocnt--;
        if (ml_ocnt == 0)
        {
            ct_chgown(ml_pmown, &ml_ctrl);
            gl_mntree = ml_mnhold;
#if ARCH_X86_64
            if (!x86_64_aes_tree_alive(ml_tree_guard))
                gl_mntree = NULL;
            x86_64_aes_tree_unpin(ml_tree_guard);
            ml_tree_guard = NULL;
#endif
        }
        wm_update(END_UPDATE);
    }
}


/*
 *  Form DIALogue routine to handle visual effects of drawing and
 *  undrawing a dialogue
 */
WORD fm_dial(WORD fmd_type, GRECT *pi, GRECT *pt)
{
    /* adjust tree position */
    gsx_sclip(&gl_rscreen);
    switch(fmd_type)
    {
    case FMD_START:
        /* grab screen sync or some other mutual exclusion method */
        break;
    case FMD_GROW:
        gr_growbox(pi, pt);
        break;
    case FMD_SHRINK:
        gr_shrinkbox(pi, pt);
        break;
    case FMD_FINISH:
        /* update certain portion of the screen */
        w_drawdesk(pt);
        w_update(DESKWH, pt, DESKWH, FALSE);
        break;
    }

    return TRUE;
}


/*
 * eralert() is called by the graphics critical alert handler
 * in gemdosif.S when a DOS error occurs.  it issues an alert
 * selected by the lookup tables ml_alrt[] & ml_pwlv[].
 *
 * input:   n   alert number (0-5), not checked
 *          d   drive number
 * returns: FALSE   user selected button 1 of the alert
 *                  (assumed to be Cancel)
 *          TRUE    user selected button 2 or 3 of the alert
 *                  (assumed to be Retry)
 */
WORD eralert(WORD n, WORD d)
{
    WORD    level;
    WORD    drive_let;
    WORD    *pwd;

    drive_let = 'A' + d;

    level = LOBYTE(ml_pwlv[n]);
    pwd = HIBYTE(ml_pwlv[n]) ? &drive_let : NULL;

    if (fm_show(ml_alrt[n], pwd, level) == 1)
        return FALSE;

    /* retry requested, we'll be busy again */
    set_mouse_to_hourglass();   /* set mouse to hourglass */
    return TRUE;
}
