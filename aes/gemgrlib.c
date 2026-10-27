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


static void gr_setup(WORD color)
{
    gsx_sclip(&gl_rscreen);
    gsx_attr(FALSE, MD_XOR, color);
}


static void gr_clamp(WORD xorigin, WORD yorigin, WORD wmin, WORD hmin,
                     WORD *pneww, WORD *pnewh)
{
    *pneww = max(xrat - xorigin + 1, wmin);
    *pnewh = max(yrat - yorigin + 1, hmin);
}


static void gr_draw(WORD have2box, GRECT *po, GRECT *poff)
{
    GRECT   t;

    gsx_xbox(po);
    if (have2box)
    {
        r_set(&t, po->g_x + poff->g_x, po->g_y + poff->g_y,
                  po->g_w + poff->g_w, po->g_h + poff->g_h);
        gsx_xbox(&t);
    }
}


static BOOL gr_wait(GRECT *po, GRECT *poff)
{
    WORD have2box;
    BOOL down;

    have2box = !rc_equal(&gl_rzero, poff);

    /* draw/erase old */
    gsx_moff();
    gr_draw(have2box, po, poff);
    gsx_mon();

    /* wait for change */
    down = gr_stilldn(TRUE, xrat, yrat, 1, 1);

    /* draw/erase old */
    gsx_moff();
    gr_draw(have2box, po, poff);
    gsx_mon();

    /* return exit event */
    return down;
}


/*
 *  Stretch the free corner of an XOR box(w,h) that is pinned at
 *  another corner, based on mouse movement until the button comes up.
 *  Also draw a second box offset from the stretching box.
 */
void gr_rubwind(WORD xorigin, WORD yorigin, WORD wmin, WORD hmin,
                GRECT *poff, WORD *pwend, WORD *phend)
{
    BOOL    down;
    GRECT   o;

    wm_update(BEG_UPDATE);
    gr_setup(BLACK);

    r_set(&o, xorigin, yorigin, 0, 0);

    /* clamp size of rubber box to no smaller than wmin, hmin */
    do
    {
        gr_clamp(o.g_x, o.g_y, wmin, hmin, &o.g_w, &o.g_h);
        down = gr_wait(&o, poff);
    } while (down);

    *pwend = o.g_w;
    *phend = o.g_h;
    wm_update(END_UPDATE);
}


/*
 *  Stretch the free corner of an XOR box(w,h) that is pinned at
 *  another corner, based on mouse movement until the button comes up.
 *  This is also called a rubber-band box.
 */
void gr_rubbox(WORD xorigin, WORD yorigin, WORD wmin, WORD hmin,
               WORD *pwend, WORD *phend)
{
    gr_rubwind(xorigin, yorigin, wmin, hmin, &gl_rzero, pwend, phend);
}


/*
 *  Drag a moving XOR box(w,h) that tracks relative to the mouse until the
 *  button comes up.  The starting x and y represent the location of the
 *  upper left hand corner of the rectangle relative to the mouse position.
 *  This relative distance should be maintained.  A constraining rectangle
 *  is also given.  The box should not be able to be dragged out of the
 *  constraining rectangle.
 */
void gr_dragbox(WORD w, WORD h, WORD sx, WORD sy, GRECT *pc,
                WORD *pdx, WORD *pdy)
{
    WORD    offx, offy;
    BOOL    down;
    GRECT   o;

    wm_update(BEG_UPDATE);
    gr_setup(BLACK);

    gr_clamp(sx+1, sy+1, 0, 0, &offx, &offy);
    r_set(&o, sx, sy, w, h);

    /* get box's x,y from mouse's x,y then constrain result */
    do
    {
        o.g_x = xrat - offx;
        o.g_y = yrat - offy;
        rc_constrain(pc, &o);
        down = gr_wait(&o, &gl_rzero);
    } while (down);

    *pdx = o.g_x;
    *pdy = o.g_y;
    wm_update(END_UPDATE);
}


WORD gr_slidebox(OBJECT *tree, WORD parent, WORD obj, WORD isvert)
{
    GRECT   t, c;
    WORD    divnd, divis;
    BOOL    xy_adjust;
    MAYBE_UNUSED(xy_adjust);

    ob_actxywh(tree, parent, &c);
    ob_relxywh(tree, obj, &t);

#if CONF_WITH_3D_OBJECTS
    xy_adjust = FALSE;

    /*
     * if the child is 3D, adjust its width & height and
     * (iff the parent is non-3D) its position too
     */
    if (tree[obj].ob_flags & FL3DOBJ)
    {
        t.g_w += 2 * ADJ3DSTD;
        t.g_h += 2 * ADJ3DSTD;
        if (!(tree[parent].ob_flags & FL3DOBJ))
        {
            t.g_x -= ADJ3DSTD;
            t.g_y -= ADJ3DSTD;
            xy_adjust = TRUE;
        }
    }
#endif

    gr_dragbox(t.g_w, t.g_h, t.g_x + c.g_x, t.g_y + c.g_y,
                &c, &t.g_x, &t.g_y);

    if (isvert)
    {
        divnd = t.g_y - c.g_y;
        divis = c.g_h - t.g_h;
    }
    else
    {
        divnd = t.g_x - c.g_x;
        divis = c.g_w - t.g_w;
    }

#if CONF_WITH_3D_OBJECTS
    if (xy_adjust && divnd)
        divnd += ADJ3DSTD;
    if (divnd > divis)
        divnd = divis;
#endif

    if (divis)
        return mul_div_round(divnd, 1000, divis);
    else
        return 0;
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
