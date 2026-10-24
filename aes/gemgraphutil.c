/* Shared GEM outline drawing and geometry, from gemgraf.c.
 * Copyright 1999, Caldera Thin Clients, Inc.
 * Copyright 2002-2026 The EmuTOS development team. GPL version 2 or later. */
#include "emutos.h"
#include "funcdef.h"
#include "obdefs.h"
#include "aesext.h"
#include "gemgraf.h"
#include "gemgsxif.h"

void gsx_xline(WORD ptscount, WORD *ppoints)
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
        v_pline(2, ppoints);
        ppoints += 2;
    }
    vsl_udsty(0xffff);
}


/*
 *  Routine to set up the points for drawing a box
 */
void gsx_bxpts(GRECT *pt)
{
    ptsin[0] = pt->g_x;
    ptsin[1] = pt->g_y;
    ptsin[2] = pt->g_x + pt->g_w - 1;
    ptsin[3] = pt->g_y;
    ptsin[4] = pt->g_x + pt->g_w - 1;
    ptsin[5] = pt->g_y + pt->g_h - 1;
    ptsin[6] = pt->g_x;
    ptsin[7] = pt->g_y + pt->g_h - 1;
    ptsin[8] = pt->g_x;
    ptsin[9] = pt->g_y;
}


/*
 *  Routine to draw a box that will look right on a dithered surface
 */
void gsx_xbox(GRECT *pt)
{
    gsx_bxpts(pt);
    gsx_xline(5, ptsin);
}


/*
 *  Routine to draw a portion of the corners of a box that will look
 *  right on a dithered surface
 */
void gsx_xcbox(GRECT *pt)
{
    WORD    wa, ha;

    wa = 2 * gl_wbox;
    ha = 2 * gl_hbox;
    ptsin[0] = pt->g_x;
    ptsin[1] = pt->g_y + ha;
    ptsin[2] = pt->g_x;
    ptsin[3] = pt->g_y;
    ptsin[4] = pt->g_x + wa;
    ptsin[5] = pt->g_y;
    gsx_xline(3, ptsin);

    ptsin[0] = pt->g_x + pt->g_w - wa;
    ptsin[1] = pt->g_y;
    ptsin[2] = pt->g_x + pt->g_w - 1;
    ptsin[3] = pt->g_y;
    ptsin[4] = pt->g_x + pt->g_w - 1;
    ptsin[5] = pt->g_y + ha;
    gsx_xline(3, ptsin);

    ptsin[0] = pt->g_x + pt->g_w - 1;
    ptsin[1] = pt->g_y + pt->g_h - ha;
    ptsin[2] = pt->g_x + pt->g_w - 1;
    ptsin[3] = pt->g_y + pt->g_h - 1;
    ptsin[4] = pt->g_x + pt->g_w - wa;
    ptsin[5] = pt->g_y + pt->g_h - 1;
    gsx_xline(3, ptsin);

    ptsin[0] = pt->g_x + wa;
    ptsin[1] = pt->g_y + pt->g_h - 1;
    ptsin[2] = pt->g_x;
    ptsin[3] = pt->g_y + pt->g_h - 1;
    ptsin[4] = pt->g_x;
    ptsin[5] = pt->g_y + pt->g_h - ha;
    gsx_xline(3, ptsin);
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


