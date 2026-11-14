/* Shared GEM outline drawing and geometry, from gemgraf.c.
 * Copyright 1999, Caldera Thin Clients, Inc.
 * Copyright 2002-2026 The EmuTOS development team. GPL version 2 or later. */
#include "aescontext.h"
#include "emutos.h"
#include "funcdef.h"
#include "obdefs.h"
#include "aesext.h"
#include "gemgraf.h"
#include "gemgsxif.h"
#include "gemrender.h"
#include "gemrender_binding.h"

void gsx_xline(AES_RENDER_PARAM WORD ptscount, WORD *ppoints)
{
    static const WORD hztltbl[2] = { 0x5555, 0xaaaa };
    static const WORD verttbl[4] = { 0x5555, 0xaaaa, 0xaaaa, 0x5555 };
    WORD    *linexy, i;
    WORD    st;

    for (i = 1; i < ptscount; i++)
    {
        if (*ppoints == *(ppoints + 2))
        {
            st = verttbl[((*ppoints & 1) | ((*(ppoints+1) & 1) << 1))];
        }
        else
        {
            linexy = (*ppoints < *(ppoints+2)) ? ppoints : ppoints + 2;
            st = hztltbl[*(linexy+1) & 1];
        }
        vsl_udsty(st);
        v_pline(AES_RENDER_ARG 2, ppoints);
        ppoints += 2;
    }
    vsl_udsty(0xffff);
}


/*
 *  Routine to set up the points for drawing a box
 */
void gsx_bxpts(AES_RENDER_PARAM GRECT *pt)
{
    WORD left = pt->g_x, top = pt->g_y;
    WORD right = left + pt->g_w - 1, bottom = top + pt->g_h - 1;

    ptsin[0] = ptsin[6] = ptsin[8] = left;
    ptsin[1] = ptsin[3] = ptsin[9] = top;
    ptsin[2] = ptsin[4] = right;
    ptsin[5] = ptsin[7] = bottom;
}


/*
 *  Routine to draw a box that will look right on a dithered surface
 */
void gsx_xbox(AES_RENDER_PARAM GRECT *pt)
{
    gsx_bxpts(AES_RENDER_ARG pt);
    gsx_xline(AES_RENDER_ARG 5, ptsin);
}


/*
 *  Routine to draw a portion of the corners of a box that will look
 *  right on a dithered surface
 */
void gsx_xcbox(AES_RENDER_PARAM GRECT *pt)
{
    WORD    wa, ha;
    WORD    left = pt->g_x, top = pt->g_y;
    WORD    right = left + pt->g_w, bottom = top + pt->g_h;

    wa = 2 * gl_wbox;
    ha = 2 * gl_hbox;
    ptsin[0] = left;
    ptsin[1] = top + ha;
    ptsin[2] = left;
    ptsin[3] = top;
    ptsin[4] = left + wa;
    ptsin[5] = top;
    gsx_xline(AES_RENDER_ARG 3, ptsin);

    ptsin[0] = right - wa;
    ptsin[1] = top;
    ptsin[2] = right - 1;
    ptsin[3] = top;
    ptsin[4] = right - 1;
    ptsin[5] = top + ha;
    gsx_xline(AES_RENDER_ARG 3, ptsin);

    ptsin[0] = right - 1;
    ptsin[1] = bottom - ha;
    ptsin[2] = right - 1;
    ptsin[3] = bottom - 1;
    ptsin[4] = right - wa;
    ptsin[5] = bottom - 1;
    gsx_xline(AES_RENDER_ARG 3, ptsin);

    ptsin[0] = left + wa;
    ptsin[1] = bottom - 1;
    ptsin[2] = left;
    ptsin[3] = bottom - 1;
    ptsin[4] = left;
    ptsin[5] = bottom - ha;
    gsx_xline(AES_RENDER_ARG 3, ptsin);
}


/*
 *  Routine to convert a rectangle to its inside dimensions
 */
void gr_inside(GRECT *pt, WORD th)
{
    pt->g_x += th;
    pt->g_y += th;
    pt->g_w -= ( 2 * th );
    pt->g_h -= ( 2 * th );
}
