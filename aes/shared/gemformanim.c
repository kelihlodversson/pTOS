/* Original GEM dialog box animations.
 * Copyright 1999, Caldera Thin Clients, Inc.
 * Copyright 2002-2026 The EmuTOS development team. GPL version 2 or later. */
#include "aescontext.h"
#include "emutos.h"
#include "obdefs.h"
#include "aesext.h"
#include "intmath.h"
#include "gemgraf.h"
#include "gemgsxif.h"
#include "gemgrlib.h"
#ifdef AES_ANIM_BINDINGS
#include AES_ANIM_BINDINGS
#else
#include "gemformanim.h"
#endif

static void gr_scale(AES_RENDER_PARAM WORD xdist, WORD ydist, WORD *pcnt, WORD *pxstep, WORD *pystep)
{
    WORD i;
    WORD dist;


    gr_setup(AES_RENDER_ARG0);

    dist = (xdist + ydist) / 2;

    for (i = 0; dist; i++)
        dist /= 2;

    if ((*pcnt = i) != 0)
    {
        *pxstep = max(1, divs(xdist, i));
        *pystep = max(1, divs(ydist, i));
    }
    else
        *pxstep = *pystep = 1;
}

#ifdef AES_ANIM_BINDINGS
/* Let the userspace adapter admit the exact native trajectory before drawing.
 * Native builds retain their original code and object size. */
void aes_animation_scale(AES_RENDER_PARAM WORD xdist, WORD ydist, WORD *count, WORD *xstep, WORD *ystep)
{
    gr_scale(AES_RENDER_ARG xdist,ydist,count,xstep,ystep);
}
#endif


void gr_stepcalc(AES_RENDER_PARAM WORD orgw, WORD orgh, GRECT *pt, WORD *pcx, WORD *pcy,
                 WORD *pcnt, WORD *pxstep, WORD *pystep)
{
    *pcx = (pt->g_w/2) - (orgw/2);
    *pcy = (pt->g_h/2) - (orgh/2);

    gr_scale(AES_RENDER_ARG *pcx, *pcy, pcnt, pxstep, pystep);

    *pcx += pt->g_x;
    *pcy += pt->g_y;
}


static void gr_xor(AES_RENDER_PARAM WORD clipped, WORD cnt, WORD cx, WORD cy, WORD cw, WORD ch,
                   WORD xstep, WORD ystep, WORD dowdht)
{
    GRECT tmprect;

    do
    {
        tmprect.g_x = cx;
        tmprect.g_y = cy;
        tmprect.g_w = cw;
        tmprect.g_h = ch;

        if (clipped)
            gsx_xcbox(AES_RENDER_ARG &tmprect);
        else
            gsx_xbox(AES_RENDER_ARG &tmprect);

        cx -= xstep;
        cy -= ystep;
        if (dowdht)
        {
            cw += (2 * xstep);
            ch += (2 * ystep);
        }
    } while(cnt--);
}


void gr_2box(AES_RENDER_PARAM WORD flag1, WORD cnt, GRECT *pt, WORD xstep, WORD ystep, WORD flag2)
{
    WORD i;

    gsx_moff();
    for (i = 0; i < 2; i++)
        gr_xor(AES_RENDER_ARG flag1, cnt, pt->g_x, pt->g_y, pt->g_w, pt->g_h, xstep, ystep, flag2);
    gsx_mon();
}


/*
 *  Draw a moving XOR box(w,h) that moves from a point whose upper
 *  left corner is at src_x, src_y to a point at dst_x, dst_y
 */

void gr_movebox(AES_RENDER_PARAM WORD w, WORD h, WORD srcx, WORD srcy, WORD dstx, WORD dsty)
{
    WORD    signx, signy;
    WORD    cnt;
    WORD    xstep, ystep;
    GRECT   t;

    r_set(&t, srcx, srcy, w, h);

    signx = (srcx < dstx) ? -1 : 1;
    signy = (srcy < dsty) ? -1 : 1;

    gr_scale(AES_RENDER_ARG signx*(srcx-dstx), signy*(srcy-dsty), &cnt, &xstep, &ystep);

    gr_2box(AES_RENDER_ARG FALSE, cnt, &t, signx*xstep, signy*ystep, FALSE);
}


/*
 *  Draw a small box that moves from the origin x,y to a spot centered
 *  within the rectangle and then expands to match the size of the rectangle
 */
void gr_growbox(AES_RENDER_PARAM GRECT *po, GRECT *pt)
{
    WORD    cx, cy;
    WORD    cnt, xstep, ystep;

    gr_stepcalc(AES_RENDER_ARG po->g_w, po->g_h, pt, &cx, &cy, &cnt, &xstep, &ystep);
    gr_movebox(AES_RENDER_ARG po->g_w, po->g_h, po->g_x, po->g_y, cx, cy);
    po->g_x = cx;
    po->g_y = cy;
    gr_2box(AES_RENDER_ARG TRUE, cnt, po, xstep, ystep, TRUE);
}


/*
 *  Draw a box that shrinks from the rectangle given down around a small
 *  box centered within the rectangle and then moves to the origin point
 */
void gr_shrinkbox(AES_RENDER_PARAM GRECT *po, GRECT *pt)
{
    WORD    cx, cy;
    WORD    cnt, xstep, ystep;

    gr_stepcalc(AES_RENDER_ARG po->g_w, po->g_h, pt, &cx, &cy, &cnt, &xstep, &ystep);
    gr_2box(AES_RENDER_ARG TRUE, cnt, pt, -xstep, -ystep, TRUE);
    gr_movebox(AES_RENDER_ARG po->g_w, po->g_h, cx, cy, po->g_x, po->g_y);
}




