/* Standard GEM layout over private, read-only workstation queries.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "gem.h"
#include "gemerror.h"
#include "vdi_service.h"
#include "application.h"
#include "transport.h"
#include "gemlayout.h"
#include "layout.h"
#include "gemobjop.h"

BOOL aes_layout_metrics(LAYOUT_METRICS *m, WORD *global)
{
    WORD input[11] = { 1,1,1,1,1,1,1,0,1,1,2 };
    VDI_SERVICE_WORKSTATION ws;
    VDI_SERVICE_DRAW_REQUEST request = { 0 };
    VDI_SERVICE_DRAW_RESULT result = { 0 };
    LONG status, closed;
    if (aes_application_session(global) < 0) return FALSE;
    if (aes_graphics_call(VDI_SERVICE_OPEN,(uintptr_t)input,(uintptr_t)&ws,0,0) != E_OK) return FALSE;
    request.code = 38; /* vqt_attributes: current system font cell metrics. */
    status = aes_graphics_call(VDI_SERVICE_DRAW,ws.handle,(uintptr_t)&request,(uintptr_t)&result,0);
    closed = aes_graphics_call(VDI_SERVICE_CLOSE,ws.handle,0,0,0);
    if (status != E_OK || closed != E_OK || ws.intout[0] < 0 || ws.intout[0] >= 32767 ||
        ws.intout[1] < 0 || ws.intout[1] >= 32767 || result.ptsout[2] <= 0 ||
        result.ptsout[3] <= 0 || result.ptsout[3] > 32764) return FALSE;
    m->width = ws.intout[0]+1; m->height = ws.intout[1]+1;
    m->wchar = result.ptsout[2]; m->hchar = result.ptsout[3];
    m->hbox = m->hchar+3; /* Original gsx_start() menu/box height. */
    return TRUE;
}

short mt_rsrc_obfix(OBJECT *tree, short object, short *global)
{
    LAYOUT_METRICS m;
    if (!tree || object < 0 || !aes_layout_metrics(&m,global)) return 0;
    rs_obfix_metrics(tree,object,m.wchar,m.hchar,m.width);
    return 1;
}
short rsrc_obfix(OBJECT *tree, short object) { return mt_rsrc_obfix(tree,object,aes_global); }

/* Overflow rejection belongs to the userspace adapter. The native shared
 * painter keeps its original WORD behavior and pays no ROM cost for this. */
static BOOL center_extent(OBJECT *root)
{
    LONG extra = root->ob_state & OUTLINED ? 6 : 0;
    WORD thickness, dummy;
    OBSPEC spec;
    GRECT rect;
    if (root->ob_state & SHADOWED) {
        ob_sst(root,ROOT,&spec,&dummy,&dummy,&dummy,&rect,&thickness);
        extra += thickness < 0 ? -2L*thickness : 2L*thickness;
    }
    return (LONG)root->ob_width+extra <= 32767 && (LONG)root->ob_height+extra <= 32767;
}

short mt_form_center_grect(OBJECT *tree, GRECT *rect, short *global)
{
    LAYOUT_METRICS m;
    OBJECT root;
    GRECT bounds;
    if (!tree || !rect || tree[0].ob_width < 0 || tree[0].ob_height < 0 || !aes_layout_metrics(&m,global)) return 0;
    root = tree[0];
    if (!center_extent(&root)) return 0;
    ob_center_metrics(&root,&bounds,m.width,m.height,m.hbox);
    /* The native helper retains WORD geometry. Stage its result so an
     * unrepresentable outline/shadow extent cannot alter caller data. */
    if (bounds.g_w < 0 || bounds.g_h < 0) return 0;
    tree[0].ob_x = root.ob_x; tree[0].ob_y = root.ob_y;
    *rect = bounds;
    return 1;
}
short form_center_grect(OBJECT *tree, GRECT *rect) { return mt_form_center_grect(tree,rect,aes_global); }
short mt_form_center(OBJECT *tree, short *x, short *y, short *w, short *h, short *global)
{
    GRECT bounds;
    if (!x || !y || !w || !h || !mt_form_center_grect(tree,&bounds,global)) return 0;
    *x = bounds.g_x; *y = bounds.g_y; *w = bounds.g_w; *h = bounds.g_h;
    return 1;
}
short form_center(OBJECT *tree, short *x, short *y, short *w, short *h)
{
    return mt_form_center(tree,x,y,w,h,aes_global);
}
