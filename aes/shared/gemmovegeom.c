/* Native hctl_window()/gr_dragbox() mover geometry.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "gemmovegeom.h"
#include "config.h"
#include "obdefs.h"
BOOL wm_move_position(const GRECT *start, LONG press_x, LONG press_y,
                      LONG x, LONG y, WORD screen_width, WORD wbox, WORD hbox,
                      GRECT *result)
{
    QUAD left = (QUAD)start->g_x+x-press_x, top = (QUAD)start->g_y+y-press_y;
    LONG max_x = (LONG)screen_width-wbox-6-2*ADJ3DSTD;
    LONG max_y = (LONG)hbox+MAX_COORDINATE-start->g_h;
    if (start->g_w <= 0 || start->g_h <= 0 || screen_width <= 0 || wbox <= 0 || hbox <= 0) return FALSE;
    /* Native rc_constrain applies the lower edge before the upper edge,
     * including windows taller than its MAX_COORDINATE drag rectangle. */
    if (left < 0) left = 0;
    if (top < hbox) top = hbox;
    if (left > max_x) left = max_x;
    if (top > max_y) top = max_y;
    /* The shared GSX outline endpoints are WORDs. Never wrap an oversized
     * window's right/bottom edges while retaining native off-screen motion. */
    if (left+start->g_w-1 > 32767) left = 32768L-start->g_w;
    if (top+start->g_h-1 > 32767) top = 32768L-start->g_h;
    if (left < -32768 || left > 32767 || top < -32768 || top > 32767) return FALSE;
    *result = *start; result->g_x = left; result->g_y = top;
    return TRUE;
}
