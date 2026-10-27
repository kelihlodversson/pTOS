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
/* Native formatted text uses the existing AES scratch area. libaes binds
 * the same fields to its private context instead. */
#ifndef AES_RENDER_BINDINGS
#include "struct.h"
#include "gemlib.h"
#include "geminit.h"
#endif
#include "gemrender_binding.h"

#if OB_RENDER_3D
/*
 * 3D globals set and returned by objc_sysvar()
 */
static WORD indtxtmove;         /* 1 => indicators move text */
static WORD indcolchange;       /* 1 => indicators change colour */
static WORD acttxtmove;         /* 1 => activators move text */
static WORD actcolchange;       /* 1 => activators change colour */
static WORD indbutcol;          /* indicator button colour */
static WORD actbutcol;          /* activator button colour */
WORD backgrcol;                 /* background colour - used by fm_alert() */

/*
 *  4-colour table used by xor_color():
 *
 *  Pixel value  VDI pen#   Colour       Pixel XOR    Complementary colour
 *       00         0       white            11       black
 *       01         2       red              10       green
 *       10         3       green            01       red
 *       11         1       black            00       white
 */
static const UBYTE vdi4comp[] = { BLACK, WHITE, GREEN, RED };

/*
 *  16-colour table used by xor_color():
 *
 *  Pixel value  VDI pen#   Colour       Pixel XOR    Complementary colour
 *      0000        0       white           1111      black
 *      0001        2       red             1110      light cyan
 *      0010        3       green           1101      light magenta
 *      0011        6       yellow          1100      light blue
 *      0100        4       blue            1011      light yellow
 *      0101        7       magenta         1010      light green
 *      0110        5       cyan            1001      light red
 *      0111        8       low white       1000      grey
 *      1000        9       grey            0111      low white
 *      1001       10       light red       0110      cyan
 *      1010       11       light green     0101      magenta
 *      1011       14       light yellow    0100      blue
 *      1100       12       light blue      0011      yellow
 *      1101       15       light magenta   0010      green
 *      1110       13       light cyan      0001      red
 *      1111        1       black           0000      white
 */
static const UBYTE vdi16comp[] = {
    BLACK, WHITE, LCYAN, LMAGENTA, LYELLOW, LRED, LBLUE, LGREEN,
    LBLACK, LWHITE, CYAN, MAGENTA, YELLOW, RED, BLUE, GREEN
};

/*
 * Return the complementary (XOR) colour for a specified colour.  For
 * colours beyond the maximum colour number for the current resolution,
 * or beyond 15, returns WHITE.
 */
static WORD xor_color(WORD color)
{
    if ((color >= 16) || (color >= gl_ws.ws_ncolors))
        return WHITE;

    if (gl_ws.ws_ncolors <= 4)
        return vdi4comp[color];

    return vdi16comp[color];
}

/*
 * Return TRUE iff it's ok to toggle an object's SELECTED state via XOR
 */
static BOOL xor_is_ok(WORD obtype, WORD obflags, OBSPEC obspec)
{
    WORD colour_word;

    if ((obtype == G_IBOX) || (obtype == G_STRING) || (obtype == G_TITLE))
        return TRUE;

    if ((obtype == G_IMAGE) || (obflags & FL3DOBJ))
        return FALSE;

    if (gl_ws.ws_ncolors <= 16)
        return TRUE;

    /*
     * for other objects, it's ok as long as they are black & white
     */
    switch(obtype)
    {
    case G_BOXTEXT:
    case G_FBOXTEXT:
    case G_TEXT:
    case G_FTEXT:
        colour_word = obspec.tedinfo->te_color;
        break;
    case G_BOX:
    case G_BOXCHAR:
        colour_word = LOWORD(obspec.index);
        break;
    default:    /* G_USERDEF, G_ICON, G_CICON */
        return TRUE;
    }

    /* check internal colour */
    if ((colour_word & 0x000f) > BLACK)
        return FALSE;

    /* check text colour */
    if (((colour_word>>8) & 0x000f) > BLACK)
        return FALSE;

    return TRUE;
}

/*
 * Draw outline for 3D background object
 */
static void draw_3d_outline(GRECT *pt)
{
    WORD pts[6], i;

    gsx_moff();

    gsx_attr(FALSE, MD_REPLACE, LBLACK);
    /*
     * draw 3 grey nested lines from bottom left to bottom right to top right:
     *                [4,5]
     *                  |
     *      [0,1] --- [2,3]
     */
    pts[0] = pt->g_x - 3;
    pts[1] = pt->g_y + pt->g_h + 2;
    pts[2] = pts[0] + pt->g_w + 5;
    pts[3] = pts[1];
    pts[4] = pts[2];
    pts[5] = pt->g_y - 3;
    v_pline(3, pts);

    for (i = 0; i < 2; i++)
    {
        pts[0]++;
        pts[1]--;
        pts[2]--;
        pts[3]--;
        pts[4]--;
        pts[5]++;
        v_pline(3, pts);
    }

    /*
     * draw 3 nested lines from bottom left to top left to top right:
     *      [2,3] --- [4,5]
     *        |
     *      [0,1]
     * note: the outermost is grey, the inner are white
     */
    pts[0] = pt->g_x - 3;
    pts[1] = pt->g_y + pt->g_h + 2;
    pts[2] = pts[0];
    pts[3] = pt->g_y - 3;
    pts[4] = pts[2] + pt->g_w + 5;
    pts[5] = pts[3];
    v_pline(3, pts);

    gsx_attr(FALSE, MD_REPLACE, WHITE);

    for (i = 0; i < 2; i++)
    {
        pts[0]++;
        pts[1]--;
        pts[2]++;
        pts[3]++;
        pts[4]--;
        pts[5]++;
        v_pline(3, pts);
    }

    gsx_mon();
}

/*
 * Draw highlights & shadows around a rectangle to give a 3D appearance:
 * raised if not selected, depressed if selected.
 */
static void add_3d_effect(GRECT *pt, WORD obstate, WORD th, WORD color)
{
    WORD pts[6], draw_color;
    GRECT r;

    gsx_moff();
    vsl_type(7);        /* user-defined line type */
    vsl_udsty(0xffff);  /* solid */

    rc_copy(pt, &r);    /* make a local copy as a precaution */

    /* for inside border, reduce apparent object size */
    if (th > 0)
    {
        r.g_x += th;
        r.g_y += th;
        r.g_w -= 2 * th;
        r.g_h -= 2 * th;
    }

    /*
     * draw a line from bottom left to top left to top right:
     *      [2,3] --- [4,5]
     *        |
     *      [0,1]
     * note that we stop short by 1 pixel at both ends of the line
     */
    pts[0] = pts[2] = r.g_x;
    pts[3] = pts[5] = r.g_y;
    pts[1] = pts[3] + r.g_h - 2;
    pts[4] = pts[2] + r.g_w - 2;

    draw_color = (obstate & SELECTED) ? BLACK : WHITE;
    gsx_attr(FALSE, MD_REPLACE, draw_color);
    v_pline(3,pts);

    /*
     * draw a line from bottom left to bottom right to top right:
     *                [4,5]
     *                  |
     *      [0,1] --- [2,3]
     * note that we stop short by 1 pixel at both ends of the line
     */
    pts[0]++;
    pts[1]++;
    pts[3] = pts[1];
    pts[4]++;
    pts[2] = pts[4];
    pts[5]++;

    draw_color = (obstate & SELECTED) ? LWHITE : LBLACK;
    if ((draw_color == color) || (gl_ws.ws_ncolors < draw_color))
        draw_color = (obstate & SELECTED) ? WHITE : BLACK;
    gsx_attr(FALSE, MD_REPLACE, draw_color);
    v_pline(3,pts);

    gsx_mon();
}
#endif

/*
 *  Routine to take an unformatted raw string and, based on a
 *  template string, build a formatted string.
 */
void ob_format(WORD just, char *raw_str, char *tmpl_str, char *fmt_str)
{
    char *pfbeg, *ptbeg, *prbeg;
    char *ptend, *prend;
    WORD inc, ptlen, prlen;

    if (*raw_str == '@')
        *raw_str = '\0';

    pfbeg = fmt_str;
    ptbeg = tmpl_str;
    prbeg = raw_str;

    ptlen = strlen(tmpl_str);
    prlen = strlen(raw_str);

    inc = 1;
    pfbeg[ptlen] = '\0';
    if (just == TE_RIGHT)
    {
        inc = -1;
        pfbeg = pfbeg + ptlen - 1;
        ptbeg = ptbeg + ptlen - 1;
        prbeg = prbeg + prlen - 1;
    }

    ptend = ptbeg + muls(inc, ptlen);
    prend = prbeg + muls(inc, prlen);

    while(ptbeg != ptend)
    {
        if (*ptbeg != '_')
            *pfbeg = *ptbeg;
        else
        {
            if (prbeg != prend)
            {
                *pfbeg = *prbeg;
                prbeg += inc;
            }
            else
                *pfbeg = '_';
        }

        pfbeg += inc;
        ptbeg += inc;
    }
}


static __inline__ WORD call_usercode(USERBLK *ub, PARMBLK *pb)
{
#if CONF_WITH_NATIVE_ABI
    WORD (*ub_code)(PARMBLK *parmblock) = ub->ub_code;
    return ub_code(pb);
#else
    register WORD (*ub_code)(PARMBLK *parmblock) __asm__("a0") = ub->ub_code;
    register WORD retvalue __asm__("d0");

    __asm__ volatile
    (
        "move.l  %2,-(sp)\n\t"
        "jsr     (%1)\n\t"
        "addq.l  #4,sp\n\t"
    : "=r"(retvalue)
    : "a"(ub_code), "g"(pb)
    : "d1", "d2", "a1", "a2", "memory", "cc"
    );
    return retvalue;
#endif
}


/*
 *  Routine to load up and call a user-defined object draw or change
 *  routine.
 */
static WORD ob_user(OBJECT *tree, WORD obj, GRECT *pt, OBSPEC spec,
                    WORD curr_state, WORD new_state)
{
    PARMBLK pb;
    USERBLK *ub = spec.userblk;

    pb.pb_tree = tree;
    pb.pb_obj = obj;
    pb.pb_prevstate = curr_state;
    pb.pb_currstate = new_state;
    rc_copy(pt, (GRECT *)&pb.pb_x);     /* FIXME: Ugly typecasting */
    gsx_gclip((GRECT *)&pb.pb_xc);      /* FIXME: ditto */
    pb.pb_parm = ub->ub_parm;

    return call_usercode(ub, &pb);
}


#if OB_RENDER_NICELINES
/*
 *  Routine to determine if an object's text is all dashes
 */
static BOOL is_dashes(char *s, WORD len)
{
    while(len--)
    {
        if (*s++ != '-')
            return FALSE;
    }

    return TRUE;
}
#endif


/*
 *  Routine to draw an object from an object tree.
 */
void ob_render_object(OBJECT *tree, WORD obj, WORD sx, WORD sy)
{
    WORD bcol = BLACK, tcol = BLACK, ipat = IP_HOLLOW, icol = WHITE, tmode = MD_REPLACE, th;
    WORD state, obtype, len, flags;
    OBSPEC spec;
    WORD tmpx, tmpy, tmpth;
    char ch;
    GRECT t, c;             /* 'c' is used as a temporary copy of 't' */
    TEDINFO edblk;
    BITBLK bi;
    ICONBLK ib;
    CICON *cicon;
#if OB_RENDER_3D
    GRECT effect_grect;     /* saved copy of 't', used by add_3d_effect() */
    WORD effect_th;
    BOOL movetext, changecol;
#endif

    if (ob_render_cancelled(tree) || (tree[obj].ob_flags & HIDETREE))
        return;
    ch = ob_sst(tree, obj, &spec, &state, &obtype, &flags, &t, &th);

    if (spec.index == -1L)
        return;

    t.g_x = sx;
    t.g_y = sy;

#if OB_RENDER_3D
    effect_th = th;         /* save for add_3d_effect() at the end */

    /*
     * for 3D objects, adjust object size.  for all objects, set
     * movetext/changecol booleans.
     */
    if (flags & FL3DOBJ)
    {
        t.g_x -= ADJ3DSTD;
        t.g_y -= ADJ3DSTD;
        t.g_w += 2 * ADJ3DSTD;
        t.g_h += 2 * ADJ3DSTD;
        if ((flags & FL3DMASK) == FL3DACT)
        {
            movetext = acttxtmove;
            changecol = actcolchange;
        }
        else
        {
            movetext = indtxtmove;
            changecol = indcolchange;
        }
    }
    else
    {
        movetext = FALSE;
        changecol = !xor_is_ok(obtype, flags, spec);
    }
#endif

    /*
     * do trivial reject with full extent including outline, shadow,
     * & thickness
     */
    if (gl_clip.g_w && gl_clip.g_h)
    {
        rc_copy(&t, &c);
        if (state & OUTLINED)
            gr_inside(&c, -3);
        else
            gr_inside(&c, ((th < 0) ? (3 * th) : (-3 * th)) );

        if (!(gsx_chkclip(&c)))
            return;
    }

#if OB_RENDER_3D
    rc_copy(&t, &effect_grect); /* save for add_3d_effect() at the end */
#endif

    /*
     * handle all non-string types
     */
    if (obtype != G_STRING)
    {
        tmpth = (th < 0) ? 0 : th;
        tmode = MD_REPLACE;
        tcol = BLACK;

        /*
         * for all tedinfo types, get copy of ted, crack the
         * color word and set the text color
         */
        switch(obtype)
        {
        case G_BOXTEXT:
        case G_FBOXTEXT:
        case G_TEXT:
        case G_FTEXT:
            memcpy(&edblk, spec.tedinfo, sizeof(TEDINFO));
            gr_crack(edblk.te_color, &bcol,&tcol, &ipat, &icol, &tmode);
#if OB_RENDER_3D
            /*
             * handle drawing the background of all text objects in colour
             *
             * when drawing in replace mode, the VDI always sets the text
             * background to white, so to draw a coloured background on
             * text objects, we need to:
             *  1) set the pattern to solid
             *  2) set the colour to the 3D background colour
             *  3) change a text mode of replace to transparent
             * additionally, for TEXT/FTEXT objects we need to:
             *  4) change them to BOXTEXT/FBOXTEXT with border thickness zero
             */
            if ((flags & FL3DMASK) && (ipat == IP_HOLLOW) && (icol == WHITE))
            {
                ipat = IP_SOLID;
                icol = backgrcol;
                if (tmode == MD_REPLACE)
                {
                    tmode = MD_TRANS;
                    if (obtype == G_TEXT)
                    {
                        obtype = G_BOXTEXT;
                        tmpth = th = 0;
                    }
                    else if (obtype == G_FTEXT)
                    {
                        obtype = G_FBOXTEXT;
                        tmpth = th = 0;
                    }
                }
            }
#endif
            break;
        }

        /*
         * handle box types: if not ted, crack the color; for all
         * box types, draw the box with border
         */
        switch(obtype)
        {
        case G_BOX:
        case G_BOXCHAR:
        case G_IBOX:
            gr_crack(spec.index, &bcol, &tcol, &ipat, &icol, &tmode);
#if OB_RENDER_3D
            /*
             * handle BOX/BOXCHAR colours
             */
            if ((obtype != G_IBOX) && (ipat == IP_HOLLOW) && (icol == WHITE))
            {
                switch(flags & FL3DMASK)
                {
                case FL3DIND:
                    ipat = IP_SOLID;
                    icol = indbutcol;
                    break;
                case FL3DACT:
                    ipat = IP_SOLID;
                    icol = actbutcol;
                    break;
                case FL3DBAK:
                    ipat = IP_SOLID;
                    icol = backgrcol;
                    break;
                }
            }
#endif
            FALLTHROUGH;
        case G_BUTTON:
            if (obtype == G_BUTTON)
            {
                bcol = BLACK;
#if OB_RENDER_3D
                /*
                 * handle BUTTON colours
                 */
                if (flags & FL3DOBJ)
                {
                    ipat = IP_SOLID;
                    icol = ((flags&FL3DMASK)==FL3DACT) ? actbutcol : indbutcol;
                }
                else
#endif
                {
                    ipat = IP_HOLLOW;
                    icol = WHITE;
                }
            }
            FALLTHROUGH;
        case G_BOXTEXT:
        case G_FBOXTEXT:
            /* draw box's border */
            if (th != 0)
            {
                gsx_attr(FALSE, MD_REPLACE, bcol);
                gr_box(t.g_x, t.g_y, t.g_w, t.g_h, th);
            }
            /* draw filled box */
            if (obtype != G_IBOX)
            {
                gr_inside(&t, tmpth);
#if OB_RENDER_3D
                /*
                 * handle BOX, BOXCHAR, BUTTON, BOXTEXT, FBOXTEXT selection
                 *
                 * if we must change colour when selected, and we are
                 * selected:
                 *  1) for a hollow box, make it solid & black
                 *  2) otherwise, set an explicit colour
                 */
                if (changecol && (state & SELECTED))
                {
                    if (ipat == IP_HOLLOW)
                    {
                        ipat = IP_SOLID;
                        icol = BLACK;
                    }
                    else
                        icol = xor_color(icol);
                }
#endif
                gr_rect(icol, ipat, &t);
                gr_inside(&t, -tmpth);
            }
            break;
        }

#if OB_RENDER_3D
        /*
         * handle text colour change for SELECTED objects
         */
        if (changecol && (state & SELECTED))
        {
            tmode = MD_TRANS;
            tcol = xor_color(tcol);
        }
#endif

        gsx_attr(TRUE, tmode, tcol);

        /*
         * do what's left for all types
         */
        switch( obtype )
        {
        case G_FTEXT:
        case G_FBOXTEXT:
            strcpy(D.g_rawstr, edblk.te_ptext);
            strcpy(D.g_tmpstr, edblk.te_ptmplt);
            ob_format(edblk.te_just, D.g_rawstr, D.g_tmpstr, D.g_fmtstr);
            FALLTHROUGH; /* to gr_gtext */
        case G_BOXCHAR:
            edblk.te_ptext = D.g_fmtstr;
            if (obtype == G_BOXCHAR)
            {
                D.g_fmtstr[0] = ch;
                D.g_fmtstr[1] = '\0';
                edblk.te_just = TE_CNTR;
                edblk.te_font = IBM;
            }
            FALLTHROUGH; /* to gr_gtext */
        case G_TEXT:
        case G_BOXTEXT:
            /*
             * we need to pass a modified grect to gr_gtext(), so make a copy
             */
            rc_copy(&t, &c);
            gr_inside(&c, tmpth);
#if OB_RENDER_3D
            /*
             * for TEXT/BOXTEXT/BOXCHAR/FTEXT/FBOXTEXT objects, check
             * if the text should be moved when toggling SELECTED.
             *
             * if so, assuming the 'base' position is (x,y), the
             * positions will be:
             *  unselected: (x,y-1)
             *  selected:   (x+1,y)
             */
            if (movetext)
            {
                if (state & SELECTED)
                    c.g_x++;
                else
                    c.g_y--;
            }
#endif
            gr_gtext(edblk.te_just, edblk.te_font, edblk.te_ptext, &c);
            break;
        case G_IMAGE:
            bi = *(spec.bitblk);
#if OB_RENDER_3D
            /*
             * if a G_IMAGE is selected, we can XOR the background (since
             * that is always black), then set the colour for the image
             */
            if (state & SELECTED)
            {
                bb_fill(MD_XOR, FIS_SOLID, IP_SOLID, t.g_x, t.g_y, t.g_w, t.g_h);
                bi.bi_color = xor_color(bi.bi_color);
            }
#endif
            gsx_blt((void *)bi.bi_pdata, bi.bi_x, bi.bi_y,
                    t.g_x, t.g_y, bi.bi_wb * 8,
                    bi.bi_hl, MD_TRANS, bi.bi_color, WHITE);
            break;
        case G_ICON:
            cicon = NULL;
#if OB_RENDER_COLOUR_ICONS
            FALLTHROUGH;
        case G_CICON:   /* a CICONBLK starts with an ICONBLK */
            if (obtype == G_CICON)
                cicon = spec.ciconblk->mainlist;
#endif
            ib = *(spec.iconblk);
            ib.ib_xicon += t.g_x;
            ib.ib_yicon += t.g_y;
            ib.ib_xtext += t.g_x;
            ib.ib_ytext += t.g_y;
            gr_gicon(state, &ib, cicon);
            state &= ~SELECTED;
            break;
        case G_USERDEF:
            state = ob_user(tree, obj, &t, spec, state, state);
            break;
        } /* switch type */
    }

    /*
     * draw text for string/title/button
     */
    if ((obtype == G_STRING) ||
        (obtype == G_TITLE) ||
        (obtype == G_BUTTON))
    {
        len = expand_string(intin, spec.free_string);
        if (len)
        {
            tcol = BLACK;
#if OB_RENDER_3D
            /*
             * switch text colour if necessary for selected BUTTON objects
             */
            if ((obtype == G_BUTTON) && changecol && (state & SELECTED))
                tcol = WHITE;
#endif
            gsx_attr(TRUE, MD_TRANS, tcol);
            tmpx = t.g_x;
            tmpy = t.g_y + ((t.g_h-gl_hchar)/2);
            if (obtype == G_BUTTON)
            {
                tmpx += ((t.g_w-(len*gl_wchar))/2);
#if OB_RENDER_3D
                /*
                 * for a BUTTON object, shift text position as required
                 */
                if (movetext && !(state & SELECTED))
                {
                    tmpx--;
                    tmpy--;
                }
#endif
            }
#if OB_RENDER_NICELINES
            /*
             * for an apparent menu separator, we replace the traditional
             * string of dashes with a drawn line for neatness
             */
            if ((obtype == G_STRING) && (state & DISABLED) && (len > 3) && is_dashes(spec.free_string, len))
            {
                gsx_cline(t.g_x, t.g_y+t.g_h/2, t.g_x+t.g_w-1, t.g_y+t.g_h/2);
            }
            else
#endif
            {
                gsx_tblt(IBM, tmpx, tmpy, len);
            }
#if OB_RENDER_EXTENDED
            /*
             * handle underlining for string objects
             */
            if ((obtype == G_STRING) && (state & WHITEBAK) && ((state&0xFF00) == 0xFF00))
            {
                gsx_attr(FALSE, MD_REPLACE, LBLACK);
                gsx_cline(t.g_x, t.g_y+t.g_h+2, t.g_x+t.g_w, t.g_y+t.g_h+2);
#if OB_RENDER_3D
                gsx_attr(FALSE, MD_REPLACE, WHITE);
                gsx_cline(t.g_x, t.g_y+t.g_h+1, t.g_x+t.g_w, t.g_y+t.g_h+1);
#endif
            }
#endif
        }
    }

    /*
     * handle changes in appearance due to object state
     */
    if (state)
    {
        if (state & OUTLINED)
        {
#if OB_RENDER_3D
            /*
             * handle OUTLINED for 3D backgrounds
             */
            if ((flags & FL3DMASK) == FL3DBAK)
            {
                draw_3d_outline(&t);
            }
            else
#endif
            {
                gsx_attr(FALSE, MD_REPLACE, BLACK);
                gr_box(t.g_x-3, t.g_y-3, t.g_w+6, t.g_h+6, 1);
                gsx_attr(FALSE, MD_REPLACE, WHITE);
                gr_box(t.g_x-2, t.g_y-2, t.g_w+4, t.g_h+4, 2);
            }
        }

        if (th > 0)
            gr_inside(&t, th);
        else
            th = -th;

        if ((state & SHADOWED) && th)
        {
            vsf_color(bcol);
            bb_fill(MD_REPLACE, FIS_SOLID, 0, t.g_x, t.g_y+t.g_h+th,
                    t.g_w + th, 2*th);
            bb_fill(MD_REPLACE, FIS_SOLID, 0, t.g_x+t.g_w+th, t.g_y,
                    2*th, t.g_h+(3*th));
        }

        if (state & CHECKED)
        {
            gsx_attr(TRUE, MD_TRANS, BLACK);
            intin[0] = 0x08;                            /* a check mark */
            gsx_tblt(IBM, t.g_x+2, t.g_y, 1);
        }

        if (state & CROSSED)
        {
            gsx_attr(FALSE, MD_TRANS, WHITE);
            gsx_cline(t.g_x, t.g_y, t.g_x+t.g_w-1, t.g_y+t.g_h-1);
            gsx_cline(t.g_x, t.g_y+t.g_h-1, t.g_x+t.g_w-1, t.g_y);
        }

        if (state & DISABLED)
        {
            bcol = WHITE;
#if OB_RENDER_3D
            /*
             * handle DISABLED for 3D backgrounds
             */
            if ((flags & FL3DMASK) == FL3DBAK)
                bcol = backgrcol;
#endif
            vsf_color(bcol);
            bb_fill(MD_TRANS, FIS_PATTERN, IP_4PATT, t.g_x, t.g_y,
                    t.g_w, t.g_h);
        }

        if (state & SELECTED)
        {
#if OB_RENDER_3D
            /*
             * handle SELECTED for non-3D objects
             */
            if (!(flags & FL3DOBJ) && !changecol)
#endif
            {
                bb_fill(MD_XOR, FIS_SOLID, IP_SOLID, t.g_x, t.g_y, t.g_w, t.g_h);
            }
        }
    }

#if OB_RENDER_3D
    /*
     * do 3D effect for activators, indicators
     */
    if (flags & FL3DOBJ)
        add_3d_effect(&effect_grect, state, effect_th, icol);
#endif
} /* ob_render_object */

void ob_render_change(OBJECT *tree, WORD obj, UWORD new_state, WORD redraw)
{
    WORD flags, obtype, th;
    GRECT t;
    UWORD curr_state;
    OBSPEC spec;
    OBJECT *objptr;

    ob_sst(tree, obj, &spec, (WORD*)&curr_state, &obtype, &flags, &t, &th);

    if ((curr_state == new_state) || (spec.index == -1L))
        return;

    objptr = tree + obj;
    objptr->ob_state = new_state;

    /* if no redraw, we're done */
    if (!redraw)
        return;

    ob_offset(tree, obj, &t.g_x, &t.g_y);

    gsx_moff();

    if (th < 0)
        th = 0;

    switch(obtype)
    {
    case G_USERDEF: /* just call the user's drawing routine */
        ob_user(tree, obj, &t, spec, curr_state, new_state);
        redraw = FALSE;
        break;
    case G_ICON:    /* never use XOR for icons */
        break;
    default:        /* others: handle change of SELECTED state */
#if OB_RENDER_3D
        /*
         * if we can toggle selection via XOR, we use XOR and don't redraw.
         * if we have a G_IMAGE object that we're deselecting, we use XOR
         * but force a redraw (xor_is_ok() returns FALSE for G_IMAGE).
         */
        if ((new_state ^ curr_state) & SELECTED)
        {
            BOOL ok = xor_is_ok(obtype,flags, spec);
            if (ok || ((obtype == G_IMAGE) && !(new_state & SELECTED)))
            {
                bb_fill(MD_XOR, FIS_SOLID, IP_SOLID, t.g_x+th, t.g_y+th,
                        t.g_w-(2*th), t.g_h-(2*th));
                redraw = !ok;
            }
        }
#else
        /* no 3D support: always use XOR if SELECTED state has changed */
        if ((new_state ^ curr_state) & SELECTED)
        {
            bb_fill(MD_XOR, FIS_SOLID, IP_SOLID, t.g_x+th, t.g_y+th,
                    t.g_w-(2*th), t.g_h-(2*th));
            redraw = FALSE;
        }
#endif
    }

    if (redraw)
        ob_render_object(tree, obj, t.g_x, t.g_y);

    gsx_mon();
}

#if OB_RENDER_3D
/*
 *  Initialise variables related to 3D objects
 */
void init_3d(void)
{
    indtxtmove = 0;         /* indicators: don't move text */
    indcolchange = 1;       /* ... but change colour       */

    acttxtmove = 1;         /* activators: move text       */
    actcolchange = 0;       /* ... but don't change colour */

    /* initialise button colours according to size of palette */
    if (gl_ws.ws_ncolors <= LWHITE)
    {
        actbutcol = indbutcol = backgrcol = WHITE;
    }
    else
    {
        actbutcol = indbutcol = backgrcol = LWHITE;
    }
}


/*
 *  Implements objc_sysvar(): set/get variables to control 3D object display
 */
WORD ob_sysvar(WORD mode, WORD which, WORD in1, WORD in2, WORD *out1, WORD *out2)
{
    /*
     * handle setting the variables
     */
    if (mode)
    {
        switch(which) {
        case LK3DIND:
            if (in1 != -1)
                indtxtmove = in1;
            if (in2 != -1)
                indcolchange = in2;
            break;
        case LK3DACT:
            if (in1 != -1)
                acttxtmove = in1;
            if (in2 != -1)
                actcolchange = in2;
            break;
        case INDBUTCOL:
            if (in1 >= gl_ws.ws_ncolors)
                return 0;
            indbutcol = in1;
            break;
        case ACTBUTCOL:
            if (in1 >= gl_ws.ws_ncolors)
                return 0;
            actbutcol = in1;
            break;
        case BACKGRCOL:
            if (in1 >= gl_ws.ws_ncolors)
                return 0;
            backgrcol = in1;
            break;
        default:
            return 0;
            break;
        }
        return 1;
    }

    /*
     * handle getting the variables
     */
    switch(which) {
    case LK3DIND:
        *out1 = indtxtmove;
        *out2 = indcolchange;
        break;
    case LK3DACT:
        *out1 = acttxtmove;
        *out2 = actcolchange;
        break;
    case INDBUTCOL:
        *out1 = indbutcol;
        break;
    case ACTBUTCOL:
        *out1 = actbutcol;
        break;
    case BACKGRCOL:
        *out1 = backgrcol;
        break;
    case AD3DVALUE:
        *out1 = ADJ3DSTD;
        *out2 = ADJ3DSTD;
        break;
    default:
        return 0;
        break;
    }

    return 1;
}
#endif

