/* Frame margins derived from native wm_calc(), with caller-local metrics.
 * Copyright 1999, Caldera Thin Clients, Inc.
 * Copyright 2002-2026 The EmuTOS development team. GPL version 2 or later. */
#include "gemframegeom.h"
#include "aesdefs.h"
#include "obdefs.h"
BOOL wm_frame_insets(UWORD kind, WORD wbox, WORD hbox, BOOL three_d, WORD screen_width, WORD screen_height, WM_FRAME_INSETS *out)
{
    WM_FRAME_INSETS r = { 1,1,1,1 };
    UWORD vertical, horizontal;
    LONG aw = wbox, ah = hbox;
    if (!out || wbox <= 0 || hbox <= 0 || screen_width <= 0 || screen_height <= 0) return FALSE;
    if (three_d) {
        aw += 2*ADJ3DSTD; ah += 2*ADJ3DSTD;
        /* Native wm_init(): TOS4 keeps four desktop icon columns in low
         * resolution by narrowing only the adjusted horizontal box metric. */
        if (screen_width < 640) {
            aw--;
            if (screen_height >= 400) aw--;
        }
        if (kind & (NAME|CLOSER|FULLER|MOVER)) r.top += ah-1;
        if (kind & INFO) r.top += hbox;
        vertical = kind & (UPARROW|DNARROW|VSLIDE|SIZER);
        horizontal = kind & (LFARROW|RTARROW|HSLIDE|SIZER);
        if (vertical && horizontal) {
            vertical &= ~SIZER; horizontal &= ~SIZER;
            if (!vertical && !horizontal) vertical |= SIZER;
        }
        if (vertical) r.right += aw-1;
        if (horizontal) r.bottom += ah-1;
    } else {
        if (kind & (NAME|CLOSER|FULLER)) r.top += ah-1;
        if (kind & INFO) r.top += ah-1;
        if (kind & (UPARROW|DNARROW|VSLIDE|SIZER)) r.right += aw-1;
        if (kind & (LFARROW|RTARROW|HSLIDE|SIZER)) r.bottom += ah-1;
    }
    *out = r;
    return TRUE;
}
