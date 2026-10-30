/* Original GEM rubber, drag and slide tracking from gemgrlib.c.
 * Copyright 1999, Caldera Thin Clients, Inc.
 * Copyright 2002-2026 The EmuTOS development team. GPL version 2 or later. */
#include "emutos.h"
#include "obdefs.h"
#include "aesdefs.h"
#include "aesext.h"
#include "gemgraf.h"
#include "gemgsxif.h"
#include "gemgrlib.h"
#include "gemoblib.h"
#include "gemobjop.h"
#include "gemformanim.h"
#include "gemwatch.h"
#include "rectfunc.h"
#include "intmath.h"
#ifdef AES_GRTRACK_BINDINGS
#include AES_GRTRACK_BINDINGS
#else
#include "struct.h"
#include "geminput.h"
#include "gemwmlib.h"
#include "aesvars.h"
#include "asm.h"
#define GRTRACK_3D CONF_WITH_3D_OBJECTS
#endif

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
    gr_setup();

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
    gr_setup();

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

#if GRTRACK_3D
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

#if GRTRACK_3D
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


