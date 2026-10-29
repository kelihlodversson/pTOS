/* Frame margins derived from native wm_calc(), with caller-local metrics.
 * Copyright 1999, Caldera Thin Clients, Inc.
 * Copyright 2002-2026 The EmuTOS development team. GPL version 2 or later. */
#include "gemframegeom.h"
#include "aesdefs.h"
#include "gemframeids.h"
#include "obdefs.h"
#include "gemobjbounds.h"
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

/* Admission and interactive sizing share the renderer's WORD scratch limits.
 * Native frame templates use one-pixel borders for title text and gadgets. */
BOOL wm_frame_limits(UWORD kind, LONG x, LONG y, WORD wbox, WORD hbox,
                      WORD screen_width, WORD screen_height, WM_FRAME_LIMITS *out)
{
    WM_FRAME_INSETS insets;
    WM_FRAME_LIMITS r;
    LONG pad = ob_3d_padding(1), aw, ah, edge, offset, deduction;
    LONG data_top, vertical_reserve, horizontal_reserve;
    UWORD top = kind & (NAME|CLOSER|FULLER|MOVER);
    if (x < -32766 || y < -32766 || x > 32767 || y > 32767 ||
        wbox > 16383 || hbox > 16383 ||
        !wm_frame_insets(kind,wbox,hbox,TRUE,screen_width,screen_height,&insets)) return FALSE;
    aw = wbox+2*ADJ3DSTD-(screen_width < 640 ? 1+(screen_height >= 400) : 0);
    ah = hbox+2*ADJ3DSTD;
    data_top = (top ? ah : 0)+((kind & INFO) ? hbox : 0);
    vertical_reserve = (kind & (UPARROW|DNARROW|VSLIDE|SIZER)) ? aw-1 : 0;
    horizontal_reserve = (kind & (LFARROW|RTARROW|HSLIDE|SIZER)) ? ah-1 : 0;
    r.min_w = insets.left+insets.right+1;
    edge = ((kind & CLOSER) != 0)*(wbox+4L)+((kind & FULLER) != 0)*(wbox+4L)+5;
    if (r.min_w < edge) r.min_w = edge;
    r.min_h = insets.top+insets.bottom+1;
    r.max_w = 32765L-x; if (r.max_w > 32763) r.max_w = 32763;
    r.max_h = 32765L-y; if (r.max_h > 32763) r.max_h = 32763;
    if (kind & SIZER) {
        /* Include both bevel margins above/below the logical corner. */
        edge = insets.top-(top ? 0 : 1)+hbox+2*ADJ3DSTD;
        if (r.min_h < edge) r.min_h = edge;
    }
    if (kind & (NAME|CLOSER|FULLER)) {
        if (y+ADJ3DSTD-pad < -32768L || y+ADJ3DSTD+hbox+pad > 32767L || hbox+2*pad > 32767L)
            return FALSE;
    }
    if (kind & CLOSER) {
        if (x+ADJ3DSTD-pad < -32768L || x+ADJ3DSTD+wbox+pad > 32767L || wbox+2*pad > 32767L)
            return FALSE;
    }
    if (kind & NAME) {
        offset = ADJ3DSTD+((kind & CLOSER) ? aw : 0);
        deduction = 2*ADJ3DSTD+((kind & CLOSER) != 0)*aw+((kind & FULLER) != 0)*aw;
        if (x+offset-pad < -32768L) return FALSE;
        edge = 32767L-2*pad+deduction;
        if (r.max_w > edge) r.max_w = edge;
        edge = 32767L-x-offset-pad+deduction;
        if (r.max_w > edge) r.max_w = edge;
    }
    if (kind & FULLER) {
        edge = -32768L-x+aw-ADJ3DSTD+pad;
        if (r.min_w < edge) r.min_w = edge;
        edge = 32767L-x+aw-ADJ3DSTD-wbox-pad;
        if (r.max_w > edge) r.max_w = edge;
        if (wbox+2*pad > 32767L) return FALSE;
    }
    if (kind & SIZER) {
        edge = -32768L-x+wbox+ADJ3DSTD+pad;
        if (r.min_w < edge) r.min_w = edge;
        edge = 32767L-x+ADJ3DSTD-pad;
        if (r.max_w > edge) r.max_w = edge;
        if (wbox+2*pad > 32767L) return FALSE;
        edge = -32768L-y+hbox+ADJ3DSTD+pad;
        if (r.min_h < edge) r.min_h = edge;
        edge = 32767L-y+ADJ3DSTD-pad;
        if (r.max_h > edge) r.max_h = edge;
        if (hbox+2*pad > 32767L) return FALSE;
    }
    /* The native bar builder positions each arrow with its 3D margins.
     * Keep opposing expanded glyphs disjoint and reserve the other bar. */
    if (kind & (UPARROW|DNARROW|VSLIDE)) {
        edge = data_top+horizontal_reserve+ah*((kind & UPARROW) != 0)+
            ah*((kind & DNARROW) != 0)+((kind & DNARROW) != 0)+(horizontal_reserve != 0);
        if (r.min_h < edge) r.min_h = edge;
        edge = -32768L-x+aw-ADJ3DSTD+pad;
        if (r.min_w < edge) r.min_w = edge;
        edge = 32767L-x+aw-ADJ3DSTD-wbox-pad;
        if (r.max_w > edge) r.max_w = edge;
        if (wbox+2*pad > 32767L) return FALSE;
    }
    if (kind & UPARROW) {
        edge = y+data_top+ADJ3DSTD;
        if (edge-pad < -32768L || edge+hbox+pad > 32767L || hbox+2*pad > 32767L) return FALSE;
    }
    if (kind & DNARROW) {
        offset = horizontal_reserve+ah-ADJ3DSTD+1;
        edge = -32768L-y+offset+pad;
        if (r.min_h < edge) r.min_h = edge;
        edge = 32767L-y+offset-hbox-pad;
        if (r.max_h > edge) r.max_h = edge;
        if (hbox+2*pad > 32767L) return FALSE;
    }
    if (kind & (LFARROW|RTARROW|HSLIDE)) {
        edge = vertical_reserve+((kind & LFARROW) ? wbox+2*ADJ3DSTD : 0)+
            ((kind & RTARROW) ? aw+1 : 0)+(vertical_reserve != 0);
        if (r.min_w < edge) r.min_w = edge;
        edge = -32768L-y+ah-ADJ3DSTD+pad;
        if (r.min_h < edge) r.min_h = edge;
        edge = 32767L-y+ah-ADJ3DSTD-hbox-pad;
        if (r.max_h > edge) r.max_h = edge;
        if (hbox+2*pad > 32767L) return FALSE;
    }
    if (kind & LFARROW) {
        if (x+ADJ3DSTD-pad < -32768L || x+ADJ3DSTD+wbox+pad > 32767L || wbox+2*pad > 32767L)
            return FALSE;
    }
    if (kind & RTARROW) {
        offset = vertical_reserve+aw-ADJ3DSTD+1;
        edge = -32768L-x+offset+pad;
        if (r.min_w < edge) r.min_w = edge;
        edge = 32767L-x+offset-wbox-pad;
        if (r.max_w > edge) r.max_w = edge;
        if (wbox+2*pad > 32767L) return FALSE;
    }
    /* Bound even full-sized 3D elevators using the production painter's
     * padded dimensions and endpoints. Track space excludes the arrows. */
    if (kind & VSLIDE) {
        LONG arrows = ah*(((kind & UPARROW) != 0)+((kind & DNARROW) != 0));
        if (hbox <= 2*ADJ3DSTD+1) return FALSE;
        edge = data_top+horizontal_reserve+arrows+hbox;
        if (r.min_h < edge) r.min_h = edge;
        edge = y+data_top+((kind & UPARROW) ? ah : 0)+ADJ3DSTD;
        if (edge-pad < -32768L) return FALSE;
        edge = 32767L-2*pad+2*ADJ3DSTD+1+data_top+horizontal_reserve+arrows;
        if (r.max_h > edge) r.max_h = edge;
        edge = 32767L-y+horizontal_reserve+((kind & DNARROW) ? ah : 0)+ADJ3DSTD+1-pad;
        if (r.max_h > edge) r.max_h = edge;
    }
    if (kind & HSLIDE) {
        LONG arrows = aw*(((kind & LFARROW) != 0)+((kind & RTARROW) != 0));
        if (wbox <= 2*ADJ3DSTD+1) return FALSE;
        edge = vertical_reserve+arrows+wbox;
        if (r.min_w < edge) r.min_w = edge;
        edge = x+((kind & LFARROW) ? aw : 0)+ADJ3DSTD;
        if (edge-pad < -32768L) return FALSE;
        edge = 32767L-2*pad+2*ADJ3DSTD+1+vertical_reserve+arrows;
        if (r.max_w > edge) r.max_w = edge;
        edge = 32767L-x+vertical_reserve+((kind & RTARROW) ? aw : 0)+ADJ3DSTD+1-pad;
        if (r.max_w > edge) r.max_w = edge;
    }
    if (r.min_w > r.max_w || r.min_h > r.max_h) return FALSE;
    *out = r; return TRUE;
}

/* Standard native line actions; bars and slider page regions are separate. */
WORD wm_arrow_action(WORD object)
{
    switch (object) {
    case W_UPARROW: return WA_UPLINE;
    case W_DNARROW: return WA_DNLINE;
    case W_LFARROW: return WA_LFLINE;
    case W_RTARROW: return WA_RTLINE;
    }
    return -1;
}
