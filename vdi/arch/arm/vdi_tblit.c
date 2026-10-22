/*
 * vdi_tblit_c.c - Blitting of Text -- portable C implementation
 *
 * Copyright (C) 1999 Caldera, Inc. and Authors:
 *               1984 Dave Staugas
 *               2002-2017 The EmuTOS development team
 *               2018 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"

#include "asm.h"
#include "lineavars.h"
#include "vdi_defs.h"
#include "vdi_textblit.h"
#include "kprint.h"

/*
 * source and destination buffers are sequences of (big endian) words,
 * with bit 15 the leftmost pixel; word number n covers pixel 16n..16n+15.
 */
static UWORD get_src_word(const UBYTE *p)
{
    return (UWORD)(((UWORD)p[0] << 8) | (UWORD)p[1]);
}

static void put_dst_word(UBYTE *p, UWORD w)
{
    p[0] = (UBYTE)(w >> 8);
    p[1] = (UBYTE)(w & 0xff);
}

/*
 * The destination is the text buffer pre_blit() fills, which is a sequence
 * of big endian words like the glyph forms, or the screen, whose words are
 * the CPU's own (as the fills, lines and v_get_pixel() have them, and the
 * console's byte-wide characters, see direct_screen_blit()).
 */
static BOOL in_text_buffer(const UBYTE *dst)
{
    const UBYTE *buf = (const UBYTE *)linea_vars.SCRTCHP;

    return dst >= buf && dst < buf + 2L * (UWORD)linea_vars.SCRPT2;
}

void normal_blit(LOCALVARS *vars, UBYTE *src, UBYTE *dst)
{
    int x,y;
    int xstart, xend;
    // The caller passes a pointer to the end of the vars structure as that how
    // the original assembler version consumes it, so we simply subtract one.
    vars--;
    /*
     * The destination is the 1-plane buffer set up by pre_blit(), or the
     * screen, of one plane to eight: each pixel is in one word per plane,
     * the words of a pixel group following each other (vars->nextwrd bytes
     * for all the planes of 16 pixels), and the colour of the text is
     * one bit of vars->forecol per plane.
     */
    if (vars->nbrplane >= 1 && vars->nbrplane <= 8)
        {
            BOOL to_buffer = in_text_buffer(dst);
            /*
             * F_SKEW shifts each row right by one more pixel whenever a
             * bit of skew_msk rotates out of the top (carry) position,
             * giving the glyph a slanted look; F_THICKEN repeats every
             * set source pixel over vars->smear consecutive destination
             * pixels.  Both effects are handled per destination column,
             * so an overlapping later source pixel cannot erase an
             * earlier one in replace mode (single mask write per column).
             */
            WORD skew = (vars->STYLE & F_SKEW) != 0;
            WORD shift = 0;
            WORD smear = (vars->STYLE & F_THICKEN) ? vars->smear + 1 : 1;   /* the pixel and what it adds */
            WORD glyphwidth;
            WORD tstad = vars->tsdad;
            WORD tddad = vars->tddad;
            WORD planes = vars->nbrplane;
            UWORD skew_msk = (vars->STYLE & F_SKEW) ? (UWORD)linea_vars.SKEWMASK : (UWORD)0x8000;
            /* light text keeps the bits of the mask, from the bottom line up, turning one a line */
            UWORD lite = (vars->STYLE & F_LIGHT) ? (UWORD)linea_vars.LITEMASK : (UWORD)0xffff;

            /*
             * The part of the source that is thickened: the cell less what
             * thickening adds, as the m68k blit masks it (thk_msk) and
             * thicken_glyph() and the reference model of text_blit do.  In
             * a font whose characters are not all as wide that is the
             * glyph; in a fixed-pitch one, whose cell stays, the last
             * columns of the glyph go.
             */
            glyphwidth = vars->width;
            if (vars->STYLE & F_THICKEN)
                glyphwidth -= vars->smear;

            for (y = 0; y < vars->height; y++)
            {
                const UBYTE *srow = src + y * vars->s_next;
                UBYTE *drow = dst + y * vars->d_next;
                UWORD rowlite = lite;

                lite = (UWORD)((lite << 1) | (lite >> 15));

                if (skew)
                {
                    if (skew_msk & 0x8000)
                        shift++;
                    rolw1(skew_msk);
                }

                /*
                 * Skewed, a row of the glyph on the screen moves as a whole
                 * (the cell is DELX wide, text_blt() has made room for the
                 * skew in the clipping): the pixels it leaves are not
                 * touched.  The text buffer, which pre_blit() has made wide
                 * enough for the skew, is filled all across.
                 */
                xstart = to_buffer ? 0 : shift;
                xend = to_buffer ? vars->DELX : vars->DELX + shift;
                for (x = xstart; x < xend; x++)
                {
                    WORD dx = (WORD)(x + tddad);
                    UWORD dmask = (UWORD)(0x8000 >> (dx & 15));
                    UBYTE *dgroup = drow + (dx >> 4) * vars->nextwrd;
                    BOOL set = FALSE;
                    int k, p;

                    for (k = 0; k < smear; k++)
                    {
                        WORD sx = (WORD)(x + tstad - shift - k);
                        if (sx < tstad || sx >= tstad + glyphwidth)
                            continue;
                        if (get_src_word(srow + ((sx >> 4) << 1)) & (UWORD)(0x8000 >> (sx & 15)))
                        {
                            set = TRUE;
                            break;
                        }
                    }

                    if (set && !(rowlite & dmask))
                        set = FALSE;            /* the effect of light text */

                    for (p = 0; p < planes; p++)
                    {
                        UBYTE *dp = dgroup + (p << 1);
                        UWORD dw = to_buffer ? get_src_word(dp) : *(UWORD *)dp;
                        BOOL fore = (vars->forecol >> p) & 1;
                        BOOL back = (vars->ambient >> p) & 1;

                        switch (vars->WRT_MODE) {
                        default:
                        case 0: /* REPLACE */
                            dw &= (UWORD)~dmask;
                            if (set ? fore : back)
                                dw |= dmask;
                            break;
                        case 1: /* TRANS */
                            if (set)
                            {
                                dw &= (UWORD)~dmask;
                                if (fore)
                                    dw |= dmask;
                            }
                            break;
                        case 2: /* XOR */
                            if (set)
                                dw ^= dmask;
                            break;
                        case 3: /* INVERS */
                            if (!set)
                            {
                                dw &= (UWORD)~dmask;
                                if (fore)
                                    dw |= dmask;
                            }
                            break;
                        }
                        if (to_buffer)
                            put_dst_word(dp, dw);
                        else
                            *(UWORD *)dp = dw;
                    }
                }
            }
        }
}
