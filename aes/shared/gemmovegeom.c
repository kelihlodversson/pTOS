/* Native hctl_window()/gr_dragbox() mover geometry.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "gemmovegeom.h"
#include "config.h"
#include "gemframegeom.h"
#include "obdefs.h"
BOOL wm_move_position(const GRECT *start, UWORD kind, LONG press_x, LONG press_y,
                      LONG x, LONG y, WORD screen_width, WORD screen_height, WORD wbox, WORD hbox,
                      GRECT *result)
{
    QUAD left = (QUAD)start->g_x+x-press_x, top = (QUAD)start->g_y+y-press_y;
    WM_FRAME_LIMITS limits;
    LONG excess_x, excess_y;
    WORD attempt;
    LONG max_x = (LONG)screen_width-wbox-6-2*ADJ3DSTD;
    LONG max_y = (LONG)hbox+MAX_COORDINATE-start->g_h;
    if (start->g_w <= 0 || start->g_h <= 0 || screen_width <= 0 || wbox <= 0 || hbox <= 0) return FALSE;
    /* Native rc_constrain applies the lower edge before the upper edge,
     * including windows taller than its MAX_COORDINATE drag rectangle. */
    if (left < 0) left = 0;
    if (top < hbox) top = hbox;
    if (left > max_x) left = max_x;
    if (top > max_y) top = max_y;
    /* Move only the origin: renderer padding must never shrink the window.
     * Each maximum is a minimum of fixed and origin-relative bounds. Moving
     * back by its excess satisfies every relative bound in one correction;
     * a remaining excess is an immutable dimension limit and cannot be fixed. */
    for (attempt = 0; attempt < 2; attempt++) {
        if (!wm_frame_limits(kind,left,top,wbox,hbox,screen_width,screen_height,&limits)) return FALSE;
        excess_x = start->g_w-limits.max_w;
        excess_y = start->g_h-limits.max_h;
        if (excess_x <= 0 && excess_y <= 0) break;
        if (excess_x > 0) left -= excess_x;
        if (excess_y > 0) top -= excess_y;
    }
    if (attempt == 2 || start->g_w < limits.min_w || start->g_h < limits.min_h) return FALSE;
    *result = *start; result->g_x = left; result->g_y = top;
    return TRUE;
}
