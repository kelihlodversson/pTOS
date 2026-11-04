/*      GEMGRLIB.C      4/11/84 - 09/20/85      Gregg Morris            */
/*      merge High C vers. w. 2.2               8/21/87         mdf     */

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
#include "struct.h"
#include "obdefs.h"
#include "aesdefs.h"
#include "aesext.h"
#include "aesvars.h"
#include "gemlib.h"

#include "geminit.h"
#include "gemevlib.h"
#include "gemgraf.h"
#include "gemwmlib.h"
#include "gemoblib.h"
#include "geminput.h"
#include "geminit.h"
#include "gemgsxif.h"
#include "gemgrlib.h"
#include "gemwatch.h"
#include "gemformanim.h"
#include "gemasm.h"
#include "rectfunc.h"

#include "intmath.h"
#include "asm.h"

/*
 *  Routine to watch the mouse while the button is down and it stays
 *  inside/outside of the specified rectangle.  Return TRUE as long as
 *  the mouse is down.  Block until the mouse moves into or out of the
 *  specified rectangle.
 */
BOOL gr_stilldn(BOOL out, WORD x, WORD y, WORD w, WORD h)
{
    WORD    rets[6];
    MOBLK   tmpmoblk;
    WORD    which;

    dsptch();

    tmpmoblk.m_out = out;
    tmpmoblk.m_gr.g_x = x;
    tmpmoblk.m_gr.g_y = y;
    tmpmoblk.m_gr.g_w = w;
    tmpmoblk.m_gr.g_h = h;

#if CONF_WITH_MENU_EXTENSION
    which = ev_multi(MU_KEYBD | MU_BUTTON | MU_M1, &tmpmoblk, NULL,
                    NULL, 0x0L, 0x0001ff00L, NULL, rets);
#else
    which = ev_multi(MU_KEYBD | MU_BUTTON | MU_M1, &tmpmoblk,
                    NULL, 0x0L, 0x0001ff00L, NULL, rets);
#endif

    if (which & MU_BUTTON)
        return FALSE;

    return TRUE;
}


void gr_setup(void)
{
    gsx_sclip(&gl_rscreen);
    gsx_attr(FALSE, MD_XOR, BLACK);
}


/*
 * handle graf_mouse() processing
 */
void gr_mouse(WORD mode, MFORM *maddr)
{
#if CONF_WITH_GRAF_MOUSE_EXTENSION
    MFORM temp;
#endif

    switch(mode)
    {
    default:
        if (mode > USER_DEF)    /* unknown mode beyond M_OFF/M_ON: ignore, as before */
            break;
        if ((mode < ARROW) || (mode > OUTLN_CROSS))
            mode = ARROW;       /* fail safe */
        maddr = mouse_cursor[mode];
        FALLTHROUGH;
    case USER_DEF:
        gsx_mfset(maddr);
        break;
    case M_OFF:
        gsx_moff();
        break;
    case M_ON:
        gsx_mon();
        break;
#if CONF_WITH_GRAF_MOUSE_EXTENSION
    case M_SAVE:
        rlr->p_mouse = gl_mouse;
        break;
    case M_RESTORE:
        maddr = &rlr->p_mouse;
        gsx_mfset(maddr);
        break;
    case M_PREVIOUS:
        /* we must use a temp because gsx_mfset() updates gl_prevmouse */
        temp = gl_prevmouse;
        maddr = &temp;
        gsx_mfset(maddr);
        break;
#endif
    }
}


void gr_mkstate(WORD *pmx, WORD *pmy, WORD *pmstat, WORD *pkstat)
{
    *pmx = xrat;
    *pmy = yrat;
    *pmstat = button;
    *pkstat = kstate;
}
