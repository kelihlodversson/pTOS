/* Shared native GEM resource scaling and form centering.
 * Copyright 1999, Caldera Thin Clients, Inc.
 * Copyright 2002-2026 The EmuTOS development team. GPL version 2 or later. */
#include "emutos.h"
#include "obdefs.h"
#include "gemobjop.h"
#include "optimopt.h"
#include "gemlayout.h"

/*
 *  Fix up a character position, from offset,row/col to a pixel value.
 *  If width is 80 then convert to full screen width.
 */
static void fix_chpos(WORD *pfix, WORD offset, WORD wchar, WORD hchar, WORD screen_width)
{
    WORD coffset;
    WORD cpos;

    cpos = *pfix;
    coffset = HIBYTE(cpos);
    cpos = LOBYTE(cpos);

    switch(offset)
    {
    case 0:
        cpos *= wchar;
        break;
    case 1:
        cpos *= hchar;
        break;
    case 2:
        if (cpos == 80)
            cpos = screen_width;
        else
            cpos *= wchar;
        break;
    case 3:
        cpos *= hchar;
        break;
    }

    cpos += ( coffset > 128 ) ? (coffset - 256) : coffset;
    *pfix = cpos;
}


/************************************************************************/
/* rs_obfix                                                             */
/************************************************************************/
void rs_obfix_metrics(OBJECT *tree, WORD curob, WORD wchar, WORD hchar, WORD screen_width)
{
    WORD offset;
    WORD *p;

    /* set X,Y,W,H */
    p = &tree[curob].ob_x;

    for (offset=0; offset<4; offset++)
        fix_chpos(p+offset, offset, wchar, hchar, screen_width);
}


BOOL ob_center_metrics(OBJECT *tree, GRECT *pt, WORD screen_width, WORD screen_height, WORD hbox)
{
    WORD    xd, yd, wd, hd;
    OBSPEC spec;
    WORD dummy;
    GRECT t;
    WORD th;
    LONG extra = 0;

    wd = tree->ob_width;
    hd = tree->ob_height;
    xd = (screen_width - wd) / 2;
    yd = hbox + ((screen_height - hbox - hd) / 2);
    tree->ob_x = xd;
    tree->ob_y = yd;

    /* account for outline */
    if (tree->ob_state & OUTLINED)
    {
        xd -= 3;
        if (xd < 0)     /* don't move object offscreen */
            xd = 0;
        yd -= 3;
        if (yd < 0)     /* don't move object offscreen */
            yd = 0;
        extra += 6;
        wd += 6;
        hd += 6;
    }

    /* account for shadow */
    if (tree->ob_state & SHADOWED)
    {
        ob_sst(tree, ROOT, &spec, &dummy, &dummy, &dummy, &t, &th);
        extra += th < 0 ? -2L*th : 2L*th;
        if (th < 0)
            th = -th;
        th += th;
        wd += th;
        hd += th;
    }
    r_set(pt, xd, yd, wd, hd);
    /* Native callers retain the WORD result. Userspace can reject expanded
     * extents that wrapped, including twice a maximum TEDINFO thickness. */
    return (LONG)tree->ob_width+extra <= 32767 && (LONG)tree->ob_height+extra <= 32767;
}
