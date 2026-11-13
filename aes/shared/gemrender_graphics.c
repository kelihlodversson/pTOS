/* Shared original AES drawing code.
 * Copyright 1999, Caldera Thin Clients, Inc.
 * Copyright 2002-2026 The EmuTOS development team. GPL version 2 or later. */
#include "aescontext.h"
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
void gsx_sclip(AES_RENDER_PARAM const GRECT *pt)
{
    gl_clip = *pt;

    if (gl_clip.g_w && gl_clip.g_h)
    {
        ptsin[0] = gl_clip.g_x;
        ptsin[1] = gl_clip.g_y;
        ptsin[2] = gl_clip.g_x + gl_clip.g_w - 1;
        ptsin[3] = gl_clip.g_y + gl_clip.g_h - 1;
        vs_clip(AES_RENDER_ARG  TRUE, ptsin);
    }
    else
        vs_clip(AES_RENDER_ARG  FALSE, ptsin);
}


/*
 *  Routine to get the current clip setting
 */
void gsx_gclip(AES_RENDER_PARAM GRECT *pt)
{
    *pt = gl_clip;
}


/*
 *  Routine to return TRUE iff the specified rectangle intersects the
 *  current clip rectangle ... or clipping is off (?)
 */
WORD gsx_chkclip(AES_RENDER_PARAM GRECT *pt)
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
void gsx_cline(AES_RENDER_PARAM OB_RENDER_LINE_COORD x1, OB_RENDER_LINE_COORD y1, OB_RENDER_LINE_COORD x2, OB_RENDER_LINE_COORD y2)
{
    gsx_moff();
    OB_RENDER_LINE(AES_RENDER_ARG x1,y1,x2,y2);
    gsx_mon();
}


/*
 *  Routine to set the text, writing mode, and color attributes
 */
void gsx_attr(AES_RENDER_PARAM UWORD text, UWORD mode, UWORD color)
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
        gsx2(AES_RENDER_ARG0);
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
        gsx2(AES_RENDER_ARG0);
    }
    intin[0] = tmp;
}


/*
 *  Routine to draw a box using the current attributes
 */
#ifndef OB_RENDER_BOX
static void gsx_box(AES_RENDER_PARAM LONG x, LONG y, LONG w, LONG h)
{
    GRECT rect;
    /* Native AES retains its WORD VDI points and public calling convention. */
    r_set(&rect,x,y,w,h);
    gsx_bxpts(AES_RENDER_ARG &rect);
    v_pline(AES_RENDER_ARG 5, ptsin);
}
#define OB_RENDER_BOX gsx_box
#endif

/*
 *  Routine to do a filled bit blit (a rectangle)
 */
void bb_fill(AES_RENDER_PARAM WORD mode, WORD fis, WORD patt, OB_RENDER_COORD hx, OB_RENDER_COORD hy, OB_RENDER_COORD hw, OB_RENDER_COORD hh)
{
    gsx_attr(AES_RENDER_ARG TRUE, mode, gl_tcolor);
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

    /* The binding controls when endpoints are narrowed to VDI WORDs. */
    OB_RENDER_FILL(hx,hy,hw,hh);
}


static UWORD ch_width(AES_RENDER_PARAM WORD fn)
{
    if (fn == IBM)
        return gl_wchar;

    if (fn == SMALL)
        return gl_wschar;

    return 0;
}


static UWORD ch_height(AES_RENDER_PARAM WORD fn)
{
    if (fn == IBM)
        return gl_hchar;

    if (fn == SMALL)
        return gl_hschar;

    return 0;
}


static void gsx_tcalc(AES_RENDER_PARAM WORD font, char *ptext, WORD *ptextw, WORD *ptexth, WORD *pnumchs)
{
    WORD    wc, hc;

    wc = ch_width(AES_RENDER_ARG font);
    hc = ch_height(AES_RENDER_ARG font);

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


void gsx_tblt(AES_RENDER_PARAM WORD tb_f, WORD x, WORD y, WORD tb_nc)
{
    if (tb_f == IBM)
    {
        if (tb_f != gl_font)
        {
            vst_height(AES_RENDER_ARG gl_ws.ws_chmaxh, &gl_wptschar, &gl_hptschar, &gl_wchar, &gl_hchar);
            gl_font = tb_f;
        }
        y += gl_hptschar;
    }

    if (tb_f == SMALL)
    {
        if (tb_f != gl_font)
        {
            vst_height(AES_RENDER_ARG gl_ws.ws_chminh, &gl_wsptschar, &gl_hsptschar, &gl_wschar, &gl_hschar);
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
    gsx2(AES_RENDER_ARG0);
}


/*
 *  Routine to draw a colored, patterned, rectangle
 */
void gr_rect(AES_RENDER_PARAM UWORD icolor, UWORD ipattern, GRECT *pt)
{
    WORD    fis;

    fis = FIS_PATTERN;
    if (ipattern == IP_HOLLOW)
        fis = FIS_HOLLOW;
    else if (ipattern == IP_SOLID)
        fis = FIS_SOLID;

    vsf_color(icolor);
    bb_fill(AES_RENDER_ARG MD_REPLACE, fis, ipattern, pt->g_x, pt->g_y, pt->g_w, pt->g_h);
}


/*
 *  Routine to adjust the x,y starting point of a text string to
 *  account for its justification.  The number of characters in
 *  the string is also returned.
 */
WORD gr_just(AES_RENDER_PARAM WORD just, WORD font, char *ptext, WORD w, WORD h, GRECT *pt)
{
    WORD    numchs;

    /* figure out the width of the text string in pixels */
    gsx_tcalc(AES_RENDER_ARG font, ptext, &pt->g_w, &pt->g_h, &numchs);

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
void gr_gtext(AES_RENDER_PARAM WORD just, WORD font, char *ptext, GRECT *pt)
{
    WORD    numchs;
    GRECT   t;

    /* figure out where & how to put out the text */
    rc_copy(pt, &t);
    numchs = gr_just(AES_RENDER_ARG just, font, ptext, t.g_w, t.g_h, &t);
    if (numchs > 0)
        gsx_tblt(AES_RENDER_ARG font, t.g_x, t.g_y, numchs);
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

void gr_box(AES_RENDER_PARAM OB_RENDER_COORD x, OB_RENDER_COORD y, OB_RENDER_COORD w, OB_RENDER_COORD h, WORD th)
{
    if (th != 0)
    {
        if (th < 0)
            th--;
        gsx_moff();
        do
        {
            th += (th > 0) ? -1 : 1;
            OB_RENDER_BOX(AES_RENDER_ARG (LONG)x+th,(LONG)y+th,(LONG)w-2*th,(LONG)h-2*th);
        } while (th != 0);
        gsx_mon();
    }
}
