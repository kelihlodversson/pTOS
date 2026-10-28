/* Native GEM frame appearance; no GEM decoration state crosses CUIS.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "gem.h"
#include "gemerror.h"
#include "application.h"
#include "transport.h"
#include "layout.h"
#include "frames.h"
#include "gemframegeom.h"
#include "gemobjbounds.h"

BOOL aes_frame_prepare(WM_FRAME_STATE *state, UWORD kind, const GRECT *border,
                       char *name, char *info, WORD *global)
{
    WORD width, height;
    WM_FRAME_INSETS insets;
    if (!aes_frame_metrics(&state->wbox,&state->hbox,&width,&height,global) ||
        !wm_frame_insets(kind,state->wbox,state->hbox,TRUE,width,height,&insets)) return FALSE;
    /* Native titles inset their text by ADJ3DSTD at both sides. Admission
     * keeps every visible object positive, with room for the drop shadow. */
    if (state->wbox > 16383 || state->hbox > 16383 || border->g_w < 5 || border->g_h <= insets.top+insets.bottom ||
        border->g_w > 32763 || border->g_h > 32763 ||
        border->g_x < -32766 || border->g_y < -32766 ||
        (LONG)border->g_x+border->g_w+2 > 32767 ||
        (LONG)border->g_y+border->g_h+2 > 32767) return FALSE;
    if (border->g_w <= ((kind & CLOSER) != 0)*(state->wbox+4) +
        ((kind & FULLER) != 0)*(state->wbox+4) + 4) return FALSE;
    state->border = *border; state->kind = kind;
    state->adjusted_width = state->wbox+4-(width < 640 ? 1+(height >= 400) : 0);
    state->adjusted_height = state->hbox+4;
    state->name = name; state->info = info;
    state->hslide = state->vslide = 0;
    state->hslsiz = state->vslsiz = -1;
    state->colours = NULL;
    state->topped = FALSE;
    return TRUE;
}
WORD aes_frame_control(const WM_FRAME_STATE *state, LONG x, LONG y)
{
    WM_FRAME frame;
    WORD object;
    if (x < -32768 || x > 32767 || y < -32768 || y > 32767) return 0;
    wm_frame_init(&frame);
    wm_frame_build(frame.tree,&frame.name,&frame.info,state);
    /* Later siblings win overlapping 3D hit bounds, exactly as ob_find()
     * does in the native control manager before entering gr_watchbox(). */
    object = objc_find(frame.tree,ROOT,8,(WORD)x,(WORD)y);
    return object == W_CLOSER || object == W_FULLER ||
        (object == W_NAME && (state->kind & MOVER)) ? object : 0;
}
BOOL aes_frame_control_inside(const WM_FRAME_STATE *state, WORD object, LONG x, LONG y)
{
    WM_FRAME frame;
    LONG left, top, width, height;
    if ((object != W_CLOSER || !(state->kind & CLOSER)) &&
        (object != W_FULLER || !(state->kind & FULLER))) return FALSE;
    wm_frame_init(&frame);
    wm_frame_build(frame.tree,&frame.name,&frame.info,state);
    left = (LONG)frame.tree[W_BOX].ob_x+frame.tree[W_TITLE].ob_x+frame.tree[object].ob_x;
    top = (LONG)frame.tree[W_BOX].ob_y+frame.tree[W_TITLE].ob_y+frame.tree[object].ob_y;
    ob_3d_bounds(&frame.tree[object],1,FALSE,&left,&top,&width,&height);
    return x >= left && y >= top && x < left+width && y < top+height;
}
BOOL aes_frame_paint_control(LONG native, const WM_FRAME_STATE *state, WORD object, BOOL selected, WORD *global)
{
    WM_FRAME frame;
    WM_FRAME_STATE drawing = *state;
    static const WORD top[NUM_ELEM] = {
        0x1101,0x1101,0x1101,0x11a1,0x1101,0x1101,0x1101,0x1101,0x1101,
        0x1101,0x1101,0x1101,0x1111,0x1101,0x1101,0x1101,0x1101,0x1111,0x1101
    };
    static const WORD bottom[NUM_ELEM] = {
        0x1101,0x1101,0x1101,0x1101,0x1101,0x1101,0x1101,0x1101,0x1101,
        0x1101,0x1101,0x1101,0x1111,0x1101,0x1101,0x1101,0x1101,0x1111,0x1101
    };
    LONG session = aes_application_session(global), result, region[4];
    ULONG index;
    BOOL ok = TRUE;
    if (session < 0 || cuis_call6(CUIS_DISPLAY_LOCK,session,1,1,0,0,0) != E_OK) return FALSE;
    result = cuis_call6(CUIS_FOCUS_QUERY,native,0,0,0,0,0);
    if (result < 0) { ok = FALSE; goto done; }
    drawing.topped = result != 0;
    drawing.colours = drawing.topped ? top : bottom;
    wm_frame_init(&frame);
    wm_frame_build(frame.tree,&frame.name,&frame.info,&drawing);
    if (selected && drawing.topped && (object == W_CLOSER || object == W_FULLER || object == W_NAME))
        frame.tree[object].ob_state |= SELECTED;
    for (index = 0; ; index++) {
        result = cuis_call6(CUIS_WINDOW_REGION,native,index,(uintptr_t)&region[0],
                            (uintptr_t)&region[1],(uintptr_t)&region[2],(uintptr_t)&region[3]);
        if (result < 0) { ok = FALSE; break; }
        if (!result) break;
        if (region[0] < -32768 || region[0] > 32767 || region[1] < -32768 || region[1] > 32767 ||
            region[2] <= 0 || region[2] > 32767 || region[3] <= 0 || region[3] > 32767 ||
            !mt_objc_draw(frame.tree,ROOT,8,region[0],region[1],region[2],region[3],global)) {
            ok = FALSE; break;
        }
    }
done:
    if (!ok) (void)cuis_call6(CUIS_DISPLAY_REDRAW,session,(QUAD)state->border.g_x,
                              (QUAD)state->border.g_y,state->border.g_w+2,state->border.g_h+2,0);
    if (cuis_call6(CUIS_DISPLAY_LOCK,session,0,0,0,0,0) != E_OK) ok = FALSE;
    return ok;
}

BOOL aes_frame_paint(LONG native, const WM_FRAME_STATE *state, WORD *global)
{
    return aes_frame_paint_control(native,state,0,FALSE,global);
}
