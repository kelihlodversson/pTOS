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

#include "vdi_defs.h"
#include "vdi_textblit.h"
#include "kprint.h"

void normal_blit(LOCALVARS *vars, UBYTE *src, UBYTE *dst)
{
    int x,y;
    // The caller passes a pointer to the end of the vars structure as that how
    // the original assembler version consumes it, so we simply subtract one.
    vars--;
    WORD dest_bitoffset = vars->tddad;
    WORD src_bitoffset = vars->tsdad;
#if CONF_CHUNKY_PIXELS
    UBYTE tmp;
    if (vars->nbrplane == 8 && vars->nextwrd == sizeof(WORD))
    {
        if (src_bitoffset > 7) // We treat the source as a sequence of (big endian) bytes instead of words
        {
            src++;
            src_bitoffset-= 8;
        }

        for( y = 0; y < vars->height; y++ )
        {
            UBYTE src_bit_mask = 0x80 >> src_bitoffset;
            UBYTE* lsrc = src;
            UBYTE c = *lsrc;
            for( x = 0; x < vars->width; x++ )
            {
                tmp = (c & src_bit_mask)?vars->forecol:vars->ambient;
                switch (vars->WRT_MODE) {
                    default:
                    case 0: /* REPLACE */
                    dst[x] = tmp;
                    break;
                    case 1: /* TRANS */
                    if (c & src_bit_mask)
                    {
                        dst[x] = tmp;
                    }
                    break;
                    case 2: /* XOR */
                    dst[x] ^= tmp;
                    break;
                    case 3: /* INVERS */
                    if (!(c & src_bit_mask))
                    {
                        dst[x] = tmp;
                    }
                    break;
                }
                src_bit_mask >>= 1;
                if(src_bit_mask == 0)
                {
                    src_bit_mask = 0x80;
                    c = *(++lsrc);
                }
            }
            src += vars->s_next;
            dst += vars->d_next;
        }
    }
    else
#endif
    {
        // TODO inplement bitplane blit here
    }
}
