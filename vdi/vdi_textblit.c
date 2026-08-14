/*
 * vdi_textblit.c - the text_blt() mainline code
 *
 * Copyright (C) 2017-2019 The EmuTOS development team
 *
 * Authors:
 *  RFB   Roger Burrows
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/* #define ENABLE_KDEBUG */

#include "config.h"
#include "portab.h"
#include "intmath.h"
#include "asm.h"

#include "../bios/tosvars.h"
#include "vdi_defs.h"
#include "vdi_textblit.h"
#include "vdi_backend.h"
#include "../bios/lineavars.h"
#include "kprint.h"


/*
 * the text scratch buffer, used by the rotation/outline/scale helpers
 * (see the SCRATCHBUF_* sizing macros in vdi_defs.h)
 *
 * this replaces the assembler _deftxbuf/_scrtsiz definitions, which
 * used a different, smaller split of the same region
 */
const WORD scrtsiz = SCRATCHBUF_OFFSET;
WORD deftxbuf[SCRATCHBUF_SIZE/sizeof(WORD)];


/*
 * the following table maps a 4-bit sequence to its reverse
 */
const static UBYTE reverse_nybble[] =
/*  0000  0001  0010  0011  0100  0101  0110  0111  */
{   0x00, 0x08, 0x04, 0x0c, 0x02, 0x0a, 0x06, 0x0e,
/*  1000  1001  1010  1011  1100  1101  1110  1111  */
    0x01, 0x09, 0x05, 0x0d, 0x03, 0x0b, 0x07, 0x0f };


/*
 * check for clipping
 *
 * returns  1 clipping required
 *          0 no clipping
 *         -1 entirely clipped, nothing to output
 */
static WORD check_clip(LOCALVARS *vars, WORD delx, WORD dely)
{
    WORD rc;

    if (!linea_vars.CLIP)
        return 0;

    rc = 0;

    /*
     * check x coordinate
     */
    if (vars->DESTX < linea_vars.XMINCL)        /* (partially) left of clip window */
    {
        if (vars->DESTX+delx <= linea_vars.XMINCL)   /* wholly left of clip window */
            return -1;
        rc = 1;
    }
    if (vars->DESTX > linea_vars.XMAXCL)        /* wholly right of clip window */
        return -1;
    if (vars->DESTX+delx > linea_vars.XMAXCL)   /* partially right of clip window */
        rc = 1;

    /*
     * check y coordinate
     */
    if (vars->DESTY < linea_vars.YMINCL)        /* (partially) below clip window */
    {
        if (vars->DESTY+dely <= linea_vars.YMINCL)   /* wholly below clip window */
            return -1;
        rc = 1;
    }
    if (vars->DESTY > linea_vars.YMAXCL)        /* wholly above clip window */
        return -1;
    if (vars->DESTY+dely > linea_vars.YMAXCL)   /* partially above clip window */
        rc = 1;

    return rc;
}


/*
 * do the actual clipping
 *
 * returns  0 OK
 *         -1 entirely clipped, nothing to output.  I believe this can
 *            only happen if rotation or scaling has been done
 */
static WORD do_clip(LOCALVARS *vars)
{
    WORD n;

    /*
     * if clipping not requested, exit
     */
    if (!linea_vars.CLIP)
        return 0;

    /*
     * clip x minimum if necessary
     */
    if (vars->DESTX < linea_vars.XMINCL)
    {
        n = vars->DESTX + vars->DELX - linea_vars.XMINCL;
        if (n <= 0)
            return -1;
        linea_vars.SOURCEX += vars->DELX - n;
        vars->DELX = n;
        vars->DESTX = linea_vars.XMINCL;
    }

    /*
     * clip x maximum if necessary
     */
    if (vars->DESTX > linea_vars.XMAXCL)
        return -1;
    n = vars->DESTX + vars->DELX - linea_vars.XMAXCL - 1;
    if (n > 0)
        vars->DELX -= n;

    /*
     * clip y minimum if necessary
     */
    if (vars->DESTY < linea_vars.YMINCL)
    {
        n = vars->DESTY + vars->DELY - linea_vars.YMINCL;
        if (n <= 0)
            return -1;
        linea_vars.SOURCEY += vars->DELY - n;
        vars->DELY = n;
        vars->DESTY = linea_vars.YMINCL;
    }

    /*
     * clip y maximum if necessary
     */
    if (vars->DESTY > linea_vars.YMAXCL)
        return -1;
    n = vars->DESTY + vars->DELY - linea_vars.YMAXCL - 1;
    if (n > 0)
        vars->DELY -= n;

    return 0;
}


/*
 * return a ULONG equal to the 3 high-order bytes pointed to by
 * the input pointer, concatenated with the low-order byte of
 * the input UWORD
 *
 * this is the 2019 EmuTOS version, but written to be endian-neutral:
 * on big-endian targets it is exactly equivalent to the original
 * `*(ULONG *)p` union trick (and equally fast), and it works on
 * little-endian ARM too.  It also avoids the unaligned ULONG load,
 * which would fault on ARM.
 */
/*
 * the scratch buffer used for outlined/skewed text is a sequence of
 * (big endian) words with bit 15 the leftmost pixel, the same layout as
 * the screen itself.  It is produced and consumed by normal_blit() and
 * the truecolor backend through byte-ordered accessors, so outline()
 * must not dereference UWORD* pointers directly on little-endian
 * machines: the accessors below are endian-neutral (a no-op on m68k).
 */
static UWORD get_buf_word(const UWORD *p)
{
    const UBYTE *b = (const UBYTE *)p;
    return (UWORD)(((UWORD)b[0] << 8) | (UWORD)b[1]);
}

static void put_buf_word(UWORD *p, UWORD w)
{
    UBYTE *b = (UBYTE *)p;
    b[0] = (UBYTE)(w >> 8);
    b[1] = (UBYTE)(w & 0xff);
}

static ULONG merge_byte(UWORD *p, UWORD n)
{
    return ((ULONG)get_buf_word(p) << 16) | ((ULONG)(get_buf_word(p + 1) >> 8) << 8) | (n & 0xFF);
}


/*
 * outline: perform text outlining
 *
 * in the following code, the top 18 bits of unsigned longs are used
 * to manage 18-bit values, consisting of the last bit of the previous
 * screen word, the 16 bits of the current screen word, and the first
 * bit of the next screen word.
 *
 * neighbours are numbered as follows:
 *              1 2 3
 *              7 X 8
 *              4 5 6
 *
 * the scratch buffer holds (big endian) words with bit 15 as the leftmost
 * pixel, so it must be accessed through get_buf_word()/put_buf_word()
 * rather than by dereferencing UWORD* directly on little-endian machines.
 */
void outline(LOCALVARS *vars)
{
    UWORD *currline, *nextline, *scratch;
    UWORD *save_next;
    UWORD curr, prev, tmp;
    WORD i, j, form_width;
    ULONG current_left, current_noshift, current_right;
    ULONG top_left, top_noshift, top_right;
    ULONG bottom_left, bottom_noshift, bottom_right;
    ULONG result;

    form_width = vars->s_next / sizeof(WORD);
    currline = (UWORD *)vars->sform + form_width;
    nextline = currline + form_width;

    /* process lines sequentially */
    for (i = vars->DELY; i > 0; i--)
    {
        save_next = nextline;
        prev = 0;
        curr = 0;
        /* endian-neutral (and alignment-safe) form of `(*(ULONG *)nextline) >> 1` */
        bottom_left = (((ULONG)get_buf_word(nextline) << 16) | (ULONG)get_buf_word(nextline + 1)) >> 1;

        /* process one word at a time */
        for (j = form_width, scratch = (UWORD *)vars->sform; j > 0; j--)
        {
            /* get the data from the current line */
            current_left = current_noshift = merge_byte(currline, curr);
            rorl(current_noshift, 1);
            current_right = current_noshift;
            rorl(current_right, 1);

            /*
             * get the data from the scratch buffer (the previous line)
             * and merge into result
             */
            top_right = merge_byte(scratch, prev);
            rorl(top_right, 1);
            top_left = top_noshift = top_right;
            top_left ^= current_left;           /* neighbours 1,2,3 */
            top_noshift ^= current_noshift;
            top_right ^= current_right;
            roll(top_noshift, 1);
            roll(top_right, 2);
            result = (top_left | top_noshift | top_right);

            /* get the data from the next line & merge into result */
            bottom_right = bottom_noshift = bottom_left;
            bottom_left ^= current_left;        /* neighbours 4,5,6 */
            bottom_noshift ^= current_noshift;
            bottom_right ^= current_right;
            roll(bottom_noshift, 1);
            roll(bottom_right, 2);
            result |= (bottom_left | bottom_noshift | bottom_right);

            /* finally, merge current line neighbours */
            current_left ^= current_noshift;    /* neighbours 7,8 */
            current_right ^= current_noshift;
            roll(current_right, 2);
            result |= (current_left | current_right);
            result >>= 16;                      /* move to lower 16 bits */

            prev = curr = get_buf_word(currline);
            prev = (prev ^ result) & result;
            put_buf_word(currline, prev);
            currline++;
            prev = get_buf_word(scratch);
            put_buf_word(scratch, curr);
            scratch++;

            tmp = get_buf_word(nextline);
            nextline++;
            bottom_left = merge_byte(nextline, tmp);
            rorl(bottom_left, 1);
        }

        nextline = save_next;
        currline = nextline;

        if (i > 2)                              /* mustn't go past end */
            nextline += form_width;
    }
}


/*
 * copy the character into a buffer so that we can
 * outline/rotate/scale it
 */
static void pre_blit(LOCALVARS *vars)
{
    WORD weight, skew, size, n, tmp_style;
    WORD dest_width, dest_height;
    WORD *p;
    LONG offset;
    UBYTE *src, *dst;

    vars->height = vars->DELY;

    vars->tsdad = linea_vars.SOURCEX & 0x000f;     /* source dot address */
    offset = (linea_vars.SOURCEY+vars->DELY-1) * (LONG)vars->s_next + ((linea_vars.SOURCEX >> 3) & ~1);
    src = vars->sform + offset;         /* bottom of font char source */
    vars->s_next = -vars->s_next;       /* we draw from the bottom up */

    weight = linea_vars.WEIGHT;
    skew = linea_vars.LOFF + linea_vars.ROFF;

    dest_width = vars->DELX;
    if (vars->STYLE & F_THICKEN)
    {
        dest_width += weight;
        vars->smear = weight;
    }

    /*
     * handle outlining
     */
    vars->tddad = 0;
    dest_height = vars->DELY;
    if (vars->STYLE & F_OUTLINE)
    {
        dest_width += 3;        /* add 1 left & 2 right pixels */
        vars->tddad += 1;       /* and make leftmost column blank */
        vars->DELY += 2;        /* add 2 rows */
        dest_height += 3;       /* add 3 rows for buffer clear */
    }
    vars->width = dest_width;
    dest_width += skew;
    vars->DELX = dest_width;
    dest_width = ((dest_width >> 4) << 1) + 2;    /* in bytes */
    vars->d_next = -dest_width;
    size = dest_width * (dest_height - 1);
    vars->buffa = linea_vars.SCRPT2 - vars->buffa; /* switch buffers */
    dst = (UBYTE *)linea_vars.SCRTCHP + vars->buffa;
    vars->sform = dst;
    if (vars->STYLE & (F_OUTLINE|F_SKEW))
    {
        p = (WORD *)vars->sform;
        n = (size - vars->d_next) / 2;  /* add bottom line */
        while(n--)
            *p++ = 0;               /* clear buffer */
        if (vars->STYLE & F_OUTLINE)
        {
            vars->width -= 3;
            vars->DELX -= 1;
            size += vars->d_next;
        }
    }

    /* label no_clear: */

    dst += size;                     /* start at the bottom */
    vars->WRT_MODE = 0;
    vars->forecol = 1;
    vars->ambient = 0;
    vars->nbrplane = 1;             /* 1 plane for this blit */
    vars->nextwrd = 2;
    tmp_style = vars->STYLE;        /* save temporarily */
    vars->STYLE &= (F_SKEW|F_THICKEN);  /* only thicken, skew */

    normal_blit(vars+1, src, dst);  /* call assembler helper function */

    vars->STYLE = tmp_style;        /* restore */
    vars->WRT_MODE = linea_vars.WRT_MODE;
    vars->s_next = -vars->d_next;   /* reset the source to the buffer */

    if (vars->STYLE & F_OUTLINE)
    {
        outline(vars);
        vars->sform += vars->s_next;
    }

    linea_vars.SOURCEX = 0;
    linea_vars.SOURCEY = 0;
    vars->STYLE &= ~(F_SKEW|F_THICKEN); /* cancel effects */
}


/*
 * rotate: perform text rotation
 */
void rotate(LOCALVARS *vars)
{
    UBYTE *src, *dst;
    WORD form_width, i, j, k, tmp;
    UWORD in, out, srcbit, dstbit;
    UWORD *p, *q;

    vars->tsdad = linea_vars.SOURCEX & 0x000f;
    src = vars->sform + ((linea_vars.SOURCEX >> 4) << 1);
    vars->buffa = linea_vars.SCRPT2 - vars->buffa;     /* switch buffers */
    dst = (UBYTE *)linea_vars.SCRTCHP + vars->buffa;

    vars->width = vars->DELX;
    vars->height = vars->DELY;

    /*
     * first, handle the simplest case: inverted text (180 rotation)
     */
    if (linea_vars.CHUP == 1800)
    {
        form_width = ((vars->DELX+vars->tsdad-1) >> 4) + 1; /* in words */
        vars->d_next = form_width * sizeof(WORD);
        q = (UWORD *)(dst + vars->d_next * vars->height);
        for (i = vars->height; i > 0; i--)
        {
            for (j = form_width, p = (UWORD *)src; j > 0; j--)
            {
                in = *p++;
                out = reverse_nybble[in&0x000f];    /* reverse 4 bits at a time */
                for (k = 3; k > 0; k--)
                {
                    out <<= 4;
                    in >>= 4;
                    out |= reverse_nybble[in&0x000f];
                }
                *--q = out;
            }
            src += vars->s_next;
        }
        vars->s_next = vars->d_next;
        vars->sform = (UBYTE *)linea_vars.SCRTCHP + vars->buffa;
        linea_vars.SOURCEX = -(linea_vars.SOURCEX + vars->DELX) & 0x000f;
        linea_vars.SOURCEY = 0;
        return;
    }

    /*
     * handle remaining cases (90 and 270 rotation)
     */
    vars->d_next = ((vars->DELY >> 4) << 1) + 2;

    if (linea_vars.CHUP == 900)
    {
        dst += (vars->DELX - 1) * vars->d_next;
        vars->d_next = -vars->d_next;
    }
    else        /* 2700 */
    {
        src += (linea_vars.SOURCEY + vars->height - 1) * vars->s_next;
        vars->s_next = -vars->s_next;
    }

    srcbit = 0x8000 >> vars->tsdad;
    dstbit = 0x8000;
    out = 0;
    p = (UWORD *)src;
    q = (UWORD *)dst;
    for (i = vars->width; i > 0; i--)
    {
        for (j = vars->height; j > 0; j--)
        {
            if (*p & srcbit)
                out |= dstbit;
            dstbit >>= 1;
            if (!dstbit)
            {
                dstbit = 0x8000;
                *q++ = out;
                out = 0;
            }
            p = (UWORD *)((UBYTE *)p + vars->s_next);
        }

        dstbit = 0x8000;
        *q = out;
        out = 0;
        dst += vars->d_next;
        q = (UWORD *)dst;

        srcbit >>= 1;
        if (!srcbit)
        {
            srcbit = 0x8000;
            src += sizeof(WORD);
        }

        p = (UWORD *)src;
    }

    vars->height = vars->DELX;  /* swap width & height */
    vars->width = vars->DELY;
    vars->DELX = vars->width;
    vars->DELY = vars->height;
    tmp = vars->tmp_delx;
    vars->tmp_delx = vars->tmp_dely;
    vars->tmp_dely = tmp;
    vars->swap_tmps = 1;

    vars->s_next = (linea_vars.CHUP == 900) ? -vars->d_next : vars->d_next;
    vars->sform = (UBYTE *)linea_vars.SCRTCHP + vars->buffa;
    linea_vars.SOURCEX = 0;
    linea_vars.SOURCEY = 0;
}


/*
 * inline function to clarify horizontal scaling code
 */
static __inline__ UWORD *shift_and_update(UWORD *dst, UWORD *dstbit, UWORD *out)
{
    *dstbit >>= 1;
    if (!*dstbit)           /* end of word ? */
    {
        *dstbit = 0x8000;   /* reset test bit */
        *dst++ = *out;      /* output accumulated word */
        *out = 0;           /* & reset it */
    }

    return dst;
}


/*
 * scaleup: increase width of character (SCALDIR is 1)
 */
static void scaleup(LOCALVARS *vars, UWORD *src, UWORD *dst)
{
    UWORD srcbit, dstbit;
    UWORD accum, in, out;
    WORD i;

    srcbit = 0x8000 >> vars->tsdad;
    dstbit = 0x8000;

    out = 0;
    accum = linea_vars.XDDA;
    in = *src++;        /* prime the source word */

    for (i = vars->width; i > 0; i--)
    {
        if (in & srcbit)            /* handle bit set in source */
        {
            accum += linea_vars.DDAINC;
            if (accum < linea_vars.DDAINC)
            {
                out |= dstbit;
                dst = shift_and_update(dst, &dstbit, &out);
            }
            out |= dstbit;
            dst = shift_and_update(dst, &dstbit, &out);
        }
        else                        /* handle bit clear in source */
        {
            accum += linea_vars.DDAINC;
            if (accum < linea_vars.DDAINC)
            {
                dst = shift_and_update(dst, &dstbit, &out);
            }
            dst = shift_and_update(dst, &dstbit, &out);
        }

        srcbit >>= 1;
        if (!srcbit)
        {
            srcbit = 0x8000;
            in = *src++;
        }
    }

    *dst = out;
}


/*
 * scaledown: decrease width of character (SCALDIR is 0)
 */
static void scaledown(LOCALVARS *vars, UWORD *src, UWORD *dst)
{
    UWORD srcbit, dstbit;
    UWORD accum, in, out;
    WORD i;

    srcbit = 0x8000 >> vars->tsdad;
    dstbit = 0x8000;

    out = 0;
    accum = linea_vars.XDDA;
    in = *src++;        /* prime the source word */

    for (i = vars->width; i > 0; i--)
    {
        if (in & srcbit)            /* handle bit set in source */
        {
            accum += linea_vars.DDAINC;
            if (accum < linea_vars.DDAINC)
            {
                out |= dstbit;
                dst = shift_and_update(dst, &dstbit, &out);
            }
        }
        else                        /* handle bit clear in source */
        {
            dst = shift_and_update(dst, &dstbit, &out);
        }

        srcbit >>= 1;
        if (!srcbit)
        {
            srcbit = 0x8000;
            in = *src++;
        }
    }

    *dst = out;
}


/*
 * scale: perform text scaling
 */
void scale(LOCALVARS *vars)
{
    UBYTE *src, *dst;
    WORD i, delx;
    UWORD accum;

    vars->tsdad = linea_vars.SOURCEX & 0x000f;
    src = vars->sform + ((linea_vars.SOURCEX >> 4) << 1) + (linea_vars.SOURCEY * vars->s_next);

    vars->buffa = linea_vars.SCRPT2 - vars->buffa;     /* switch buffers */
    dst = (UBYTE *)linea_vars.SCRTCHP + vars->buffa;

    vars->width = vars->DELX;
    vars->height = vars->DELY;

    vars->d_next = ((vars->width >> 3) << 1) + 2;

    /*
     * first, scale the character
     */
    accum = 0x7fff;
    if (linea_vars.SCALDIR)        /* scale up */
    {
        for (i = vars->height; i > 0; i--)
        {
            accum += linea_vars.DDAINC;
            if (accum < linea_vars.DDAINC)
            {
                scaleup(vars, (UWORD *)src, (UWORD *)dst);
                dst += vars->d_next;
            }
            scaleup(vars, (UWORD *)src, (UWORD *)dst);
            dst += vars->d_next;
            src += vars->s_next;
        }
    }
    else                /* scale down */
    {
        for (i = vars->height; i > 0; i--)
        {
            accum += linea_vars.DDAINC;
            if (accum < linea_vars.DDAINC)
            {
                scaledown(vars, (UWORD *)src, (UWORD *)dst);
                dst += vars->d_next;
            }
            src += vars->s_next;
        }
    }

    /*
     * then, adjust the character spacing
     */
    accum = linea_vars.XDDA;
    delx = linea_vars.SCALDIR ? vars->DELX : 0;
    for (i = vars->DELX; i > 0; i--)
    {
        accum += linea_vars.DDAINC;
        if (accum < linea_vars.DDAINC)
            delx++;
    }
    linea_vars.XDDA = accum;

    vars->DELX = delx;
    vars->DELY = vars->tmp_dely;
    vars->s_next = vars->d_next;
    vars->sform = (UBYTE *)linea_vars.SCRTCHP + vars->buffa;
    linea_vars.SOURCEX = 0;
    linea_vars.SOURCEY = 0;
}


/*
 * output a block to the screen
 */
static void screen_blit(LOCALVARS *vars)
{
    LONG offset;

    vars->forecol = linea_vars.TEXTFG;
    vars->ambient = 0;          /* logically TEXTBG, but that isn't set up by the VDI */
    vars->nbrplane = linea_vars.v_planes;
    vars->nextwrd = vars->nbrplane * sizeof(WORD);
    vars->height = vars->DELY;
    vars->width = vars->DELX;

    /*
     * calculate the starting address for the character to be copied
     */
    vars->tsdad = linea_vars.SOURCEX & 0x000f; /* source dot address */
    offset = (linea_vars.SOURCEY+vars->DELY-1) * (LONG)vars->s_next + ((linea_vars.SOURCEX >> 3) & ~1);
    vars->sform += offset;
    vars->s_next = -vars->s_next;   /* we draw from the bottom up */

#if CONF_WITH_VDI_BACKEND_DISPATCH
    {
        const vdi_backend_ops *backend = vdi_screen_backend();

        /* see the comment in get_start_addr() (vdi_misc.c) */
        if (backend)
            backend->text_blit(vars);
    }
#elif CONF_WITH_VDI_BACKEND_TRUECOLOR
    /* see the comment in get_start_addr() (vdi_misc.c) */
    truecolor_text_blit(vars);
#else
    planar_text_blit(vars);
#endif
}

/*
 * planar text blit: output the current glyph to a bitplane screen
 *
 * this is the historical path; the packed-truecolor backend has its own
 * version (see vdi_backend_truecolor.c)
 */
void planar_text_blit(LOCALVARS *vars)
{
    vars->tddad = vars->DESTX & 0x000f;
    vars->dform = v_bas_ad;
    vars->dform += (vars->DESTX&0xfff0)>>shift_offset[linea_vars.v_planes];
    vars->dform += (UWORD)(vars->DESTY+vars->DELY-1) * (ULONG)linea_vars.v_lin_wr;
    vars->d_next = -linea_vars.v_lin_wr;

    normal_blit(vars+1, vars->sform, vars->dform);  /* call assembler helper function */
}


/*
 * resize characters for lineA
 *
 * this is similar to act_siz(), but note that act_siz() always starts
 * with the same value in the accumulator, while here the initial value
 * is passed as an argument.
 *
 * this allows us to use the same routine for scaling height (which is
 * constant for a given string) and width (which can vary since we do a
 * kind of nano-justification).
 *
 * entry:
 *  init        initial value of accumulator
 *  size        value to scale
 *
 * used variables:
 *  DDAINC      DDA increment passed externally
 *  SCALDIR     0 if scale down, 1 if enlarge
 *
 * exit:
 *  new size
 */
static UWORD char_resize(UWORD init, UWORD size)
{
    UWORD accu, retval, i;

    if (linea_vars.DDAINC == 0xffff) {     /* double size */
        return (size<<1);
    }
    accu = init;
    retval = linea_vars.SCALDIR ? size : 0;

    for (i = 0; i < size; i++) {
        accu += linea_vars.DDAINC;
        if (accu < linea_vars.DDAINC)
            retval++;
    }

    /* if input is non-zero, make return value at least 1 */
    if (size && !retval)
        retval = 1;

    return retval;
}


void text_blt(void)
{
    LOCALVARS vars;
    WORD clipped, delx, dely, weight;
    WORD temp;
    BOOL need_preblit = FALSE;

    vars.swap_tmps = 0;

    /*
     * make local copies, just as the original code does
     */
    vars.STYLE = linea_vars.STYLE;
    vars.WRT_MODE = linea_vars.WRT_MODE;
    vars.DELX = linea_vars.DELX;
    vars.DESTX = linea_vars.DESTX;
    vars.DELY = linea_vars.DELY;
    vars.DESTY = linea_vars.DESTY;

    vars.buffa = 0;
    dely = vars.DELY;
    delx = vars.DELX;

    if (linea_vars.SCALE)
    {
        vars.tmp_dely = dely = char_resize(0x7fff, vars.DELY);
        vars.tmp_delx = delx = char_resize(linea_vars.XDDA, vars.DELX);
    }

    vars.smear = 0;
    if (vars.STYLE & F_THICKEN)
    {
        weight = linea_vars.WEIGHT;
        if (weight == 0)        /* cancel thicken if no weight */
            vars.STYLE &= ~F_THICKEN;
        if (!linea_vars.MONO)
            delx += weight;
    }

    if (vars.STYLE & F_SKEW)
    {
        delx += linea_vars.LOFF + linea_vars.ROFF;
    }

    if (vars.STYLE & F_OUTLINE)
    {
        delx += OUTLINE_THICKNESS * 2;
        dely += OUTLINE_THICKNESS * 2;
    }

    switch(linea_vars.CHUP) {
    case 900:
        vars.DESTY -= delx;
        FALLTHROUGH;
    case 2700:
        temp = delx;        /* swap delx/dely for 90 or 270 */
        delx = dely;
        dely = temp;
        break;
    case 1800:
        vars.DESTX -= delx;
    }

    clipped = check_clip(&vars, delx, dely);
    if (clipped < 0)
        goto upda_dst;

    vars.dest_wrd = 0;
    vars.s_next = linea_vars.FWIDTH;
    vars.sform = (UBYTE *)linea_vars.FBASE;

    if (linea_vars.SCALE)
    {
        scale(&vars);
    }

    /*
     * decide if we need to copy the source glyph to a temporary buffer
     * so we can manipulate it before the actual screen blit
     *
     * we copy in the following situations:
     *  (in truecolor mode) if (skewing OR thickening OR outlining), OR
     *  if outlining, OR
     *     rotating AND (skewing OR thickening), OR
     *     skewing AND clipping-is-required,
     *      call pre_blit()
     *
     * Audited under issue #171: this is the text_blt() pre-blit-decision
     * exception documented on vdi_screen_is_truecolor() itself -- deciding
     * whether to run pre_blit() ahead of the already-dispatched text_blit(),
     * not a primitive.
     */
#if CONF_WITH_VDI_BACKEND_TRUECOLOR
    if (vdi_screen_is_truecolor() && (vars.STYLE & (F_SKEW|F_THICKEN|F_OUTLINE)))
        need_preblit = TRUE;
    else
#endif
    if (vars.STYLE & F_OUTLINE)
        need_preblit = TRUE;
    else if (linea_vars.CHUP && (vars.STYLE & (F_SKEW|F_THICKEN)))
        need_preblit = TRUE;
    else if ((vars.STYLE & F_SKEW) && clipped)
        need_preblit = TRUE;

    if (need_preblit)
    {
        pre_blit(&vars);
    }

    if (linea_vars.CHUP)
    {
        rotate(&vars);
    }

    if (vars.STYLE & F_THICKEN)
    {
        vars.smear = linea_vars.WEIGHT;
        if (!linea_vars.MONO)
            vars.DELX += vars.smear;
    }

    if (do_clip(&vars) == 0)
    {
        screen_blit(&vars);
    }

upda_dst:
    delx = linea_vars.DELX;
    if (linea_vars.SCALE)
    {
        delx = vars.swap_tmps ? vars.tmp_dely : vars.tmp_delx;
    }

    if (linea_vars.STYLE & F_OUTLINE)
    {
        delx += OUTLINE_THICKNESS * 2;
        dely += OUTLINE_THICKNESS * 2;
    }

    if ((linea_vars.STYLE & F_THICKEN) && !linea_vars.MONO)
    {
        delx += linea_vars.WEIGHT;
    }

    switch(linea_vars.CHUP) {
    default:        /* normally 0, the default */
        linea_vars.DESTX += delx;      /* move right by DELX */
        break;
    case 900:
        linea_vars.DESTY -= delx;      /* move up by DELX */
        break;
    case 1800:
        linea_vars.DESTX -= delx;      /* move left by DELX */
        break;
    case 2700:
        linea_vars.DESTY += delx;      /* move down by DELX */
        break;
    }
}
