/* Reuse the native frame builder and gr_slidebox's 3D rounding rules.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "gemslidegeom.h"
#include "aesdefs.h"
BOOL wm_slider_geometry(const WM_FRAME_STATE *state, WORD object, WM_SLIDER_GEOMETRY *out)
{
    WM_FRAME frame;
    WM_SLIDER_GEOMETRY r;
    WORD bar, track;
    LONG x,y,w,h;
    if (object != W_VELEV && object != W_HELEV) return FALSE;
    r.vertical = object == W_VELEV;
    if (!(state->kind & (r.vertical ? VSLIDE : HSLIDE))) return FALSE;
    bar = r.vertical ? W_VBAR : W_HBAR; track = object-1;
    wm_frame_init(&frame); wm_frame_build(frame.tree,&frame.name,&frame.info,state);
    x = (LONG)state->border.g_x+frame.tree[W_DATA].ob_x+frame.tree[bar].ob_x+frame.tree[track].ob_x;
    y = (LONG)state->border.g_y+frame.tree[W_DATA].ob_y+frame.tree[bar].ob_y+frame.tree[track].ob_y;
    w = frame.tree[track].ob_width; h = frame.tree[track].ob_height;
    if (x < -32768L || y < -32768L || x+w > 32767L || y+h > 32767L || w <= 0 || h <= 0) return FALSE;
    r.track.g_x = x; r.track.g_y = y; r.track.g_w = w; r.track.g_h = h;
    x += frame.tree[object].ob_x-ADJ3DSTD; y += frame.tree[object].ob_y-ADJ3DSTD;
    w = frame.tree[object].ob_width+2L*ADJ3DSTD;
    h = frame.tree[object].ob_height+2L*ADJ3DSTD;
    if (x < -32768L || y < -32768L || x+w > 32767L || y+h > 32767L || w <= 0 || h <= 0 ||
        (r.vertical ? h > r.track.g_h : w > r.track.g_w)) return FALSE;
    r.thumb.g_x = x; r.thumb.g_y = y; r.thumb.g_w = w; r.thumb.g_h = h;
    *out = r; return TRUE;
}
BOOL wm_slider_position(const WM_SLIDER_GEOMETRY *slider, LONG press_x, LONG press_y,
                        LONG x, LONG y, WORD *value, GRECT *outline)
{
    QUAD left = (QUAD)slider->thumb.g_x+x-press_x;
    QUAD top = (QUAD)slider->thumb.g_y+y-press_y;
    LONG max_x = (LONG)slider->track.g_x+slider->track.g_w-slider->thumb.g_w;
    LONG max_y = (LONG)slider->track.g_y+slider->track.g_h-slider->thumb.g_h;
    LONG divnd, divis;
    /* Native rc_constrain applies each lower bound before its upper bound,
     * including the wider 3D vertical thumb in low-resolution modes. */
    if (left < slider->track.g_x) left = slider->track.g_x;
    if (top < slider->track.g_y) top = slider->track.g_y;
    if (left > max_x) left = max_x;
    if (top > max_y) top = max_y;
    if (left < -32768L || top < -32768L || left > 32767L || top > 32767L) return FALSE;
    divnd = slider->vertical ? top-slider->track.g_y : left-slider->track.g_x;
    divis = slider->vertical ? slider->track.g_h-slider->thumb.g_h : slider->track.g_w-slider->thumb.g_w;
    if (divis < 0 || divnd < 0) return FALSE;
    /* Native gr_slidebox restores the child's 3D origin before rounding. */
    if (divnd) divnd += ADJ3DSTD;
    if (divnd > divis) divnd = divis;
    *value = divis ? (divnd*1000L+divis/2)/divis : 0;
    *outline = slider->thumb; outline->g_x = left; outline->g_y = top;
    return TRUE;
}
WORD wm_slider_page(const WM_FRAME_STATE *state, WORD object, LONG x, LONG y)
{
    WM_SLIDER_GEOMETRY slider;
    if ((object != W_VSLIDE && object != W_HSLIDE) || !wm_slider_geometry(state,object+1,&slider)) return -1;
    if (slider.vertical) return y < slider.thumb.g_y+ADJ3DSTD ? WA_UPPAGE : WA_DNPAGE;
    return x < slider.thumb.g_x+ADJ3DSTD ? WA_LFPAGE : WA_RTPAGE;
}
