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

void normal_blit(LOCALVARS *vars, UBYTE *src, UBYTE *dst)
{
    int x,y;
    // The caller passes a pointer to the end of the vars structure as that how
    // the original assembler version consumes it, so we simply subtract one.
    vars--;
    if (vars->nbrplane == 1 && vars->nextwrd == 2)
        {
            /*
             * 1-plane buffer blit, as set up by pre_blit().
             *
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
            WORD smear = (vars->STYLE & F_THICKEN) ? vars->smear : 1;
            WORD glyphwidth = (vars->STYLE & F_THICKEN) ? vars->width - vars->smear : vars->width;
            WORD tstad = vars->tsdad;
            WORD tddad = vars->tddad;
            UWORD skew_msk = (vars->STYLE & F_SKEW) ? (UWORD)linea_vars.SKEWMASK : (UWORD)0x8000;

            for (y = 0; y < vars->height; y++)
            {
                const UBYTE *srow = src + y * vars->s_next;
                UBYTE *drow = dst + y * vars->d_next;

                if (skew)
                {
                    if (skew_msk & 0x8000)
                        shift++;
                    rolw1(skew_msk);
                }

                for (x = 0; x < vars->DELX; x++)
                {
                    WORD dx = (WORD)(x + tddad);
                    UWORD dw = get_src_word(drow + ((dx >> 4) << 1));
                    UWORD dmask = (UWORD)(0x8000 >> (dx & 15));
                    BOOL set = FALSE;
                    int k;

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

                    switch (vars->WRT_MODE) {
                    default:
                    case 0: /* REPLACE */
                        dw &= (UWORD)~dmask;
                        if (set)
                            dw |= (vars->forecol) ? dmask : 0;
                        else
                            dw |= (vars->ambient) ? dmask : 0;
                        break;
                    case 1: /* TRANS */
                        if (set)
                        {
                            dw &= (UWORD)~dmask;
                            dw |= (vars->forecol) ? dmask : 0;
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
                            dw |= (vars->forecol) ? dmask : 0;
                        }
                        break;
                    }
                    put_dst_word(drow + ((dx >> 4) << 1), dw);
                }
            }
        }
        else
        {
            // TODO implement other bitplane blits here
        }
}
