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
    WM_FRAME_LIMITS limits;
    QUAD width = (QUAD)x-start->g_x+1, height = (QUAD)y-start->g_y+1;
    LONG min_w = wchar, min_h = hchar;
    UWORD top = kind & (NAME|CLOSER|FULLER|MOVER);
    if (!(kind & SIZER) || wchar <= 0 || hchar <= 0 || start->g_w <= 0 || start->g_h <= 0 ||
        start->g_x < -32766 || start->g_y < -32766 ||
        !wm_frame_limits(kind,start->g_x,start->g_y,wbox,hbox,screen_width,screen_height,&limits)) return FALSE;
    if (top || (kind & (LFARROW|RTARROW|HSLIDE))) min_w = 4L*wbox;
    if (kind & (UPARROW|DNARROW|VSLIDE)) min_h = 6L*hbox;
    if (min_w < limits.min_w) min_w = limits.min_w;
    if (min_h < limits.min_h) min_h = limits.min_h;
    if (min_w > limits.max_w || min_h > limits.max_h) return FALSE;
    if (width < min_w) width = min_w;
    if (height < min_h) height = min_h;
    if (width > limits.max_w) width = limits.max_w;
    if (height > limits.max_h) height = limits.max_h;
    *result = *start; result->g_w = width; result->g_h = height;
    return TRUE;
}
