/* Native hctl_window()/gr_rubwind() sizing with safe frame extents.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "gemsizegeom.h"
#include "gemframegeom.h"
#include "aesdefs.h"
#include "obdefs.h"
BOOL wm_size_position(const GRECT *start, UWORD kind, LONG x, LONG y,
                      WORD wchar, WORD hchar, WORD wbox, WORD hbox,
                      WORD screen_width, WORD screen_height, GRECT *result)
{
    WM_FRAME_INSETS insets;
    QUAD width = (QUAD)x-start->g_x+1, height = (QUAD)y-start->g_y+1;
    LONG min_w = wchar, min_h = hchar, edge, max_w, max_h;
    UWORD top = kind & (NAME|CLOSER|FULLER|MOVER);
    if (!(kind & SIZER) || wchar <= 0 || hchar <= 0 || start->g_w <= 0 || start->g_h <= 0 ||
        start->g_x < -32766 || start->g_y < -32766 ||
        !wm_frame_insets(kind,wbox,hbox,TRUE,screen_width,screen_height,&insets)) return FALSE;
    if (top || (kind & (LFARROW|RTARROW|HSLIDE))) min_w = 4L*wbox;
    if (kind & (UPARROW|DNARROW|VSLIDE)) min_h = 6L*hbox;
    /* Retain native minima, then keep work, title and the SIZER positive.
     * Native TOS4's lone SIZER reserves a vertical bar, not both bars. */
    edge = insets.left+insets.right+1;
    if (min_w < edge) min_w = edge;
    edge = ((kind & CLOSER) != 0)*(wbox+4L)+((kind & FULLER) != 0)*(wbox+4L)+5;
    if (min_w < edge) min_w = edge;
    edge = insets.top+insets.bottom+1;
    if (min_h < edge) min_h = edge;
    edge = insets.top-(top ? 0 : 1)+hbox+ADJ3DSTD;
    if (min_h < edge) min_h = edge;
    max_w = 32765L-start->g_x; if (max_w > 32763) max_w = 32763;
    max_h = 32765L-start->g_y; if (max_h > 32763) max_h = 32763;
    if (min_w > max_w || min_h > max_h) return FALSE;
    if (width < min_w) width = min_w;
    if (height < min_h) height = min_h;
    if (width > max_w) width = max_w;
    if (height > max_h) height = max_h;
    *result = *start; result->g_w = width; result->g_h = height;
    return TRUE;
}
