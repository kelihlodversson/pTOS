/*
 * vdi_text.c - uses text_blt to move data from a font table to screen
 *
 * Copyright 1982 by Digital Research Inc.  All rights reserved.
 * Copyright 1999 by Caldera, Inc. and Authors:
 * Copyright 2002-2025 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "emutos.h"
#include "asm.h"
#include "endian.h"
#include "intmath.h"
#include "string.h"
#include "aesext.h"
#include "vdi_defs.h"
#include "vdi_backend.h"
#include "vdistub.h"
#include "lineavars.h"
#include "biosext.h"
#include "has.h"
#include "vdi_textblit.h"
#include "vdi_gdos.h"


extern WORD deftxbuf[];         /* Default text scratch buffer */
extern const WORD scrtsiz;      /* Default offset to large text buffer */


/* Prototypes for this module */
static void make_header(Vwk * vwk);
static UWORD clc_dda(Vwk * vwk, UWORD act, UWORD req);
static UWORD act_siz(Vwk * vwk, UWORD top);

/*
 * calculates height of text string
 */
static WORD calc_height(Vwk *vwk)
{
    const Fonthead *fnt_ptr = vwk->cur_font;
    WORD height;

    height = fnt_ptr->top + fnt_ptr->bottom + 1;    /* handles scaled fonts */

    if (vwk->style & F_OUTLINE)
        height += OUTLINE_THICKNESS * 2;    /* outlining adds 1 pixel all around */

    return height;
}

/*
 * whether a font's bold characters stay in their cells (are no wider than
 * plain ones): those of a monospaced font, and of every size and page of a
 * fixed-pitch scalable face
 */
static BOOL bold_in_cell(const Fonthead *fnt_ptr)
{
#if CONF_WITH_VDI_SCALABLE_FONTS
    if ((fnt_ptr->flags & (F_SFONT | F_SFONT_FIXED)) == (F_SFONT | F_SFONT_FIXED))
        return TRUE;
#endif
    return (fnt_ptr->flags & F_MONOSPACE) != 0;
}

/*
 * calculates width of text string, in a font of the workstation's: what
 * its characters add up to, unscaled
 */
static WORD raw_width(const Fonthead *fnt_ptr, WORD cnt, const WORD *str)
{
    WORD table_start = fnt_ptr->first_ade;
    WORD i, chr, width;

    if (fnt_ptr->flags & F_MONOSPACE)
    {
        width = cnt * (fnt_ptr->off_table[1]-fnt_ptr->off_table[0]);
    }
    else
    {
        for (i = 0, width = 0; i < cnt; i++) {
            chr = *str++ - table_start;
            width += fnt_ptr->off_table[chr + 1] - fnt_ptr->off_table[chr];
        }
    }

    return width;
}

/* the width of 'cnt' characters 'width' wide in the font, as scaled and styled */
static WORD styled_width(Vwk *vwk, const Fonthead *fnt_ptr, WORD cnt, WORD width)
{
    if (vwk->scaled) {
        if (vwk->dda_inc == 0xFFFF)
            width *= 2;
        else
            width = act_siz(vwk, width);
    }

    if ((vwk->style & F_THICKEN) && !bold_in_cell(fnt_ptr))
        width += cnt * fnt_ptr->thicken;

    if (vwk->style & F_SKEW)
        width += fnt_ptr->left_offset + fnt_ptr->right_offset;

    if (vwk->style & F_OUTLINE)
        width += cnt * OUTLINE_THICKNESS * 2;   /* outlining adds 1 pixel all around */

    return width;
}

static WORD calc_width(Vwk *vwk, const Fonthead *fnt_ptr, WORD cnt, const WORD *str)
{
    return styled_width(vwk, fnt_ptr, cnt, raw_width(fnt_ptr, cnt, str));
}

#if CONF_WITH_VDI_TEXT_SPEEDUP
/*
 * returns TRUE if we can use a direct screen blit
 *
 * the following must all be true:
 *  there are no effects
 *  there is no rotation
 *  the output is left-aligned
 *  the output is not justified
 *  the characters are byte-aligned
 *  the font is monospace with a cell width of 8
 *  the font contains glyphs for all 256 characters
 *  the entire text string will not be clipped
 */
static BOOL ok_for_direct_blit(Vwk *vwk, const Fonthead *fnt_ptr, WORD width, JUSTINFO *justified)
{
    WORD xmin, xmax, ymin, ymax;

#if CONF_WITH_VDI_BACKEND_TRUECOLOR
    /* The direct path below writes planar screen bytes, not packed pixels. */
    if (vdi_truecolor_screen())
        return FALSE;
#endif

    if (vwk->style | vwk->chup | vwk->h_align)
        return FALSE;

    if (justified)
        return FALSE;

#if CONF_WITH_VDI_16BIT
    if (TRUECOLOR_MODE)     /* always byte-aligned */
    {
    }
    else
#endif
    {
        if (linea_vars.DESTX & 0x0007)
            return FALSE;
    }

    /* every character 8 pixels wide (MONO alone says only how bold is) */
    if (!(fnt_ptr->flags & F_MONOSPACE) || (fnt_ptr->max_cell_width != 8))
        return FALSE;

    if ((fnt_ptr->first_ade != 0) || (fnt_ptr->last_ade != 255))
        return FALSE;

    if (vwk->clip)
    {
        xmin = vwk->xmn_clip;
        ymin = vwk->ymn_clip;
        xmax = vwk->xmx_clip;
        ymax = vwk->ymx_clip;
    }
    else
    {
        xmin = 0;       /* must not exceed screen limits */
        ymin = 0;
        xmax = xres;
        ymax = yres;
    }

    /* check that string falls entirely within clip area */
    if ((linea_vars.DESTX < xmin) || (linea_vars.DESTX+width > xmax))
        return FALSE;
    if ((linea_vars.DESTY < ymin) || (linea_vars.DESTY+linea_vars.DELY > ymax))
        return FALSE;

    return TRUE;
}
#endif

#if CONF_WITH_BLITTER
/*
 * check if the blitter can output the characters of the string the quick
 * way, see blitter_text_string(): plain text, or bold text with
 * CONF_WITH_VDI_TEXT_EFFECTS_SPEEDUP (light and underline are all right,
 * and so is justification), on a bitplane screen
 */
static BOOL ok_for_blitter_string(Vwk *vwk)
{
    if (!blitter_is_enabled)
        return FALSE;

    if (vdi_screen_is_truecolor() || TRUECOLOR_MODE)
        return FALSE;

#if CONF_WITH_VDI_TEXT_EFFECTS_SPEEDUP
    if (vwk->style & (F_SKEW | F_OUTLINE))
#else
    if (vwk->style & (F_THICKEN | F_SKEW | F_OUTLINE))
#endif
        return FALSE;

    if (vwk->chup || vwk->scaled)
        return FALSE;

    if (vwk->cur_font->flags & F_HORZ_OFF)
        return FALSE;

    return TRUE;
}
#endif

/*
 * set up text_blt() to output a character: where its image is in the font
 * form, and how big; returns its index in the font (which is not that of
 * the character if it is out of range for the font, a '?' being output)
 */
static inline WORD load_char(const Fonthead *fnt_ptr, WORD ch)
{
    WORD temp = ch;

    /* If the character is out of range for this font make it a ? */
    if ((temp < fnt_ptr->first_ade) || (temp > fnt_ptr->last_ade))
        temp = '?';
    temp -= fnt_ptr->first_ade;

    linea_vars.SOURCEX = fnt_ptr->off_table[temp];
    linea_vars.DELX = fnt_ptr->off_table[temp + 1] - linea_vars.SOURCEX;

    linea_vars.SOURCEY = 0;
    linea_vars.DELY = fnt_ptr->form_height;

    return temp;
}

/*
 * the furthest to the right the destination can get with a string of 'count'
 * characters, whatever they are: a character is at most max_cell_width wide
 * and gets adv_extra from its style, and each one that follows is moved on by
 * the spacing of the justification
 */
static LONG run_reach(const Fonthead *fnt_ptr, WORD count, WORD adv_extra,
                      const JUSTINFO *justified)
{
    LONG step = (LONG)fnt_ptr->max_cell_width + adv_extra;

    if (justified)
        step += (LONG)justified->charx + justified->rmcharx
              + justified->wordx + justified->rmwordx;
    if (step > 0x7fffL)
        return 0x7fffffffL;
    return (LONG)linea_vars.DESTX + (LONG)count * (WORD)step;
}

/*
 * move the destination for the justification after a character: the spacing
 * between characters, and between words (after a blank)
 */
static inline void justify_step(JUSTINFO *justified, WORD ch)
{
    linea_vars.DESTX += justified->charx;
    linea_vars.DESTY += justified->chary;
    if (justified->rmchar) {
        linea_vars.DESTX += justified->rmcharx;
        linea_vars.DESTY += justified->rmchary;
        justified->rmchar--;
    }
    if (ch == ' ') {
        linea_vars.DESTX += justified->wordx;
        linea_vars.DESTY += justified->wordy;
        if (justified->rmword) {
            linea_vars.DESTX += justified->rmwordx;
            linea_vars.DESTY += justified->rmwordy;
            justified->rmword--;
        }
    }
}

#if CONF_WITH_VDI_SCALABLE_FONTS
/*
 * The ink of a scalable font's glyphs that leaves their cells (an italic f, the
 * tail of a j, a slanted letter) is not in the cells, which are as wide as the
 * advances: it is in cells of its own (vdi_sfont_overhang()), two to a
 * character, the part left of its cell and the part right of it.  Replace
 * mode, in which a cell's background is drawn too, would have a character
 * cover the ink of the one before it, so that ink is output after all the
 * characters, transparently, in a pass of its own (see output_text()): the
 * string is gone through again, from where it started and with the same
 * spacing, and these cells are output where the characters' cells are, the
 * left part before it and the right part after it.
 */
static BOOL over_pass;                  /* output_run(): only the ink outside the cells */
static BOOL overhang_seen;              /* a run was in a font with such ink */

/* move the destination 'd' pixels along the baseline */
static void move_along(WORD d)
{
    switch(linea_vars.CHUP) {
    default:
        linea_vars.DESTX += d;
        break;
    case 900:
        linea_vars.DESTY -= d;
        break;
    case 1800:
        linea_vars.DESTX -= d;
        break;
    case 2700:
        linea_vars.DESTY += d;
        break;
    }
}

/*
 * In the overhang pass, instead of a character's cell (set up by load_char(),
 * and in 'temp' of the font): the cells of ink that leave it (none if the
 * font has no 'off'), and the move on to where the next character goes, which
 * is what text_blt() makes of the cell's width and the style
 */
static void draw_overhang(WORD temp, const UWORD *off, const UBYTE *form, UWORD form_width)
{
    WORD x = linea_vars.DESTX, y = linea_vars.DESTY;
    WORD delx = linea_vars.DELX, adv = delx;
    WORD mono = linea_vars.MONO;
    WORD left, right;
    const UBYTE *fbase = linea_vars.FBASE;
    WORD fwidth = linea_vars.FWIDTH;

    if ((linea_vars.STYLE & F_THICKEN) && !mono)
        adv += linea_vars.WEIGHT;
    if (linea_vars.STYLE & F_OUTLINE)
        adv += OUTLINE_THICKNESS * 2;

    left = right = 0;
    if (off)
    {
        left = off[2 * temp + 1] - off[2 * temp];
        right = off[2 * temp + 2] - off[2 * temp + 1];
    }
    /*
     * XOR toggles a pixel each time it is drawn: the cell and a piece, each
     * thickened, outlined or slanted by itself, overlap, and would toggle the
     * same pixels twice.  That ink is left as the cells have it.
     */
    if (linea_vars.WRT_MODE == WM_XOR && (linea_vars.STYLE & (F_THICKEN | F_OUTLINE | F_SKEW)))
        left = right = 0;
    if (left > 0 || right > 0)
    {
        linea_vars.FBASE = form;
        linea_vars.FWIDTH = form_width;
        /*
         * A piece is not a cell of a fixed-pitch font, whose bold stays in
         * it (the last columns of the source are not drawn, as thickening
         * puts them in the cell's): it is thickened as the cell of any
         * other font is, and so is one column wide.  Each piece is drawn
         * from the top of the form, whatever clipping the one before
         * moved the source by (do_clip() does that to SOURCEX and SOURCEY).
         */
        linea_vars.MONO = 0;
        if (left > 0)
        {
            linea_vars.SOURCEX = off[2 * temp];
            linea_vars.SOURCEY = 0;
            linea_vars.DELX = left;
            move_along(-left);
            text_blt();
            linea_vars.DESTX = x;
            linea_vars.DESTY = y;
        }
        if (right > 0)
        {
            /* where the cell ends, which its thickening or outline does not move */
            linea_vars.SOURCEX = off[2 * temp + 1];
            linea_vars.SOURCEY = 0;
            linea_vars.DELX = right;
            move_along(delx);
            text_blt();
            linea_vars.DESTX = x;
            linea_vars.DESTY = y;
        }
        linea_vars.MONO = mono;
        linea_vars.FBASE = fbase;
        linea_vars.FWIDTH = fwidth;
        linea_vars.DELX = delx;
    }
    move_along(adv);
}
#endif

/*
 * output specified text string, in one of the workstation's fonts
 *
 * 'width' is the pre-calculated width of the text on the screen;
 * if negative, it has not yet been calculated.  'align' is the width
 * the alignment is made for, unless ALIGN_UNSET: the whole string's (which
 * a negative track offset can make negative), of
 * which this is the first run of characters in one font.  With 'cont',
 * this run follows one already output, where that one left off: it is
 * not aligned.  With 'more', another run follows this one, from where
 * it leaves off.  The underline of runs is drawn once, by the last, from
 * where the first started (kept in 'ul_start').
 */
#define ALIGN_UNSET ((WORD)0x8000)     /* output_run(): align for the run's own width */

static void output_run(Vwk *vwk, const Fonthead *fnt_ptr, WORD count, WORD *str,
                       WORD width, WORD align, JUSTINFO *justified, BOOL cont, BOOL more,
                       Point *ul_start)
{
    WORD i, j;
    WORD startx, starty;
    WORD xfact, yfact;
    WORD tx1, tx2, ty1, ty2;
    WORD delh, delv;
    WORD d1, d2;
    WORD outline, underline;

    WORD temp;
    BOOL done, skip_clipped, line_out, run_mode;
    WORD adv_extra, clip_extra, xmn, xmx, xlim;
    Point * point;
#if CONF_WITH_BLITTER
    WORD (*blitter_string)(const Fonthead *, WORD, const WORD *, JUSTINFO *);
#endif
#if CONF_WITH_VDI_SCALABLE_FONTS
    const UWORD *over_off = NULL;       /* the cells of ink outside the cells */
    const UBYTE *over_form = NULL;
    UWORD over_width = 0;
#endif

    if (count <= 0)     /* quick out for unlikely occurrence */
        return;

    if (width < 0)      /* called from vdi_v_gtext() */
        width = calc_width(vwk, fnt_ptr, count, str);
    if (align == ALIGN_UNSET)
        align = width;

    /* some data copying for the assembler part */
    linea_vars.DDAINC = vwk->dda_inc;
    linea_vars.SCALDIR = vwk->t_sclsts;
    linea_vars.SCALE = vwk->scaled;
    linea_vars.MONO = bold_in_cell(fnt_ptr);   /* bold stays in the cell */
    linea_vars.WRT_MODE = vwk->wrt_mode;

    linea_vars.CLIP = vwk->clip;
#if CONF_WITH_VDI_SCALABLE_FONTS
    /*
     * the ink outside the cells can be off the screen where the cells are
     * not (a j at the left edge): the bounds of the workstation are the
     * screen's if it does not clip, so this clips it to the screen
     */
    if (over_pass)
        linea_vars.CLIP = TRUE;
#endif
    linea_vars.XMINCL = vwk->xmn_clip;
    linea_vars.YMINCL = vwk->ymn_clip;
    linea_vars.XMAXCL = vwk->xmx_clip;
    linea_vars.YMAXCL = vwk->ymx_clip;
    linea_vars.STYLE = vwk->style;
    linea_vars.CHUP = vwk->chup;
    linea_vars.SCRPT2 = vwk->scrpt2;
    linea_vars.SCRTCHP = vwk->scrtchp;
#if CONF_WITH_VDI_SCALABLE_FONTS
    /* a scalable font's glyphs can be bigger than the default buffer holds */
    if (fnt_ptr->flags & F_SFONT)
    {
        vdi_sfont_scratch(fnt_ptr, &linea_vars.SCRTCHP, &linea_vars.SCRPT2);
        if (!linea_vars.SCALE && vdi_sfont_overhang(fnt_ptr, &over_off, &over_form, &over_width))
            overhang_seen = TRUE;
    }
#endif

    if (vwk->style & F_THICKEN)
        linea_vars.WEIGHT = fnt_ptr->thicken;

    if (vwk->style & F_LIGHT)
        linea_vars.LITEMASK = fnt_ptr->lighten;

    if (vwk->style & F_SKEW) {
        d1 = fnt_ptr->left_offset;  /* used in vertical alignment calcs */
        d2 = fnt_ptr->right_offset;
        linea_vars.SKEWMASK = fnt_ptr->skew;
    } else {
        d1 = 0;
        d2 = 0;
    }
    linea_vars.LOFF = d1;
    linea_vars.ROFF = d2;

    linea_vars.FBASE = fnt_ptr->dat_table;
    linea_vars.FWIDTH = fnt_ptr->form_width;

    /*
     * in Atari TOS, outlined text starts 1 pixel earlier than
     * non-outlined, so we set 'outline' to handle that.
     * this also affects horizontal alignment calculations.
     */
    outline = (vwk->style & F_OUTLINE) ? OUTLINE_THICKNESS : 0;

    switch(vwk->h_align) {
    default:                /* normally case 0: left justified */
        delh = 0;
        break;
    case 1:
        delh = align / 2 - outline;
        break;
    case 2:
        delh = align - (outline * 2);
        break;
    }

    switch(vwk->v_align) {
    default:                /* normally case 0: baseline */
        delv = fnt_ptr->top;
        delh += d1;
        break;
    case 1:
        delv = fnt_ptr->top - fnt_ptr->half;
        delh += divs(muls(fnt_ptr->half, d2), fnt_ptr->top);
        break;
    case 2:
        delv = fnt_ptr->top - fnt_ptr->ascent;
        delh += divs(muls(fnt_ptr->ascent, d2), fnt_ptr->top);
        break;
    case 3:
        delv = fnt_ptr->top + fnt_ptr->bottom;
        break;
    case 4:
        delv = fnt_ptr->top + fnt_ptr->descent;
        delh += divs(muls(fnt_ptr->descent, d1), fnt_ptr->bottom);
        break;
    case 5:
        delv = 0;
        delh += d1 + d2;
        break;
    }

    /*
     * like Atari TOS, we try to ensure that any underline will fall within
     * the character cell.  if we have sufficient room (e.g. in an 8x16 font),
     * we drop the underline to the bottom line; otherwise it sits on the
     * descent line.  in the latter case, if the font has been doubled, the
     * underline will be thick, and we need to raise it.
     */
    if (fnt_ptr->bottom > fnt_ptr->ul_size)             /* normal for 8x16 font */
        underline = 1;
    else if (vwk->scaled && (vwk->dda_inc == 0xffff))   /* doubling size exactly? */
        underline = -1;
    else
        underline = 0;
    underline += fnt_ptr->ul_size;

    point = (Point*)PTSIN;
    switch(vwk->chup) {
    default:                /* normally case 0: no rotation */
        if (!cont) {
            linea_vars.DESTX = point->x - delh - outline;
            linea_vars.DESTY = point->y - delv - outline;
        }
        startx = linea_vars.DESTX;
        starty = linea_vars.DESTY + fnt_ptr->top + underline;
        xfact = 0;
        yfact = 1;
        break;
    case 900:
        if (!cont) {
            linea_vars.DESTX = point->x - delv - outline;
            linea_vars.DESTY = point->y + delh + outline + 1;
        }
        startx = linea_vars.DESTX + fnt_ptr->top + underline;
        starty = linea_vars.DESTY;
        xfact = 1;
        yfact = 0;
        break;
    case 1800:
        if (!cont) {
            linea_vars.DESTX = point->x + delh + outline + 1;
            linea_vars.DESTY = point->y - ((fnt_ptr->top + fnt_ptr->bottom) - delv) - outline;
        }
        startx = linea_vars.DESTX;
        starty = linea_vars.DESTY + fnt_ptr->bottom - underline;
        xfact = 0;
        yfact = -1;
        break;
    case 2700:
        if (!cont) {
            linea_vars.DESTX = point->x - ((fnt_ptr->top + fnt_ptr->bottom) - delv) - outline;
            linea_vars.DESTY = point->y - delh - outline;
        }
        startx = linea_vars.DESTX + fnt_ptr->bottom - underline;
        starty = linea_vars.DESTY;
        xfact = -1;
        yfact = 0;
        break;
    }

    linea_vars.TEXTFG = vwk->text_color;
    linea_vars.DELY = fnt_ptr->form_height;

#if CONF_WITH_VDI_TEXT_SPEEDUP
    /*
     * call special direct screen blit routine if applicable
     */
#if CONF_WITH_VDI_SCALABLE_FONTS
    if (!over_pass)
#endif
    if (ok_for_direct_blit(vwk, fnt_ptr, width, justified))
    {
        direct_screen_blit(count, str);
        linea_vars.DESTX += width;      /* where a run that follows starts */
        return;
    }
#endif

    if (!cont)
        linea_vars.XDDA = 32767;   /* init the horizontal dda (a run goes on with it) */

#if CONF_WITH_BLITTER
    /* the blitter's way of outputting several characters at once, if any */
    blitter_string = NULL;
#if CONF_WITH_VDI_SCALABLE_FONTS
    if (!over_pass)
#endif
    if (ok_for_blitter_string(vwk))
#if CONF_WITH_VDI_TEXT_EFFECTS_SPEEDUP
        blitter_string = (vwk->style & F_THICKEN) ? blitter_bold_string : blitter_text_string;
#else
        blitter_string = blitter_text_string;
#endif
#endif

    /*
     * Characters that are wholly outside the clip window are not handed to
     * text_blt() (nor to the blitter's string path, whose set-up would be
     * made for nothing): that costs a good deal to find out there, and of
     * the characters of a line of text, those that stick out of a window or
     * are on a line that is not in it can be most of them.
     *
     * This is only done for text that is neither rotated nor scaled, whose
     * lines are not moved up or down by justification, with the test
     * check_clip() makes in vdi_textblit.c on the width and height that
     * text_blt() gives it, and the move of the destination it makes when it
     * finds there is nothing to draw.  Not asking is always all right, as
     * text_blt() then makes the same test itself: so no character is
     * skipped that is not outside, but some outside ones may go to it.
     *
     * clip_extra and adv_extra are what the style adds to the width of a
     * character for the test and for the move; the left edge is tested
     * against xlim, which has clip_extra taken off it.
     */
    adv_extra = clip_extra = xmn = xmx = xlim = 0;
    line_out = run_mode = FALSE;
    skip_clipped = vwk->clip && !vwk->chup && !vwk->scaled;
#if CONF_WITH_VDI_SCALABLE_FONTS
    if (over_pass)
        skip_clipped = FALSE;           /* ink of a character outside the window can be in it */
#endif
    if (skip_clipped && justified
     && (justified->chary || justified->rmchary
      || justified->wordy || justified->rmwordy))
        skip_clipped = FALSE;
    if (skip_clipped)
    {
        WORD y = linea_vars.DESTY;
        WORD clip_dely = fnt_ptr->form_height;

        adv_extra = 0;
        if ((vwk->style & F_THICKEN) && !bold_in_cell(fnt_ptr))
            adv_extra += fnt_ptr->thicken;
        clip_extra = adv_extra;
        if (vwk->style & F_SKEW)
            clip_extra += linea_vars.LOFF + linea_vars.ROFF;
        if (vwk->style & F_OUTLINE)
        {
            adv_extra += OUTLINE_THICKNESS * 2;
            clip_extra += OUTLINE_THICKNESS * 2;
            clip_dely += OUTLINE_THICKNESS * 2;
        }
        xmn = vwk->xmn_clip;
        xmx = vwk->xmx_clip;
        xlim = xmn - clip_extra;

        /* the line is the same for all the characters: in or out? */
        line_out = (y < vwk->ymn_clip && y + clip_dely <= vwk->ymn_clip)
                || y > vwk->ymx_clip;

        /*
         * a string that lies inside the window needs no test at all (width
         * is what the characters add up to, if the font has no offsets
         * that move them)
         */
        if (!line_out && !justified && !(fnt_ptr->flags & F_HORZ_OFF)
         && linea_vars.DESTX >= xmn
         && linea_vars.DESTX + width + clip_extra <= xmx
         && y >= vwk->ymn_clip)
            skip_clipped = FALSE;
        else if (!(fnt_ptr->flags & F_HORZ_OFF)
              && (!justified
               || (justified->charx >= 0 && justified->rmcharx >= 0
                && justified->wordx >= 0 && justified->rmwordx >= 0))
              && run_reach(fnt_ptr, count, adv_extra, justified) < 32767L)
        {
            /*
             * if the characters go from left to right, one after the other (no
             * justification that can move them back, no offsets of their own,
             * and so far to the right that the position of a character
             * cannot wrap round: DESTX is a signed 16 bit number),
             * those that are wholly left of the window are a run at the start
             * and those wholly right of it are a run at the end: the first
             * character that is right of the window ends the string, as the
             * underline is cut by the window all the same wherever it ends, and
             * a line that is not in the window needs no loop at all (nor, then,
             * the underline, if its rows are not in it either)
             */
            run_mode = TRUE;
            if (line_out)
            {
                WORD ul_top = starty;

                if (!(vwk->style & F_UNDER)
                 || ul_top + fnt_ptr->ul_size <= vwk->ymn_clip
                 || ul_top > vwk->ymx_clip)
                    return;
            }
        }
    }

    j = 0;
    if (run_mode)
    {
        /* the characters wholly left of the window (or all, if the line is out) */
        while (j < count)
        {
            WORD x = linea_vars.DESTX;

            load_char(fnt_ptr, str[j]);
            if (!line_out && !(x < xmn && x + linea_vars.DELX <= xlim))
                break;
            linea_vars.DESTX = x + linea_vars.DELX + adv_extra;
            if (justified)
                justify_step(justified, str[j]);
            j++;
        }
    }

    for (; j < count; j++) {

        temp = load_char(fnt_ptr, str[j]);

        done = FALSE;
        if (skip_clipped)
        {
            if (run_mode)
            {
                if (linea_vars.DESTX > xmx)
                {
                    /*
                     * the rest is right of the window: not drawn, but if
                     * another run follows (which kerning or a track
                     * offset can bring back left), the destination moved
                     * over it all the same, as that one starts there
                     */
                    for (; more && j < count; j++)
                    {
                        load_char(fnt_ptr, str[j]);
                        linea_vars.DESTX += linea_vars.DELX + adv_extra;
                        if (justified)
                            justify_step(justified, str[j]);
                    }
                    break;
                }
            }
            else
            {
                WORD x = linea_vars.DESTX;

                done = line_out || x > xmx
                    || (x < xmn && x + linea_vars.DELX <= xlim);
                if (done)
                    linea_vars.DESTX += linea_vars.DELX + adv_extra;
            }
        }

        if (!done)
        {
#if CONF_WITH_BLITTER
            /* the characters that are plain or bold and not clipped, all at once */
            if (blitter_string)
            {
                WORD n = blitter_string(fnt_ptr, count - j, str + j, justified);

                if (n > 0)
                {
                    j += n;
                    if (j >= count)
                        break;
                    temp = load_char(fnt_ptr, str[j]);
                }
            }
#endif
#if CONF_WITH_VDI_SCALABLE_FONTS
            if (over_pass)
                draw_overhang(temp, over_off, over_form, over_width);
            else
#endif
            text_blt();
        }

        if (justified)
            justify_step(justified, str[j]);
        /* end if justified */
        if (fnt_ptr->flags & F_HORZ_OFF)
            linea_vars.DESTX += fnt_ptr->hor_table[temp];

    }                   /* for j */

    if (ul_start)
    {
        if (!cont)
        {
            ul_start->x = startx;
            ul_start->y = starty;
        }
        else
        {
            startx = ul_start->x;
            starty = ul_start->y;
        }
    }

#if CONF_WITH_VDI_SCALABLE_FONTS
    if (over_pass)
        return;                         /* the underline is drawn with the characters */
#endif
    if ((vwk->style & F_UNDER) && !more) {
        Line * line = (Line*)PTSIN;
        line->x1 = startx;
        line->y1 = starty;

        if (vwk->chup % 1800 == 0) {
            line->x2 = linea_vars.DESTX;
            line->y2 = line->y1;
        } else {
            line->x2 = line->x1;
            line->y2 = linea_vars.DESTY;
        }

        if (vwk->style & F_LIGHT)
            linea_vars.LN_MASK = fnt_ptr->lighten;
        else
            linea_vars.LN_MASK = 0xffff;

        count = fnt_ptr->ul_size;
        for (i = 0; i < count; i++) {
            if (vwk->clip) {
                tx1 = line->x1;
                tx2 = line->x2;
                ty1 = line->y1;
                ty2 = line->y2;

                if (clip_line(vwk, line))
                    abline(line, vwk->wrt_mode, vwk->text_color);

                line->x1 = tx1;
                line->x2 = tx2;
                line->y1 = ty1;
                line->y2 = ty2;
            } else
                abline(line, vwk->wrt_mode, vwk->text_color);

            line->x1 += xfact;
            line->x2 += xfact;
            line->y1 += yfact;
            line->y2 += yfact;

            rorw1(linea_vars.LN_MASK);
        }
    }
}

#if CONF_WITH_VDI_SCALABLE_FONTS
/*
 * Text in a character map other than the Atari character set
 * (vst_map_mode()): BICS, Unicode's first plane (UCS-2) or all of it
 * (UTF-16, which pairs surrogates).  A bitmap font only has the Atari
 * characters, so the text is turned into those, '?' where there is none.
 * A scalable font has a size for each Unicode page of 256 characters
 * (vdi_sfont_page()): the text is cut into runs of characters in one page,
 * each output where the one before left off, in its page's size.  ASCII,
 * the most of most text, is the same in the Atari character set, so it
 * stays in the size itself and its characters as they are.
 *
 * Kerning (vst_kern()) is applied in the same way, in any character map:
 * a run also ends before a character that is kerned against the one before
 * it, and the next run starts that much further on.  The kerning is in
 * fractions of a pixel (16.16), which are carried on, so that a track
 * offset of less than a pixel per character adds up as it should.  The
 * underline is drawn once, under all the runs.
 */

/* a distance along the baseline as a move on the screen, for the rotation */
static void along_baseline(Vwk *vwk, WORD d, WORD *dx, WORD *dy)
{
    switch(vwk->chup) {
    default:
        *dx = d;
        *dy = 0;
        break;
    case 900:
        *dx = 0;
        *dy = -d;
        break;
    case 1800:
        *dx = -d;
        *dy = 0;
        break;
    case 2700:
        *dx = 0;
        *dy = d;
        break;
    }
}

/* how far a move on the screen goes along the baseline */
static WORD baseline_part(Vwk *vwk, WORD dx, WORD dy)
{
    switch(vwk->chup) {
    default:
        return dx;
    case 900:
        return -dy;
    case 1800:
        return -dx;
    case 2700:
        return dy;
    }
}

/* whether kerning applies to text in the workstation's current font */
static BOOL kerning_applies(Vwk *vwk)
{
    const Fonthead *font = vwk->cur_font;
    WORD index;

    if (!(font->flags & F_SFONT))
        return FALSE;
    if (vwk->track_offset)
        return TRUE;
    index = vwk->pair_kern ? vdi_sfont_find(font->font_id) : -1;

    return index >= 0 && vdi_sfonts[index].kern_pairs;
}

#define RUN_MAX     32              /* characters copied for a run */
#define RUN_SAME    (-1)            /* the run is in the current font itself */

typedef struct
{
    const WORD *str;                /* the string */
    WORD count;                     /* how long it is */
    WORD pos;                       /* how far it has been read */
    const Fonthead *font;           /* the run's font */
    WORD *chars;                    /* its characters, as the font has them */
    WORD n;                         /* how many */
    WORD kern;                      /* how far on it starts, in pixels: kerning */
    BOOL kerning;                   /* whether kerning applies */
    LONG prev_cp;                   /* the character before the run, or -1 */
    LONG kern_sum;                  /* the kerning so far, in 16.16 pixels, */
    LONG kern_done;                 /* and what of it has been applied, in pixels */
    LONG kept[RUN_MAX];             /* the pairs' kerning past where the last run was cut, */
    WORD kept_n;                    /* for how many characters, */
    WORD kept_pos;                  /* from where in the string */
    WORD buf[RUN_MAX];
} TEXT_RUN;

/* the next character of a string, as a Unicode code point; -1 if none */
static LONG next_char(WORD map, const WORD *str, WORD count, WORD *pos)
{
    UWORD c = str[(*pos)++], c2;

    switch (map) {
    case MAP_BICS:
        return vdi_bics_to_unicode(c);
    case MAP_UTF16:
        if (c >= 0xdc00 && c < 0xe000)
            return -1;                  /* half a pair */
        if (c >= 0xd800 && c < 0xdc00)
        {
            if (*pos >= count)
                return -1;
            c2 = str[*pos];
            if (c2 < 0xdc00 || c2 >= 0xe000)
                return -1;
            (*pos)++;
            return 0x10000L + (((LONG)c - 0xd800) << 10) + (c2 - 0xdc00);
        }
        return c;
    default:                            /* MAP_UCS2 */
        return c;
    }
}

/*
 * where a code point is in the workstation's current font: in the font
 * itself (RUN_SAME), as the character in *ch, or in a Unicode page of it,
 * whose number is returned, as *ch in it
 */
static LONG char_place(Vwk *vwk, LONG cp, WORD *ch)
{
    WORD atari;

    if (cp >= 0x20 && cp < 0x7f)
    {
        *ch = (WORD)cp;
        return RUN_SAME;
    }
    if (vwk->cur_font->flags & F_SFONT)
    {
        if (cp < 0)
            cp = 0xfffdL;               /* a lone surrogate: the replacement character */
        *ch = (WORD)(cp & 0xff);
        return cp >> 8;
    }
    atari = vdi_unicode_to_atari(cp);
    *ch = atari >= 0 ? atari : '?';
    return RUN_SAME;
}

/*
 * the font a page is in (a page of a scalable font, or failing that the
 * font itself, the characters then turned into the Atari ones)
 */
static const Fonthead *place_font(Vwk *vwk, LONG page, WORD *chars, WORD n)
{
    const Fonthead *font;
    WORD i, atari;

    if (page == RUN_SAME)
        return vwk->cur_font;
    font = vdi_sfont_page(vwk->cur_font, (UWORD)page);
    if (font)
        return font;
    for (i = 0; i < n; i++)
    {
        atari = vdi_unicode_to_atari((page << 8) | (UBYTE)chars[i]);
        chars[i] = atari >= 0 ? atari : '?';
    }
    return vwk->cur_font;
}

/* whether text in the workstation's current font goes in runs */
static BOOL text_in_runs(Vwk *vwk)
{
    return vwk->char_map != MAP_ATARI || kerning_applies(vwk);
}

static void run_begin(Vwk *vwk, TEXT_RUN *r, const WORD *str, WORD count)
{
    r->str = str;
    r->count = count;
    r->pos = 0;
    r->kerning = kerning_applies(vwk);
    r->prev_cp = -1;
    r->kern_sum = r->kern_done = 0;
    r->kept_n = 0;
}

/*
 * The kerning before each character of a run (as yet): where it comes to
 * a whole pixel more after the first character, the run is cut there.
 * It goes on from what the runs before have had, and from the pairs'
 * kerning they worked out past where they were cut ('start' is where the
 * run starts in the string), so that each pair is looked up once.
 */
static void run_kern(Vwk *vwk, TEXT_RUN *r, WORD start, const LONG *cps, const WORD *ends)
{
    LONG kern[RUN_MAX], sum = r->kern_sum, done = r->kern_done, next, px;
    WORD i, n = r->n, known = 0;

    if (r->kept_n && r->kept_pos == start)
    {
        known = r->kept_n < n ? r->kept_n : n;
        memcpy(kern, r->kept, known * sizeof(LONG));
    }
    r->kept_n = 0;
    if (known < n && (!vwk->pair_kern
                      || !vdi_sfont_kerning(vwk->cur_font, known ? cps[known - 1] : r->prev_cp,
                                            cps + known, n - known, kern + known)))
        bzero(kern + known, (n - known) * sizeof(LONG));

    r->kern = 0;
    for (i = 0; i < r->n; i++)
    {
        next = sum;
        if (r->prev_cp >= 0 || i)
            next += (kern[i] << 10) + vwk->track_offset;    /* in 16.16 */
        px = (next >= 0 ? next + 0x8000L : next - 0x7fffL) / 0x10000L - done;
        if (i && px)
        {
            /* the run ends before this one, which the next run starts with */
            r->n = i;
            r->pos = ends[i - 1];
            if (vwk->pair_kern)
            {
                r->kept_n = n - i;
                r->kept_pos = r->pos;
                memcpy(r->kept, kern + i, r->kept_n * sizeof(LONG));
            }
            break;
        }
        if (!i)
            r->kern = (WORD)px;
        sum = next;
        done += px;
    }
    r->kern_sum = sum;
    r->kern_done = done;
    r->prev_cp = cps[r->n - 1];
}

/*
 * the next run of characters in one font: as they are in the string where
 * that is what the font has (ASCII in Unicode, all in the Atari set),
 * else copied, RUN_MAX at most; copied, with their code points, for
 * kerning too
 */
static BOOL next_run(Vwk *vwk, TEXT_RUN *r)
{
    WORD start = r->pos, here, ch;
    LONG place, run_place = RUN_SAME, cp;
    BOOL direct = !r->kerning;
    LONG cps[RUN_MAX];
    WORD ends[RUN_MAX];

    r->n = 0;
    while (r->pos < r->count)
    {
        here = r->pos;
        if (vwk->char_map == MAP_ATARI)
        {
            ch = r->str[r->pos++];
            cp = ch >= 0 && ch < 256 ? vdi_atari_to_unicode((UBYTE)ch) : 0;
            place = RUN_SAME;
        }
        else
        {
            cp = next_char(vwk->char_map, r->str, r->count, &r->pos);
            place = char_place(vwk, cp, &ch);
            if (cp < 0 && (vwk->cur_font->flags & F_SFONT))
                cp = 0xfffdL;           /* drawn as the replacement character: kerned so */
        }
        if (r->n == 0)
            run_place = place;
        else if (place != run_place)
        {
            r->pos = here;
            break;
        }
        if (direct && (place != RUN_SAME || r->pos != here + 1 || ch != r->str[here]))
        {
            /* not as it is in the string: copied from here on */
            if (r->n > RUN_MAX - 1)
            {
                r->pos = here;
                break;
            }
            memcpy(r->buf, r->str + start, r->n * sizeof(WORD));
            direct = FALSE;
        }
        if (!direct)
        {
            if (r->n == RUN_MAX)
            {
                r->pos = here;
                break;
            }
            r->buf[r->n] = ch;
            cps[r->n] = cp;
            ends[r->n] = r->pos;
        }
        r->n++;
    }
    if (!r->n)
        return FALSE;

    r->kern = 0;
    if (r->kerning)
        run_kern(vwk, r, start, cps, ends);
    r->chars = direct ? (WORD *)(r->str + start) : r->buf;
    r->font = place_font(vwk, run_place, r->chars, r->n);

    return TRUE;
}

/*
 * the width of a string in the character map: what its runs add up to,
 * less what skewing adds to each but once
 */
static WORD text_bounds(Vwk *vwk, WORD count, const WORD *str, WORD *lo, WORD *hi)
{
    TEXT_RUN run;
    WORD width = 0, skew = 0, raw = 0, n = 0, start;

    *lo = *hi = 0;
    run_begin(vwk, &run, str, count);
    while (next_run(vwk, &run))
    {
        if (vwk->scaled)
        {
            /* one font, scaled as one string, as it is drawn */
            raw += raw_width(run.font, run.n, run.chars);
            n += run.n;
            width += run.kern;
            continue;
        }
        /* a negative offset can take a run back over those before it */
        start = width + run.kern - skew;
        width = start + calc_width(vwk, run.font, run.n, run.chars);
        if (start < *lo)
            *lo = start;
        if (width > *hi)
            *hi = width;
        if (vwk->style & F_SKEW)
            skew = run.font->left_offset + run.font->right_offset;
    }
    if (vwk->scaled)
    {
        width += styled_width(vwk, vwk->cur_font, n, raw);
        *hi = width;
    }
    vdi_sfont_trim();                   /* the pages it took past the cache's bound */

    return width;
}

static WORD text_width(Vwk *vwk, WORD count, const WORD *str)
{
    WORD lo, hi;

    return text_bounds(vwk, count, str, &lo, &hi);
}

/* how many blanks a string has in the character map */
static WORD text_spaces(Vwk *vwk, WORD count, const WORD *str)
{
    WORD pos = 0, n = 0;

    while (pos < count)
        if (next_char(vwk->char_map, str, count, &pos) == ' ')
            n++;

    return n;
}

/* how many characters a string has in the character map (pairs being one) */
static WORD text_chars(Vwk *vwk, WORD count, const WORD *str)
{
    WORD pos = 0, n = 0;

    while (pos < count)
    {
        next_char(vwk->char_map, str, count, &pos);
        n++;
    }

    return n;
}

/*
 * The font and the character in it of a character in the character map
 * (one WORD: a code point of the first plane in UTF-16), for the calls
 * about one character; NULL if it has none
 */
const Fonthead *vdi_text_char(Vwk *vwk, WORD c, WORD *ch)
{
    WORD pos = 0;
    LONG cp, place;

    if (vwk->char_map == MAP_ATARI)
    {
        *ch = c;
        return vwk->cur_font;
    }
    cp = next_char(vwk->char_map, &c, 1, &pos);
    if (cp < 0)
        return NULL;
    place = char_place(vwk, cp, ch);

    return place_font(vwk, place, ch, 1);
}

/*
 * whether the current font has a character (one WORD, as for
 * vdi_text_char()) of a character map other than the Atari one: not just
 * a shape drawn for it, the missing-glyph one or an Atari '?'
 */
static BOOL text_char_exists(Vwk *vwk, WORD c)
{
    const Fonthead *font = vwk->cur_font;
    WORD pos = 0, atari, index;
    LONG cp;

    cp = next_char(vwk->char_map, &c, 1, &pos);
    if (cp <= 0)
        return FALSE;
    if (font->flags & F_SFONT)
    {
        index = vdi_sfont_find(font->font_id);
        return index >= 0 && vdi_sfont_char_glyph(index, cp) != 0;
    }
    atari = vdi_unicode_to_atari(cp);

    return atari >= font->first_ade && atari <= font->last_ade;
}

/*
 * a character's advance, as the text path moves on after it: its width,
 * scaled as the font is, with what bold and outline add (not skewing,
 * which is added once per string); 0 if the font has no such character
 */
WORD vdi_text_advance(Vwk *vwk, WORD c)
{
    const Fonthead *font;
    WORD ch, adv;

    font = vdi_text_char(vwk, c, &ch);
    if (!font)
        return 0;
    if (ch < font->first_ade || ch > font->last_ade)
        ch = '?';                       /* as load_char() draws it */
    adv = calc_width(vwk, font, 1, &ch);
    if (vwk->style & F_SKEW)
        adv -= font->left_offset + font->right_offset;
    vdi_sfont_trim();                   /* a page it took past the cache's bound */

    return adv;
}
#endif

/* the width of a string in the current font, in the character map */
WORD vdi_text_width(Vwk *vwk, WORD count, const WORD *str)
{
#if CONF_WITH_VDI_SCALABLE_FONTS
    if (text_in_runs(vwk))
        return text_width(vwk, count, str);
#endif

    return calc_width(vwk, vwk->cur_font, count, str);
}

#if CONF_WITH_VDI_SCALABLE_FONTS
/* a text string in runs, aligned for 'width' */
static void text_runs(Vwk *vwk, WORD count, WORD *str, WORD width, JUSTINFO *justified)
{
    TEXT_RUN run;
    BOOL cont = FALSE;
    WORD dx, dy;
    Point ul_start;
    JUSTINFO words;
    BOOL page;

    run_begin(vwk, &run, str, count);
    while (next_run(vwk, &run))
    {
        if (cont && run.kern)
        {
            along_baseline(vwk, run.kern, &dx, &dy);
            linea_vars.DESTX += dx;
            linea_vars.DESTY += dy;
        }
        /*
         * a blank is always in the font itself: in a page, the
         * character 0x20 (U+0420, say) is none, and gets no word space
         */
        page = justified && run.font != vwk->cur_font;
        if (page)
        {
            words = *justified;
            justified->wordx = justified->wordy = 0;
            justified->rmword = 0;
        }
        output_run(vwk, run.font, run.n, run.chars, -1, cont ? ALIGN_UNSET : width,
                   justified, cont, run.pos < count, &ul_start);
        if (page)
        {
            justified->wordx = words.wordx;
            justified->wordy = words.wordy;
            justified->rmword = words.rmword;
        }
        cont = TRUE;
    }
}

/* whether a character can be drawn over one before it: negative spacing */
static BOOL runs_overlap(Vwk *vwk)
{
    return vwk->track_offset < 0 || (vwk->pair_kern && kerning_applies(vwk));
}
#endif

/*
 * output a text string, in the workstation's character map
 *
 * 'width' is as for output_run(): the width it is aligned for, if not
 * negative
 */
static void output_text_pass(Vwk *vwk, WORD count, WORD *str, WORD width, JUSTINFO *justified)
{
#if CONF_WITH_VDI_SCALABLE_FONTS
    if (text_in_runs(vwk))
    {
        if (count <= 0)
            return;
        if (width < 0)
            width = text_width(vwk, count, str);
        if (vwk->wrt_mode == WM_REPLACE && runs_overlap(vwk))
        {
            /*
             * a character's cell, background and all, would cover the ink
             * of one before it that it is drawn over: the string is drawn
             * as it is, then its characters again, transparently, from the
             * same point (which the underline has taken over) and with
             * the same spacing
             */
            WORD x = PTSIN[0], y = PTSIN[1];
            JUSTINFO just;

            if (justified)
                just = *justified;
            text_runs(vwk, count, str, width, justified);
            PTSIN[0] = x;
            PTSIN[1] = y;
            if (justified)
                *justified = just;
            vwk->wrt_mode = WM_TRANS;
            text_runs(vwk, count, str, width, justified);
            vwk->wrt_mode = WM_REPLACE;
        }
        else
            text_runs(vwk, count, str, width, justified);
        vdi_sfont_trim();               /* the pages it took past the cache's bound */
        return;
    }
#endif

    output_run(vwk, vwk->cur_font, count, str, width, ALIGN_UNSET, justified, FALSE, FALSE, NULL);
}

/*
 * output a text string, in the workstation's character map: the characters,
 * and in a scalable font, once they are all there, the ink of them that
 * leaves their cells, drawn from the same point and with the same spacing
 * (see draw_overhang())
 */
static void output_text(Vwk *vwk, WORD count, WORD *str, WORD width, JUSTINFO *justified)
{
#if CONF_WITH_VDI_SCALABLE_FONTS
    if ((vwk->cur_font->flags & F_SFONT) && !vwk->scaled && count > 0)
    {
        WORD x = PTSIN[0], y = PTSIN[1], mode = vwk->wrt_mode;
        WORD kept[4];
        JUSTINFO just;

        if (justified)
            just = *justified;
        overhang_seen = FALSE;
        output_text_pass(vwk, count, str, width, justified);
        if (!overhang_seen || mode == WM_ERASE)     /* erase would blank the next glyph's ink */
            return;

        /* the underline has taken the point, and what follows it in ptsin */
        kept[0] = PTSIN[0];
        kept[1] = PTSIN[1];
        kept[2] = PTSIN[2];
        kept[3] = PTSIN[3];
        PTSIN[0] = x;
        PTSIN[1] = y;
        if (justified)
            *justified = just;
        over_pass = TRUE;
        if (mode == WM_REPLACE)
            vwk->wrt_mode = WM_TRANS;   /* not a background over the characters */
        output_text_pass(vwk, count, str, width, justified);
        vwk->wrt_mode = mode;
        over_pass = FALSE;
        PTSIN[0] = kept[0];
        PTSIN[1] = kept[1];
        PTSIN[2] = kept[2];
        PTSIN[3] = kept[3];
        return;
    }
#endif
    output_text_pass(vwk, count, str, width, justified);
}

void vdi_v_gtext(Vwk * vwk)
{
    output_text(vwk, CONTRL->nintin, INTIN, -1, NULL);
}

#if CONF_WITH_VDI_SCALABLE_FONTS
/*
 * v_ftext() is v_gtext(), and v_ftext_offset() (more than one point in
 * ptsin) places each character at its own offset (ptsin[2..], a pair per
 * WORD of the string) from where the one before it is, the first from the
 * point, instead of after the one before, as fVDI does.  In UTF-16, the
 * second WORD of a surrogate pair is no character of its own: its offset
 * is not used.  The alignment is for the extent from the first character
 * to the end of the last.
 */
void vdi_v_ftext(Vwk * vwk)
{
    WORD count = CONTRL->nintin, noffs = CONTRL->nptsin - 1;
    WORD *str = INTIN, *off = PTSIN + 2, *old_ptsin = PTSIN;
    WORD pos, next, last = 0, along, h_align, dx, dy, pen_x, pen_y;
    WORD pts[4];                        /* the underline is drawn through PTSIN[0..3] */
    WORD lstlin = linea_vars.LSTLIN, pass, passes;

    if (noffs < 1)
    {
        output_text(vwk, count, str, -1, NULL);
        return;
    }

    /* the extent along the baseline, for the alignment */
    along = 0;
    if (count > noffs)
        count = noffs;
    for (pos = 0; pos < count; pos = next)
    {
        next = pos;
        next_char(vwk->char_map, str, count, &next);
        if (pos)
            along += baseline_part(vwk, off[2 * pos], off[2 * pos + 1]);
        last = pos;
    }
    along += vdi_text_width(vwk, pos - last, str + last);
    along_baseline(vwk, vwk->h_align == 1 ? along / 2 : vwk->h_align == 2 ? along : 0,
                   &dx, &dy);

    h_align = vwk->h_align;
    vwk->h_align = 0;
    PTSIN = pts;

    /*
     * In replace mode, a character placed over the one before it would
     * cover its ink with its cell's background: the characters are all
     * drawn as they are, then all again, transparently, as output_text()
     * does with a string whose spacing is negative.
     */
    passes = vwk->wrt_mode == WM_REPLACE ? 2 : 1;
    for (pass = 0; pass < passes; pass++)
    {
        if (pass)
            vwk->wrt_mode = WM_TRANS;
        pen_x = old_ptsin[0] - dx;
        pen_y = old_ptsin[1] - dy;
        for (pos = 0; pos < count; pos = next)
        {
            next = pos;
            next_char(vwk->char_map, str, count, &next);
            pen_x += off[2 * pos];
            pen_y += off[2 * pos + 1];
            pts[0] = pen_x;
            pts[1] = pen_y;
            /*
             * underlined in XOR mode, each character's underline leaves its
             * last pixel to the next one's, as the lines of a polyline do,
             * so that where they meet is not drawn twice
             */
            linea_vars.LSTLIN = next < count ? FALSE : lstlin;
            output_text(vwk, next - pos, str + pos, -1, NULL);
        }
    }
    if (passes == 2)
        vwk->wrt_mode = WM_REPLACE;
    linea_vars.LSTLIN = lstlin;
    PTSIN = old_ptsin;
    vwk->h_align = h_align;
}

static void extent_box(Vwk *vwk, WORD width);

/*
 * vqt_f_extent() is vqt_extent() without the skewing (it is the sum of the
 * advances, as NVDI has it); vqt_real_extent() (subcode 4200) is the box
 * the text takes when output at the point in ptsin, alignment and rotation
 * and all: its lower left, lower right, upper right and upper left
 * corners, as the text has them
 */
void vdi_vqt_f_extent(Vwk * vwk)
{
    const Fonthead *fnt_ptr = vwk->cur_font;
    WORD x = PTSIN[0], y = PTSIN[1], width, height, delh, delv, i;
    WORD outline, d1, d2, lo, hi;
    WORD u[4], v[4];

    /*
     * vqt_f_extent() itself: the advances, for where the cursor goes, as
     * vqt_extent() has them but without what skewing adds to the box
     */
    if (CONTRL->subcode != 4200)
    {
        width = vdi_text_width(vwk, CONTRL->nintin, INTIN);
        if (vwk->style & F_SKEW)
            width -= fnt_ptr->left_offset + fnt_ptr->right_offset;
        extent_box(vwk, width);
        return;
    }

    /*
     * placed as output_run() places the text: alignment, skew, outline;
     * the alignment is for where the text ends, the box from where it
     * goes furthest each way (a negative offset taking it back)
     */
    lo = 0;
    if (text_in_runs(vwk))
        width = text_bounds(vwk, CONTRL->nintin, INTIN, &lo, &hi);
    else
        width = hi = calc_width(vwk, fnt_ptr, CONTRL->nintin, INTIN);
    height = calc_height(vwk);
    outline = (vwk->style & F_OUTLINE) ? OUTLINE_THICKNESS : 0;
    d1 = (vwk->style & F_SKEW) ? fnt_ptr->left_offset : 0;
    d2 = (vwk->style & F_SKEW) ? fnt_ptr->right_offset : 0;
    delh = vwk->h_align == 1 ? width / 2 - outline
         : vwk->h_align == 2 ? width - outline * 2 : 0;
    switch(vwk->v_align) {
    default:
        delv = fnt_ptr->top;
        delh += d1;
        break;
    case 1:
        delv = fnt_ptr->top - fnt_ptr->half;
        delh += divs(muls(fnt_ptr->half, d2), fnt_ptr->top);
        break;
    case 2:
        delv = fnt_ptr->top - fnt_ptr->ascent;
        delh += divs(muls(fnt_ptr->ascent, d2), fnt_ptr->top);
        break;
    case 3:
        delv = fnt_ptr->top + fnt_ptr->bottom;
        break;
    case 4:
        delv = fnt_ptr->top + fnt_ptr->descent;
        delh += divs(muls(fnt_ptr->descent, d1), fnt_ptr->bottom);
        break;
    case 5:
        delv = 0;
        delh += d1 + d2;
        break;
    }
    delh += outline;
    delv += outline;

    /* along the baseline and down from the top, in the order of the corners */
    u[0] = u[3] = lo - delh;
    u[1] = u[2] = hi - delh;
    v[0] = v[1] = height - 1 - delv;
    v[2] = v[3] = -delv;
    for (i = 0; i < 4; i++)
    {
        WORD dx, dy;

        along_baseline(vwk, u[i], &dx, &dy);
        switch(vwk->chup) {
        default:
            dy += v[i];
            break;
        case 900:
            dx += v[i];
            break;
        case 1800:
            dy -= v[i];
            break;
        case 2700:
            dx -= v[i];
            break;
        }
        PTSOUT[2 * i] = x + dx;
        PTSOUT[2 * i + 1] = y + dy;
    }
}
#endif

#if CONF_WITH_GDOS
/*
 * trnsfont - converts a font to standard form
 *
 * The routine just does byte swapping.
 *
 * input:
 *     FWIDTH = width of font data in bytes.
 *     DELY   = number of scan lines in font.
 *     FBASE  = starting address of the font data.
 */
static void trnsfont(void)
{
    WORD cnt, i;
    UWORD *addr;

    cnt = (linea_vars.FWIDTH * linea_vars.DELY) / sizeof(*addr);
    for (i = 0, addr = (UWORD *)linea_vars.FBASE; i < cnt; i++, addr++)
        *addr = bswap16(*addr);
}
#endif

void text_init2(Vwk * vwk)
{
    vwk->cur_font = linea_vars.def_font;
    vwk->loaded_fonts = NULL;
    vwk->scrpt2 = scrtsiz;
    vwk->scrtchp = deftxbuf;
    vwk->num_fonts = linea_vars.font_count;
#if CONF_WITH_VDI_SCALABLE_FONTS
    vwk->sfonts_loaded = 0;
    vwk->error_var = NULL;
    vwk->char_map = MAP_ATARI;
    vwk->sf_size = 0;
    vwk->sf_want = 0;
    vwk->sf_width = 0;
    vwk->sf_skew = 0;
    vwk->sf_mirror = 0;
    vwk->pair_kern = FALSE;
    vwk->track_offset = 0;
#endif

    vwk->style = 0;        /* reset special effects */
    vwk->scaled = FALSE;
    vwk->h_align = 0;
    vwk->v_align = 0;
    vwk->chup = 0;
    vwk->pts_mode = FALSE;

    linea_vars.font_ring[2] = vwk->loaded_fonts;
    linea_vars.DEV_TAB[10] = vwk->num_fonts;
}

void text_init(void)
{
    WORD i, j;
    WORD id_save, cell_height;
    const Fonthead *fnt_ptr, **chain_ptr;

    linea_vars.SIZ_TAB[0] = 32767;         /* minimal char width */
    linea_vars.SIZ_TAB[1] = 32767;         /* minimal char height */
    linea_vars.SIZ_TAB[2] = 0;             /* maximal char width */
    linea_vars.SIZ_TAB[3] = 0;             /* maximal char height */

    /* Initialize the font ring. */
    linea_vars.font_ring[0] = &fon6x6;
    linea_vars.font_ring[1] = &fon8x8;
    linea_vars.font_ring[2] = NULL;
    linea_vars.font_ring[3] = NULL;

    id_save = fon6x6.font_id;

    linea_vars.def_font = NULL;
    cell_height = (linea_vars.V_REZ_VT >= 400) ? 16 : 8;   /* to select among default fonts */

    chain_ptr = linea_vars.font_ring;
    i = 0;
    j = 0;
    while ((fnt_ptr = *chain_ptr++)) {
        do {
            if (fnt_ptr->flags & F_DEFAULT) {   /* If default, save font pointer */
                if (!linea_vars.def_font)                  /* ... for sure if we don't have one yet */
                    linea_vars.def_font = fnt_ptr;
                else if (linea_vars.def_font->form_height != cell_height)
                    linea_vars.def_font = fnt_ptr;         /* ... also if previously-saved has wrong height */
            }

            if (fnt_ptr->font_id != id_save) {  /* If new font count */
                j++;
                id_save = fnt_ptr->font_id;
            }

            if (fnt_ptr->font_id == 1) {        /* Update SIZ_TAB if system font */
                if (linea_vars.SIZ_TAB[0] > fnt_ptr->max_char_width)
                    linea_vars.SIZ_TAB[0] = fnt_ptr->max_char_width;

                if (linea_vars.SIZ_TAB[1] > fnt_ptr->top)
                    linea_vars.SIZ_TAB[1] = fnt_ptr->top;

                if (linea_vars.SIZ_TAB[2] < fnt_ptr->max_char_width)
                    linea_vars.SIZ_TAB[2] = fnt_ptr->max_char_width;

                if (linea_vars.SIZ_TAB[3] < fnt_ptr->top)
                    linea_vars.SIZ_TAB[3] = fnt_ptr->top;
                i++;            /* Increment count of heights */
            }
            /* end if system font */

#if CONF_WITH_GDOS
            /*
             * all builtin fonts have standard format, so this test is
             * only useful when we are supporting loaded fonts
             */
            if (!(fnt_ptr->flags & F_STDFORM)) {
                linea_vars.FBASE = fnt_ptr->dat_table;
                linea_vars.FWIDTH = fnt_ptr->form_width;
                linea_vars.DELY = fnt_ptr->form_height;
                trnsfont();
            }
#endif
        } while ((fnt_ptr = fnt_ptr->next_font));
    }
    linea_vars.DEV_TAB[5] = i;                     /* number of sizes */
    linea_vars.font_count = linea_vars.DEV_TAB[10] = ++j;     /* number of faces */
}

/*
 * set up width/height values returned by vst_height()/vst_point()
 */
static void setup_width_height(const Fonthead *font)
{
    WORD *p;
    UWORD top;

    p = PTSOUT;
    *p++ = font->max_char_width;
    *p++ = top = font->top;
    *p++ = font->max_cell_width;
    *p = top + font->bottom + 1;
    flip_y = 1;
}

/* the height of the current font: vst_height(), and vst_font() keeping it */
static void text_height(Vwk * vwk)
{
    const Fonthead **chain_ptr;
    const Fonthead *test_font, *single_font;
    WORD font_id;
    UWORD test_height;
    BOOL found;

    font_id = vwk->cur_font->font_id;
    vwk->pts_mode = FALSE;

#if CONF_WITH_VDI_SCALABLE_FONTS
    /* a scalable font: exactly that height, nothing nearest or scaled */
    if (vwk->cur_font->flags & F_SFONT)
    {
        WORD index = vdi_sfont_find(font_id);
        const Fonthead *f = NULL;

        if (index >= 0)
            f = vdi_sfont_select(vwk, index, vdi_sfont_size_for_top(index, PTSIN[1]), PTSIN[1]);
        if (f)
            vwk->cur_font = f;
        else
            vdi_sfont_error(vwk, SPEEDO_CACHE_FULL);
        vdi_sfont_trim();               /* the size it had may go now */
        vwk->scaled = FALSE;
        setup_width_height(vwk->cur_font);
        return;
    }
#endif

    /* Find the smallest font in the requested face */
    chain_ptr = linea_vars.font_ring;

    found = FALSE;
    while (!found && (test_font = *chain_ptr++)) {
        do {
            if (test_font->font_id == font_id)
                found = TRUE;
        } while (!found && (test_font = test_font->next_font));
    }

    single_font = test_font;
    test_height = PTSIN[1];

    /* Traverse the chains and find the font closest to the size requested. */
    do {
        while ((test_font->top <= test_height)
               && (test_font->font_id == font_id)) {
            single_font = test_font;
            if (!(test_font = test_font->next_font))
                break;
        }
    } while ((test_font = *chain_ptr++));

    /* Set up environment for this font in the non-scaled case */
    vwk->cur_font = single_font;
    vwk->scaled = FALSE;

    if (single_font->top != test_height) {
        vwk->dda_inc = clc_dda(vwk, single_font->top, test_height);
        make_header(vwk);
        single_font = vwk->cur_font;
    }

    setup_width_height(single_font);    /* set up return values */
}


/*
 * act_siz - Actual sizer routine
 *
 * entry:
 *   top     - size to scale (DELY etc)
 *
 * used variables:
 *   vwk->dda_inc (UWORD) - DDA increment passed externally
 *   vwk->t_sclsts (WORD) - 0 if scale down, 1 if enlarge
 *
 * exit:
 *   actual size
 */
static UWORD act_siz(Vwk * vwk, UWORD top)
{
    UWORD accu;
    UWORD retval;
    UWORD i;

    if (vwk->dda_inc == 0xffff) {
        /* double size */
        return (top<<1);
    }
    accu = 0x7fff;
    retval = vwk->t_sclsts ? top : 0;

    for (i = 0; i < top; i++) {
        accu += vwk->dda_inc;
        if (accu < vwk->dda_inc)
            retval++;
    }

    /* if input is non-zero, make return value at least 1 */
    if (top && !retval)
        retval = 1;

    return retval;
}


static void make_header(Vwk * vwk)
{
    const Fonthead *source_font;
    Fonthead *dest_font;

    source_font = vwk->cur_font;
    dest_font = &vwk->scratch_head;

    dest_font->font_id = source_font->font_id;
    dest_font->point = source_font->point * 2;

    memcpy(dest_font->name, source_font->name, FONT_NAME_LEN);

    dest_font->first_ade = source_font->first_ade;
    dest_font->last_ade = source_font->last_ade;

    if (vwk->dda_inc == 0xFFFF) {
        dest_font->top = source_font->top * 2 + 1;
        dest_font->ascent = source_font->ascent * 2 + 1;
        dest_font->half = source_font->half * 2 + 1;
    } else {
        dest_font->top = act_siz(vwk, source_font->top);
        dest_font->ascent = act_siz(vwk, source_font->ascent);
        dest_font->half = act_siz(vwk, source_font->half);
    }
    dest_font->descent = act_siz(vwk, source_font->descent);
    dest_font->bottom = act_siz(vwk, source_font->bottom);
    dest_font->max_char_width = act_siz(vwk, source_font->max_char_width);
    dest_font->max_cell_width = act_siz(vwk, source_font->max_cell_width);
    dest_font->left_offset = act_siz(vwk, source_font->left_offset);
    dest_font->right_offset = act_siz(vwk, source_font->right_offset);
    dest_font->thicken = act_siz(vwk, source_font->thicken);
    dest_font->ul_size = act_siz(vwk, source_font->ul_size);

    dest_font->lighten = source_font->lighten;
    dest_font->skew = source_font->skew;
    dest_font->flags = source_font->flags;

    dest_font->hor_table = source_font->hor_table;
    dest_font->off_table = source_font->off_table;
    dest_font->dat_table = source_font->dat_table;

    dest_font->form_width = source_font->form_width;
    dest_font->form_height = source_font->form_height;

    vwk->scaled = TRUE;
    vwk->cur_font = dest_font;
}


/* the size of the current font: vst_point(), and vst_font() keeping it */
static void text_point(Vwk * vwk)
{
    WORD font_id;
    const Fonthead **chain_ptr, *double_font;
    const Fonthead *test_font, *single_font;
    WORD test_height, h;
    BOOL found;

    font_id = vwk->cur_font->font_id;
    vwk->pts_mode = TRUE;

#if CONF_WITH_VDI_SCALABLE_FONTS
    /* a scalable font: exactly that size, nothing nearest or doubled */
    if (vwk->cur_font->flags & F_SFONT)
    {
        WORD index = vdi_sfont_find(font_id);
        const Fonthead *f = NULL;

        if (index >= 0)
            f = vdi_sfont_select(vwk, index, vdi_sfont_size_for_points(INTIN[0]), 0);
        if (f)
            vwk->cur_font = f;
        else
            vdi_sfont_error(vwk, SPEEDO_CACHE_FULL);
        vdi_sfont_trim();               /* the size it had may go now */
        vwk->scaled = FALSE;
        setup_width_height(vwk->cur_font);
        INTOUT[0] = vwk->cur_font->point;
        return;
    }
#endif

    /* Find the smallest font in the requested face */
    chain_ptr = linea_vars.font_ring;
    found = FALSE;
    while (!found && (test_font = *chain_ptr++)) {
        do {
            if (test_font->font_id == font_id)
                found = TRUE;
        } while (!found && (test_font = test_font->next_font));
    }

    double_font = single_font = test_font;
    test_height = INTIN[0];

    /* Traverse the chains and find the font closest to the size requested */
    /* and closest to half the size requested.                 */
    do {
        while (((h = test_font->point) <= test_height)
               && (test_font->font_id == font_id)) {
            single_font = test_font;
            if (h * 2 <= test_height)
                double_font = test_font;

            if (!(test_font = test_font->next_font))
                break;
        }
    } while ((test_font = *chain_ptr++));

    /* Set up environment for this font in the non-scaled case */
    vwk->cur_font = single_font;
    vwk->scaled = FALSE;

    if (single_font->point != test_height) {
        h = double_font->point * 2;

        if ((h > single_font->point) && (h <= test_height)) {
            vwk->dda_inc = 0xFFFF;
            vwk->t_sclsts = 1;
            vwk->cur_font = double_font;
            make_header(vwk);
            single_font = vwk->cur_font;
        }
    }

    setup_width_height(single_font);    /* set up return values */

    /* also return point size actually set */
    INTOUT[0] = single_font->point;
}


/*
 * vst_height() and vst_point(): a scalable font's width follows its height
 * again (vst_setsize() and vst_width() set it apart)
 */
void vdi_vst_height(Vwk * vwk)
{
#if CONF_WITH_VDI_SCALABLE_FONTS
    vwk->sf_width = 0;
    vwk->sf_mirror = 0;
#endif
    text_height(vwk);
}

void vdi_vst_point(Vwk * vwk)
{
#if CONF_WITH_VDI_SCALABLE_FONTS
    vwk->sf_width = 0;
    vwk->sf_mirror = 0;
#endif
    text_point(vwk);
}


void vdi_vst_effects(Vwk * vwk)
{
    INTOUT[0] = vwk->style = INTIN[0] & linea_vars.INQ_TAB[2];
}


void vdi_vst_alignment(Vwk * vwk)
{
    WORD a, *int_out, *int_in;

    int_in = INTIN;
    int_out = INTOUT;
    a = *int_in++;
    if (a < 0 || a > 2)
        a = 0;
    vwk->h_align = *int_out++ = a;

    a = *int_in;
    if (a < 0 || a > 5)
        a = 0;
    vwk->v_align = *int_out = a;
}


/*
 * the following implementation mimics TOS3/TOS4 behaviour
 *
 * the rotation angle in tenths of a degree may be supplied as any
 * positive or negative WORD value, but it is first normalised to
 * a value between 0 & 3599 by adding or subtracting multiples of
 * 3600, and then rounded to the nearest multiple of 900 between
 * 0 and 3600 inclusive before storing.
 *
 * trivia: a value of 3600 is handled by other VDI code in the same
 * way as a value of 0, but if you really want to store a value of
 * 3600 rather than 0, you can pass a value between 3150 and 3599
 * inclusive ...
 *
 * TOS1 difference: TOS1 does not normalise before rounding, so
 * stored values range from -32400 to +32400; values outside the
 * range of 0 to 3600 inclusive cause vqt_extent() to not return
 * values ...
 */
void vdi_vst_rotation(Vwk * vwk)
{
    WORD angle = INTIN[0];

    while(angle < 0)        /* ensure positive angle */
        angle += 3600;

    while(angle >= 3600)    /* between 0 and 3599 inclusive */
        angle -= 3600;

    /* this sets a value of 0, 900, 1800, 2700 or 3600, just like TOS3/TOS4 */
    INTOUT[0] = vwk->chup = divs(angle + 450, 900) * 900;
}


void vdi_vst_font(Vwk * vwk)
{
    WORD *old_intin, point, *old_ptsout, dummy[4], *old_ptsin;
    WORD face;
    const Fonthead *test_font, **chain_ptr;
    BOOL found;
#if CONF_WITH_VDI_SCALABLE_FONTS
    const Fonthead *old_font = vwk->cur_font;
    BOOL exact = FALSE;                 /* a scalable font, sized here */
#endif

    test_font = vwk->cur_font;
    point = test_font->point;
    dummy[1] = test_font->top;
    face = INTIN[0];

    chain_ptr = linea_vars.font_ring;

    found = FALSE;
    while (!found && (test_font = *chain_ptr++)) {
        do {
            if (test_font->font_id == face)
                found = TRUE;
        } while (!found && (test_font = test_font->next_font));
    }

#if CONF_WITH_VDI_SCALABLE_FONTS
    /* a scalable font the built-in GDOS added to this workstation */
    if (!found && vwk->sfonts_loaded)
    {
        WORD index = vdi_sfont_find(face);

        if (index >= 0 && index < vwk->sfonts_loaded)   /* not one that a later scan added */
        {
            const Fonthead *f;
            LONG size;
            WORD want;

            /*
             * the size as it was: from a scalable font, the exact one asked
             * for (a fractional one from vst_arbpt32(), say), not its point
             * size, which is rounded
             */
            if (old_font->flags & F_SFONT)
            {
                size = vwk->sf_size;
                want = vwk->sf_want;
                if (want)
                    size = vdi_sfont_size_for_top(index, want);
            }
            else if (vwk->pts_mode)
            {
                size = vdi_sfont_size_for_points(point);
                want = 0;
            }
            else
            {
                size = vdi_sfont_size_for_top(index, dummy[1]);
                want = dummy[1];
            }
            f = vdi_sfont_select(vwk, index, size, want);
            if (f)
            {
                test_font = f;
                found = exact = TRUE;
            }
        }
    }
#endif

    /* If we fell through the loop, we could not find the face. */
    /* Default to the system font.                  */
    if (!found)
        test_font = &fon6x6;

    /* Call down to the set text height routine to get the proper size */
    vwk->cur_font = test_font;

#if CONF_WITH_VDI_SCALABLE_FONTS
    if (exact)
    {
        /* already at its size: sizing it again from the points would round it */
        vwk->scaled = FALSE;
        vdi_sfont_trim();
        INTOUT[0] = vwk->cur_font->font_id;
        return;
    }
#endif

    old_intin = INTIN;
    old_ptsin = PTSIN;
    old_ptsout = PTSOUT;
    INTIN = &point;
    PTSIN = PTSOUT = dummy;

    if (vwk->pts_mode)
        text_point(vwk);
    else
        text_height(vwk);

    INTIN = old_intin;
    PTSIN = old_ptsin;
    PTSOUT = old_ptsout;

#if CONF_WITH_VDI_SCALABLE_FONTS
    vdi_sfont_trim();                   /* a scalable size it had may go now */
#endif

    INTOUT[0] = vwk->cur_font->font_id;
}


void vdi_vst_color(Vwk * vwk)
{
    WORD r;

    r = validate_color_index(INTIN[0]);

    INTOUT[0] = r;
    vwk->text_color = MAP_COL[r];
}


void vdi_vqt_attributes(Vwk * vwk)
{
    WORD *pointer;
    const Fonthead *fnt_ptr;

    pointer = INTOUT;
    fnt_ptr = vwk->cur_font;

    *pointer++ = fnt_ptr->font_id;      /* INTOUT[0] */
    *pointer++ = REV_MAP_COL[vwk->text_color];     /* INTOUT[1] */
    *pointer++ = vwk->chup;        /* INTOUT[2] */
    *pointer++ = vwk->h_align;     /* INTOUT[3] */
    *pointer++ = vwk->v_align;     /* INTOUT[4] */
    *pointer = vwk->wrt_mode + 1;  /* INTOUT[5] */

    pointer = PTSOUT;
    *pointer++ = fnt_ptr->max_char_width;
    *pointer++ = fnt_ptr->top;
    *pointer++ = fnt_ptr->max_cell_width;
    *pointer = fnt_ptr->top + fnt_ptr->bottom + 1;  /* handles scaled fonts */

    flip_y = 1;
}


/* the box of vqt_extent(), for text that wide, as it is rotated */
static void extent_box(Vwk *vwk, WORD width)
{
    WORD height;

    height = calc_height(vwk);

    bzero(PTSOUT,8*sizeof(WORD));
    switch (vwk->chup) {
    default:                    /* 0 or 3600 ... see vdi_vst_rotation() */
        PTSOUT[2] = width;
        PTSOUT[4] = width;
        PTSOUT[5] = height;
        PTSOUT[7] = height;
        break;
    case 900:
        PTSOUT[0] = height;
        PTSOUT[2] = height;
        PTSOUT[3] = width;
        PTSOUT[5] = width;
        break;
    case 1800:
        PTSOUT[0] = width;
        PTSOUT[1] = height;
        PTSOUT[3] = height;
        PTSOUT[6] = width;
        break;
    case 2700:
        PTSOUT[1] = width;
        PTSOUT[4] = height;
        PTSOUT[6] = height;
        PTSOUT[7] = width;
        break;
    }
    flip_y = 1;
}

void vdi_vqt_extent(Vwk * vwk)
{
    extent_box(vwk, vdi_text_width(vwk, CONTRL->nintin, INTIN));
}


void vdi_vqt_width(Vwk * vwk)
{
    WORD k;
    WORD *pointer;
    const Fonthead *fnt_ptr;

    fnt_ptr = vwk->cur_font;
    pointer = PTSOUT;

    /* Set that there is no horizontal offset */
    *(pointer + 2) = 0;
    *(pointer + 4) = 0;

    k = INTIN[0];
#if CONF_WITH_VDI_SCALABLE_FONTS
    /* the character as the font has it, in a page of it maybe */
    fnt_ptr = vdi_text_char(vwk, INTIN[0], &k);
    if (!fnt_ptr || (vwk->char_map != MAP_ATARI && !text_char_exists(vwk, INTIN[0])))
    {
        fnt_ptr = vwk->cur_font;
        k = -1;
    }
#endif
    if ((k < fnt_ptr->first_ade) || (k > fnt_ptr->last_ade))
        INTOUT[0] = -1;
    else {
        INTOUT[0] = INTIN[0];
        k -= fnt_ptr->first_ade;
        *(pointer) = fnt_ptr->off_table[k + 1] - fnt_ptr->off_table[k];
        if (vwk->scaled) {
            if (vwk->dda_inc == 0xFFFF)
                *pointer *= 2;
            else
                *pointer = act_siz(vwk, *pointer);
        }

        if (fnt_ptr->flags & F_HORZ_OFF) {
            *(pointer + 2) = fnt_ptr->hor_table[k * 2];
            *(pointer + 4) = fnt_ptr->hor_table[(k * 2) + 1];
        }
    }
#if CONF_WITH_VDI_SCALABLE_FONTS
    vdi_sfont_trim();                   /* a page it took past the cache's bound */
#endif

    flip_y = 1;
}



#if CONF_WITH_VDI_SCALABLE_FONTS
/*
 * The font at a place in the workstation's list, as vqt_name() counts
 * them from 1: a bitmap font, or after those, once vst_load_fonts() has
 * added them, a scalable font, whose index in vdi_sfonts goes in *sfont
 * (else -1).  FALSE if there is no font there.
 */
BOOL vdi_font_at(Vwk *vwk, WORD element, const Fonthead **font, WORD *sfont)
{
    WORD i, current_font_id;
    const Fonthead *tmp_font, **chain_ptr;

    *font = NULL;
    *sfont = -1;
    chain_ptr = linea_vars.font_ring;
    i = 0;
    current_font_id = -1;

    while ((tmp_font = *chain_ptr++)) {
        do {
            if (tmp_font->font_id != current_font_id) {
                current_font_id = tmp_font->font_id;    /* remember current id */
                if ((++i) == element) {
                    *font = tmp_font;
                    return TRUE;
                }
            }
        } while ((tmp_font = tmp_font->next_font));
    }

    if (vwk->sfonts_loaded && element > i && element - i <= vwk->sfonts_loaded)
    {
        *sfont = element - i - 1;
        return TRUE;
    }

    return FALSE;
}
#endif

void vdi_vqt_name(Vwk * vwk)
{
    WORD i, element, current_font_id;
    const char *name;
    WORD *int_out;
    const Fonthead *tmp_font, **chain_ptr;
    BOOL found;

    element = INTIN[0];

#if CONF_WITH_VDI_SCALABLE_FONTS
    /*
     * After the bitmap fonts come the built-in GDOS's scalable fonts, once
     * vst_load_fonts() has added them; intout[33] says which kind a font
     * is, 0 for a bitmap font, 1 for an outline font, as with SpeedoGDOS.
     * NVDI's vqt_ext_name() (subcode 1, with a second intin) keeps that
     * in intout[33], and adds the format and flags in intout[34].
     */
    if (vdi_gdos_id)
    {
        const VDI_SFONT *sfont = NULL;
        WORD index;
        BOOL ext = CONTRL->subcode == 1 && CONTRL->nintin >= 2;

        if (!vdi_font_at(vwk, element, &tmp_font, &index))
            tmp_font = &fon6x6;
        if (index >= 0)
            sfont = &vdi_sfonts[index];

        int_out = INTOUT;
        *int_out++ = sfont ? sfont->id : tmp_font->font_id;
        name = sfont ? sfont->name : tmp_font->name;
        /* at most 31 characters, so that the name always ends in a NUL */
        for (i = 0; i < FONT_NAME_LEN - 1 && name[i]; i++)
            *int_out++ = (UBYTE)name[i];
        while (i++ < FONT_NAME_LEN)
            *int_out++ = 0;
        *int_out++ = sfont ? 1 : 0;
        if (ext)
        {
            if (sfont)
                *int_out = ((sfont->flags & SFONT_MONOSPACE) << 8) | sfont->format;
            else
                *int_out = ((tmp_font->flags & F_MONOSPACE) ? SFONT_MONOSPACE << 8 : 0)
                         | SFONT_BITMAP;
            CONTRL->nintout = 35;
        }
        else
            CONTRL->nintout = 34;
        return;
    }
#endif

    chain_ptr = linea_vars.font_ring;
    i = 0;
    current_font_id = -1;

    found = FALSE;
    while (!found && (tmp_font = *chain_ptr++)) {
        do {
            if (tmp_font->font_id != current_font_id) {
                current_font_id = tmp_font->font_id;    /* remember current id */
                if ((++i) == element)
                    found = TRUE;
            }
        } while (!found && (tmp_font = tmp_font->next_font));
    }

    /* The element is out of bounds - default to the system font */
    if (!found)
        tmp_font = &fon6x6;

    int_out = INTOUT;
    *int_out++ = tmp_font->font_id;
    for (i = 0, name = tmp_font->name; (*int_out++ = *name++); i++)
        ;
    while (i++ < FONT_NAME_LEN)
        *int_out++ = 0;
}


void vdi_vqt_fontinfo(Vwk * vwk)
{
    WORD *pointer;
    const Fonthead *fnt_ptr;

    fnt_ptr = vwk->cur_font;

    pointer = INTOUT;
    *pointer++ = fnt_ptr->first_ade;
    *pointer = fnt_ptr->last_ade;

    pointer = PTSOUT;
    *pointer++ = fnt_ptr->max_cell_width;
    *pointer++ = fnt_ptr->bottom;

    if (vwk->style & F_THICKEN)
        *pointer++ = fnt_ptr->thicken;
    else
        *pointer++ = 0;

    *pointer++ = fnt_ptr->descent;

    if (vwk->style & F_SKEW) {
        *pointer++ = fnt_ptr->left_offset;
        *pointer++ = fnt_ptr->half;
        *pointer++ = fnt_ptr->right_offset;
    } else {
        *pointer++ = 0;
        *pointer++ = fnt_ptr->half;
        *pointer++ = 0;
    }

    *pointer++ = fnt_ptr->ascent;
    *pointer++ = 0;
    *pointer = fnt_ptr->top;

    flip_y = 1;
}


void gdp_justified(Vwk * vwk)
{
    WORD spaces = 0;
    WORD expand;
    WORD interword, interchar;
    WORD cnt, cnt_chars, width, max_x;
    WORD i, direction, delword, delchar;
    WORD *pointer, *str;
    JUSTINFO just;

    cnt = CONTRL->nintin - 2;

    pointer = INTIN;
    interword = *pointer++;
    interchar = *pointer++;
    str = pointer;

    /*
     * if interword adjustment required, count spaces
     */
    if (interword) {
#if CONF_WITH_VDI_SCALABLE_FONTS
        if (vwk->char_map != MAP_ATARI)
            spaces = text_spaces(vwk, cnt, str);    /* a BICS blank is 0 */
        else
#endif
        for (i = 0; i < cnt; i++)
            if (*(pointer++) == ' ')
                spaces++;
    }

    width = vdi_text_width(vwk, cnt, str);
#if CONF_WITH_VDI_SCALABLE_FONTS
    if (vwk->char_map != MAP_ATARI)
        cnt_chars = text_chars(vwk, cnt, str);  /* a pair of surrogates is one */
    else
#endif
        cnt_chars = cnt;

    max_x = PTSIN[2];

    bzero(&just, sizeof(JUSTINFO));     /* set zero default values */

    /*
     * calculate values for interword spacing
     */
    if (interword && spaces) {
        delword = divs((WORD)(max_x - width), spaces);
        just.rmword = mods((WORD)(max_x - width), spaces);

        if (just.rmword < 0) {
            direction = -1;
            just.rmword = 0 - just.rmword;
        } else
            direction = 1;

        if (interchar) {
            expand = vwk->cur_font->max_cell_width / 2;
            if (delword > expand) {
                delword = expand;
                just.rmword = 0;
            }
            if (delword < (0 - expand)) {
                delword = 0 - expand;
                just.rmword = 0;
            }
            width += (delword * spaces) + (just.rmword * direction);
        }

        switch (vwk->chup) {
        default:                /* normally case 0: no rotation */
            just.wordx = delword;
            just.rmwordx = direction;
            break;
        case 900:
            just.wordy = 0 - delword;
            just.rmwordy = 0 - direction;
            break;
        case 1800:
            just.wordx = 0 - delword;
            just.rmwordx = 0 - direction;
            break;
        case 2700:
            just.wordy = delword;
            just.rmwordy = direction;
            break;
        }
    }

    /*
     * calculate values for intercharacter spacing
     */
    if (interchar && cnt_chars > 1) {
        delchar = divs((WORD)(max_x - width), cnt_chars - 1);
        just.rmchar = mods((WORD)(max_x - width), cnt_chars - 1);

        if (just.rmchar < 0) {
            direction = -1;
            just.rmchar = 0 - just.rmchar;
        } else
            direction = 1;

        switch (vwk->chup) {
        default:                /* normally case 0: no rotation */
            just.charx = delchar;
            just.rmcharx = direction;
            break;
        case 900:
            just.chary = 0 - delchar;
            just.rmchary = 0 - direction;
            break;
        case 1800:
            just.charx = 0 - delchar;
            just.rmcharx = 0 - direction;
            break;
        case 2700:
            just.chary = delchar;
            just.rmchary = direction;
            break;
        }
    }

    output_text(vwk, cnt, str, max_x, &just);
}


void vdi_vst_load_fonts(Vwk * vwk)
{
#if CONF_WITH_GDOS
    WORD id, count, *control;

    Fonthead *first_font;

#if CONF_WITH_VDI_SCALABLE_FONTS
    /*
     * The built-in GDOS is only active when no GDOS was loaded from disk,
     * so this is an application's own call, not a GDOS passing its fonts
     * on: add the scalable fonts, once, as SpeedoGDOS does.
     */
    if (vdi_gdos_id)
    {
        if (vwk->sfonts_loaded)
            INTOUT[0] = 0;
        else
        {
            vdi_sfont_rescan();     /* if the boot-time scan found none, or after an unload */
            /*
             * Loaded only if there are fonts to add: a workstation that
             * found none can ask again, once they are there, and cannot
             * see a list that its count does not include.
             */
            if (vdi_sfont_count)
            {
                vwk->sfonts_loaded = vdi_sfont_count;   /* the fonts of this workstation: later scans add more */
                vwk->num_fonts += vdi_sfont_count;
            }
            INTOUT[0] = vwk->sfonts_loaded;
        }
        return;
    }
#endif

    /* Init some common variables */
    control = (WORD *) CONTRL;

    /* You only get one chance to load fonts.  If fonts are linked in, exit  */
    if (vwk->loaded_fonts) {
        INTOUT[0] = 0;
        return;
    }

    /* The inputs to this routine are :         */
    /* CONTRL[7-8]   = Pointer to scratch buffer    */
    /* CONTRL[9]     = Offset to buffer 2       */
    /* CONTRL[10-11] = Pointer to first font    */

    /* Init the global structures           */
    vwk->scrpt2 = control[9];
    vwk->scrtchp = (WORD *)(uintptr_t)ULONG_AT(&control[7]);

    first_font = (Fonthead *)(uintptr_t)ULONG_AT(&control[10]);
    vwk->loaded_fonts = first_font;

    /* Find out how many distinct font id numbers have just been linked in. */
    id = -1;
    count = 0;

    do {
        /* Update the count of font id numbers, if necessary. */
        if (first_font->font_id != id) {
            id = first_font->font_id;
            count++;
        }

        /* Make sure the font is in device specific format. */
        if (!(first_font->flags & F_STDFORM)) {
            linea_vars.FBASE = first_font->dat_table;
            linea_vars.FWIDTH = first_font->form_width;
            linea_vars.DELY = first_font->form_height;
            trnsfont();
            first_font->flags |= F_STDFORM;
        }
        first_font = first_font->next_font;
    } while (first_font);

    linea_vars.font_ring[2] = vwk->loaded_fonts;

    /* Update the device table count of faces. */
    vwk->num_fonts += count;
    INTOUT[0] = count;
#else
    INTOUT[0] = 0;      /* we loaded no new fonts */
#endif
}


void vdi_vst_unload_fonts(Vwk * vwk)
{
#if CONF_WITH_GDOS
#if CONF_WITH_VDI_SCALABLE_FONTS
    vwk->sfonts_loaded = 0;
    vdi_sfont_unloaded();
#endif
    /* Since we always unload all fonts, this is easy. */
    vwk->loaded_fonts = NULL;           /* No fonts installed */
    linea_vars.font_ring[2] = NULL;
    vwk->scrpt2 = SCRATCHBUF_OFFSET;    /* Reset pointers to default buffers */
    vwk->scrtchp = vdishare.deftxbuf;
    vwk->num_fonts = linea_vars.font_count;        /* Reset font count to default */
#endif
}


/*
 * input:
 *   req - d0, get requested size
 *   act - d1, get actual size
 *
 * output:
 *   vwk->t_sclsts is the text scaling flag (means: scale up or down)
 */
static UWORD clc_dda(Vwk * vwk, UWORD act, UWORD req)
{
    if ( req < act ) {
        vwk->t_sclsts = 0;           /* we do scale down */
        if ( req == 0 ) {
            req = 1;            /* if 0 then make it 1 (minimum value) */
        }
    }
    else {
        vwk->t_sclsts = 1;           /* we do scale up */
        req -= act;
        /* if larger than 2x? */
        if ( req >= act )
            return 0xFFFF;          /* indicates 2x scale up */
    }

    return divu(((ULONG)req)<<16, act);
}
