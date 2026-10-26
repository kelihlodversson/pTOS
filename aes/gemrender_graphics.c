/* Shared original AES drawing code.
 * Copyright 1999, Caldera Thin Clients, Inc.
 * Copyright 2002-2026 The EmuTOS development team. GPL version 2 or later. */
#include "emutos.h"
#include "obdefs.h"
#include "aesdefs.h"
#include "aesext.h"
#include "funcdef.h"
#include "gemgsxif.h"
#include "gemobjop.h"
#include "gemgraf.h"
#include "optimopt.h"
#include "rectfunc.h"
#include "intmath.h"
#include "gemoblib.h"
#include "string.h"
#include "gsx2.h"
#include "gemrender.h"
#include "gemrender_binding.h"

/*
 *  Routine to set the clip rectangle.  If the w,h of the clip is 0,
 *  then no clip should be set.  Otherwise, set the appropriate clip.
 */
void gsx_sclip(const GRECT *pt)
{
    gl_clip = *pt;

    if (gl_clip.g_w && gl_clip.g_h)
    {
        ptsin[0] = gl_clip.g_x;
        ptsin[1] = gl_clip.g_y;
        ptsin[2] = gl_clip.g_x + gl_clip.g_w - 1;
        ptsin[3] = gl_clip.g_y + gl_clip.g_h - 1;
        vs_clip( TRUE, ptsin);
    }
    else
        vs_clip( FALSE, ptsin);
}


/*
 *  Routine to get the current clip setting
 */
void gsx_gclip(GRECT *pt)
{
    *pt = gl_clip;
}


/*
 *  Routine to return TRUE iff the specified rectangle intersects the
 *  current clip rectangle ... or clipping is off (?)
 */
WORD gsx_chkclip(GRECT *pt)
{
    /* if clipping is on */
    if (gl_clip.g_w && gl_clip.g_h)
    {
        if ((pt->g_y + pt->g_h) < gl_clip.g_y)
            return FALSE;                   /* all above    */
        if ((pt->g_x + pt->g_w) < gl_clip.g_x)
            return FALSE;                   /* all left     */
        if ((gl_clip.g_y + gl_clip.g_h) <= pt->g_y)
            return FALSE;                   /* all below    */
        if ((gl_clip.g_x + gl_clip.g_w) <= pt->g_x)
            return FALSE;                   /* all right    */
    }

    return TRUE;
}

/*
 *  Routine to draw a clipped polyline, hiding the mouse as you do it
 */
void gsx_cline(UWORD x1, UWORD y1, UWORD x2, UWORD y2)
{
    WORD pxy[4] = { x1, y1, x2, y2 };

    gsx_moff();
    v_pline(2, pxy);
    gsx_mon();
}


/*
 *  Routine to set the text, writing mode, and color attributes
 */
void gsx_attr(UWORD text, UWORD mode, UWORD color)
{
    WORD    tmp;

    tmp = intin[0];
    contrl.nptsin = 0;
    contrl.nintin = 1;
    contrl.handle = gl_handle;
    if (mode != gl_mode)
    {
        contrl.code = SET_WRITING_MODE;
        intin[0] = gl_mode = mode;
        gsx2();
    }

    contrl.code = 0;
    if (text)
    {
        if (color != gl_tcolor)
        {
            contrl.code = SET_TEXT_COLOR;
            gl_tcolor = color;
        }
    }
    else
    {
        if (color != gl_lcolor)
        {
            contrl.code = SET_LINE_COLOR;
            gl_lcolor = color;
        }
    }

    if (contrl.code)
    {
        intin[0] = color;
        gsx2();
    }
    intin[0] = tmp;
}


/*
 *  Routine to draw a box using the current attributes
 */
static void gsx_box(GRECT *pt)
{
    gsx_bxpts(pt);
    v_pline(5, ptsin);
}

/*
 *  Routine to do a filled bit blit (a rectangle)
 */
void bb_fill(WORD mode, WORD fis, WORD patt, WORD hx, WORD hy, WORD hw, WORD hh)
{
    gsx_attr(TRUE, mode, gl_tcolor);
    if (fis != gl_fis)
    {
        vsf_interior(fis);
        gl_fis = fis;
    }
    if (patt != gl_patt)
    {
        vsf_style(patt);
        gl_patt = patt;
    }

    ptsin[0] = hx;
    ptsin[1] = hy;
    ptsin[2] = hx + hw - 1;
    ptsin[3] = hy + hh - 1;
    vr_recfl(ptsin);
}


static UWORD ch_width(WORD fn)
{
    if (fn == IBM)
        return gl_wchar;

    if (fn == SMALL)
        return gl_wschar;

    return 0;
}


static UWORD ch_height(WORD fn)
{
    if (fn == IBM)
        return gl_hchar;

    if (fn == SMALL)
        return gl_hschar;

    return 0;
}


static void gsx_tcalc(WORD font, char *ptext, WORD *ptextw, WORD *ptexth, WORD *pnumchs)
{
    WORD    wc, hc;

    wc = ch_width(font);
    hc = ch_height(font);

    /* figure out the width of the text string in pixels */
    *pnumchs = expand_string(intin, ptext);
    *ptextw = min(*ptextw, (WORD)muls(*pnumchs, wc));

    /* figure out the height of the text */
    *ptexth = min(*ptexth, hc);
    if (divs(*ptexth, hc))
        *pnumchs = min(*pnumchs, divs(*ptextw, wc));
    else
        *pnumchs = 0;
}


void gsx_tblt(WORD tb_f, WORD x, WORD y, WORD tb_nc)
{
    if (tb_f == IBM)
    {
        if (tb_f != gl_font)
        {
            vst_height(gl_ws.ws_chmaxh, &gl_wptschar, &gl_hptschar, &gl_wchar, &gl_hchar);
            gl_font = tb_f;
        }
        y += gl_hptschar;
    }

    if (tb_f == SMALL)
    {
        if (tb_f != gl_font)
        {
            vst_height(gl_ws.ws_chminh, &gl_wsptschar, &gl_hsptschar, &gl_wschar, &gl_hschar);
            gl_font = tb_f;
        }
        y += gl_hsptschar;
    }

    contrl.code = 8;          /* TEXT */
    contrl.nptsin = 1;
    contrl.handle = gl_handle;
    ptsin[0] = x;
    ptsin[1] = y;
    contrl.nintin = tb_nc;
    gsx2();
}


/*
 *  Routine to draw a colored, patterned, rectangle
 */
void gr_rect(UWORD icolor, UWORD ipattern, GRECT *pt)
{
    WORD    fis;

    fis = FIS_PATTERN;
    if (ipattern == IP_HOLLOW)
        fis = FIS_HOLLOW;
    else if (ipattern == IP_SOLID)
        fis = FIS_SOLID;

    vsf_color(icolor);
    bb_fill(MD_REPLACE, fis, ipattern, pt->g_x, pt->g_y, pt->g_w, pt->g_h);
}


/*
 *  Routine to adjust the x,y starting point of a text string to
 *  account for its justification.  The number of characters in
 *  the string is also returned.
 */
WORD gr_just(WORD just, WORD font, char *ptext, WORD w, WORD h, GRECT *pt)
{
    WORD    numchs;

    /* figure out the width of the text string in pixels */
    gsx_tcalc(font, ptext, &pt->g_w, &pt->g_h, &numchs);

    h -= pt->g_h;
    if (h > 0)              /* check height */
        pt->g_y += (h + 1) / 2;

    w -= pt->g_w;
    if (w > 0)
    {
        /* do justification */
        if (just == TE_CNTR)
            pt->g_x += (w + 1) / 2;
        else if (just == TE_RIGHT)
            pt->g_x += w;
    }

    return numchs;
}


/*
 *  Routine to draw a string of graphic text
 */
void gr_gtext(WORD just, WORD font, char *ptext, GRECT *pt)
{
    WORD    numchs;
    GRECT   t;

    /* figure out where & how to put out the text */
    rc_copy(pt, &t);
    numchs = gr_just(just, font, ptext, t.g_w, t.g_h, &t);
    if (numchs > 0)
        gsx_tblt(font, t.g_x, t.g_y, numchs);
}


/*
 *  Routine to crack out the border color, text color, inside pattern,
 *  and inside color from a single color information word
 */
void gr_crack(UWORD color, WORD *pbc, WORD *ptc, WORD *pip, WORD *pic, WORD *pmd)
{
    /* 4 bit encoded border color */
    *pbc = (color >> 12) & 0x000f;

    /* 4 bit encoded text color */
    *ptc = (color >> 8) & 0x000f;

    /* 1 bit used to set text writing mode */
    *pmd = (color & 0x80) ? MD_REPLACE : MD_TRANS;

    /* 3 bit encoded pattern */
    *pip = (color >> 4) & 0x0007;

    /* 4 bit encoded inside color */
    *pic = color & 0x000f;
}

void gr_box(WORD x, WORD y, WORD w, WORD h, WORD th)
{
    GRECT   t, n;

    r_set(&t, x, y, w, h);
    if (th != 0)
    {
        if (th < 0)
            th--;
        gsx_moff();
        do
        {
            th += (th > 0) ? -1 : 1;
            rc_copy(&t, &n);
            gr_inside(&n, th);
            gsx_box(&n);
        } while (th != 0);
        gsx_mon();
    }
}
