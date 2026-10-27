/*      GEMGRAF.C       04/11/84 - 09/17/85     Lee Lorenzen            */
/*      merge High C vers. w. 2.2               8/21/87         mdf     */
/*      fix gr_gicon null text                  11/18/87        mdf     */

/*
*       Copyright 1999, Caldera Thin Clients, Inc.
*                 2002-2025 The EmuTOS development team
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

/* #define ENABLE_KDEBUG */

#include "emutos.h"
#include "obdefs.h"
#include "aesdefs.h"
#include "aesext.h"
#include "intmath.h"
#include "funcdef.h"

#include "gemgraf.h"
#include "gemgsxif.h"
#include "optimopt.h"
#include "gsx2.h"
#include "gemrender.h"
#include "rectfunc.h"


#include "gemrender_binding.h"
#if OB_RENDER_COLOUR_ICONS
#include "gemdos.h"
#endif

static void gr_gblt(WORD *pimage, GRECT *pi, WORD col1, WORD col2)
{
    gsx_blt(pimage, 0, 0, pi->g_x, pi->g_y,
            pi->g_w, pi->g_h, MD_TRANS, col1, col2);
}


#if OB_RENDER_COLOUR_ICONS
/*
 *  Blit a previously-transformed colour icon from memory to screen.
 *  The write mode is backend-owned: planar keeps S_OR_D (mask-ANDed
 *  planes composed over the mask blit), packed truecolor needs
 *  BM_S_ONLY so RGB565 values are replaced, not ORed.
 */
static void gr_colourblit(WORD *pdata, GRECT *pi, WORD num_planes)
{
    WORD pxyarray[8];

    gsx_fix(&gl_src, pdata, pi->g_w/8, pi->g_h);
    gl_src.fd_nplanes = num_planes;

    gsx_fix_screen(&gl_dst);
    gl_dst.fd_nplanes = num_planes;

    gsx_moff();
    pxyarray[0] = 0;
    pxyarray[1] = 0;
    pxyarray[2] = pi->g_w - 1;
    pxyarray[3] = pi->g_h - 1;
    pxyarray[4] = pi->g_x;
    pxyarray[5] = pi->g_y;
    pxyarray[6] = pi->g_x + pi->g_w - 1;
    pxyarray[7] = pi->g_y + pi->g_h - 1;
    gsx_mon();

    vro_cpyfm(vdi_colour_blit_mode(), pxyarray, &gl_src, &gl_dst);
}


/*
 *  Create a dithered copy of the specified mask and return its address
 *
 *  returns NULL if memory can't be allocated
 */
static WORD *dither_mask(WORD *pdata, WORD w, WORD h)
{
    WORD width = w / 16;    /* in WORDs */
    WORD i, j, dither;
    WORD *mask, *p, *q;

    mask = dos_alloc_anyram((LONG)width * sizeof(WORD) * h);
    if (!mask)
        return NULL;

    for (i = 0, p = pdata, q = mask; i < h; i++)
    {
        dither = (i & 1) ? 0xaaaa : 0x5555;
        for (j = 0; j < width; j++)
            *q++ = *p++ & dither;
    }

    return mask;
}
#endif


/*
 *  Routine to draw a mono or colour icon
 *
 *  Iff colour icons are enabled, and 'cicon' is not NULL, this will
 *  draw a colour icon
 */
void gr_gicon(WORD state, ICONBLK *ib, CICON *cicon)
{
    WORD    *pdata, *pmask;
    WORD    fgcol, bgcol, tmp;
    WORD    ch;
    GRECT   *pi, *pt;

    ch = ib->ib_char;
    pi = (GRECT *)&ib->ib_xicon;
    pt = (GRECT *)&ib->ib_xtext;

    /* crack the color/char definition word */
    fgcol = (ch >> 12) & 0x000f;
    bgcol = (ch >> 8) & 0x000f;
    ch &= 0x00ff;

#if OB_RENDER_COLOUR_ICONS
    /* set up the data & mask ptrs */
    if (cicon)
    {
        /* use the 'selected' data/mask if available */
        if ((state & SELECTED) && cicon->sel_data)
        {
            pdata = cicon->sel_data;
            pmask = cicon->sel_mask;
        }
        else
        {
            pdata = cicon->col_data;
            pmask = cicon->col_mask;
        }
    }
    else
#endif
    {
        if (state & SELECTED)       /* invert mono icon colours if selected   */
        {
            tmp = fgcol;
            fgcol = bgcol;
            bgcol = tmp;
        }
        pdata = ib->ib_pdata;
        pmask = ib->ib_pmask;
    }

    /* do mask & text background unless WHITEBAK is set & the icon's background is white */
    if ( !((state & WHITEBAK) && (bgcol == WHITE)) )
    {
        /* NOTE: this may need to be modified for 16-bit colour */
        gr_gblt(pmask, pi, bgcol, fgcol);
        /* do not draw background rectangle for icon text if the string is empty */
        if (ib->ib_ptext[0])
        {
            if (cicon && (state & SELECTED))
                gr_rect(fgcol, IP_SOLID, pt);
            else
                gr_rect(bgcol, IP_SOLID, pt);
        }
    }

    /* draw the image */
#if OB_RENDER_COLOUR_ICONS
    if (cicon)
    {
        gr_colourblit(pdata, pi, cicon->num_planes);
        if (state & SELECTED)
        {
            tmp = fgcol;
            fgcol = bgcol;
            bgcol = tmp;
            if (!cicon->sel_data)       /* check if we need to darken */
            {
                WORD *mask = dither_mask(pmask, pi->g_w, pi->g_h);
                if (mask)               /* malloc ok */
                {
                    gr_gblt(mask, pi, BLACK, WHITE);
                    dos_free(mask);
                }
            }
        }
    }
    else
#endif
    {
        gr_gblt(pdata, pi, fgcol, bgcol);
    }

    /* draw the character */
    gsx_attr(TRUE, MD_TRANS, fgcol);
    if (ch)
    {
        intin[0] = ch;
        gsx_tblt(SMALL, ib->ib_xicon+ib->ib_xchar, ib->ib_yicon+ib->ib_ychar, 1);
    }

    /* draw the label */
    gr_gtext(TE_CNTR, SMALL, ib->ib_ptext, pt);
}
