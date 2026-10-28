/* Standard standalone GEM popup menus using caller-local tracking.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "gem.h"
#include "gemerror.h"
#include "application.h"
#include "transport.h"
#include "layout.h"
#include "resource_context.h"
#include "gemmenupopup.h"
#include "gemobjbounds.h"
#include "vdi_service.h"
#include "string.h"
#define POPUP_ITEMS 4096

typedef struct { WORD *global; GRECT clip; } POPUP_CONTEXT;
static BOOL popup_draw(void *context, OBJECT *tree, WORD root)
{
    POPUP_CONTEXT *c = context;
    return mt_objc_draw(tree,root,1,c->clip.g_x,c->clip.g_y,c->clip.g_w,c->clip.g_h,c->global);
}
static BOOL popup_change(void *context, OBJECT *tree, WORD item, UWORD state)
{
    POPUP_CONTEXT *c = context;
    return mt_objc_change(tree,item,0,c->clip.g_x,c->clip.g_y,c->clip.g_w,c->clip.g_h,state,1,c->global);
}
static BOOL popup_pointer(void *context, WORD *x, WORD *y)
{
    POPUP_CONTEXT *c = context;
    WORD buttons, modifiers;
    return mt_graf_mkstate(x,y,&buttons,&modifiers,c->global);
}
static WORD popup_find(void *context, OBJECT *tree, WORD root, WORD x, WORD y)
{
    (void)context;
    return objc_find(tree,root,1,x,y);
}
static BOOL popup_rect(void *context, OBJECT *tree, WORD item, GRECT *rect)
{
    LONG x = tree[0].ob_x, y = tree[0].ob_y, w, h;
    (void)context;
    if (item) { x += tree[item].ob_x; y += tree[item].ob_y; }
    ob_3d_bounds(tree+item,0,TRUE,&x,&y,&w,&h);
    if (x < -32768L || x > 32767L || y < -32768L || y > 32767L ||
        w <= 0 || w > 32767L || h <= 0 || h > 32767L) return FALSE;
    rect->g_x = x; rect->g_y = y; rect->g_w = w; rect->g_h = h;
    return TRUE;
}
static WORD popup_wait(void *context, BOOL outside, const GRECT *rect, WORD *mouse)
{
    POPUP_CONTEXT *c = context;
    return mt_evnt_multi(MU_M1|MU_BUTTON,1,255,1,outside,rect->g_x,rect->g_y,rect->g_w,rect->g_h,
                        0,0,0,0,0,NULL,0,mouse,mouse+1,mouse+2,mouse+3,NULL,NULL,c->global);
}
static const POPUP_TRACK_OPS popup_ops = {
    popup_draw, popup_change, popup_pointer, popup_find, popup_rect, popup_wait
};
static BOOL string_item(OBJECT *object)
{
    OBSPEC spec = object->ob_flags & INDIRECT ? *object->ob_spec.indirect : object->ob_spec;
    WORD type = object->ob_type & 255, n;
    if ((type != G_STRING && type != G_TITLE) || !spec.free_string ||
        (object->ob_flags & SUBMENU)) return FALSE;
    for (n = 0; n <= 127; n++) if (!spec.free_string[n]) return TRUE;
    return FALSE;
}
WORD mt_menu_popup(MENU *menu, WORD xpos, WORD ypos, MENU *result, WORD *global)
{
    MENU input, output;
    POPUP_CONTEXT c;
    LAYOUT_METRICS m;
    VDI_SERVICE_WORKSTATION ws;
    WORD workin[11] = { 1,1,1,1,1,1,1,0,1,1,2 }, mouse[4], count = 0, item, i, start = NIL;
    WORD selection = NIL, modifiers = 0, *map = NULL;
    OBJECT *original, *tree = NULL, *box;
    void *buffer = NULL;
    LONG session = aes_application_session(global), x, y, pad, right, bottom, bytes;
    BOOL display = FALSE, grabbed = FALSE, opened = FALSE, copied = FALSE, ok = FALSE;
    if (!menu || !result || session < 0) return 0;
    input = *menu; output = input;
    if (!input.mn_tree || input.mn_menu < 0 || input.mn_item < 0 || input.mn_scroll ||
        !aes_layout_metrics(&m,global)) goto done;
    original = input.mn_tree; box = original+input.mn_menu;
    if (((box->ob_type & 255) != G_BOX && (box->ob_type & 255) != G_IBOX) ||
        (box->ob_flags & HIDETREE) || box->ob_width <= 0 || box->ob_width > m.width ||
        box->ob_height <= 0 || box->ob_height > m.height-m.hbox) goto done;
    /* Classic popup menus contain one box and immediate string/title leaves.
     * Bound traversal even when a caller supplies a cyclic child list. */
    for (item = box->ob_head; item != input.mn_menu; item = original[item].ob_next) {
        OBJECT *child;
        if (item < 0 || count == POPUP_ITEMS) goto done;
        child = original+item;
        if (child->ob_head != NIL || child->ob_tail != NIL || !string_item(child) ||
            child->ob_x < 0 || child->ob_y < 0 || child->ob_width <= 0 || child->ob_height <= 0 ||
            (LONG)child->ob_x+child->ob_width > box->ob_width ||
            (LONG)child->ob_y+child->ob_height > box->ob_height) goto done;
        count++;
        if (item == input.mn_item) start = count;
    }
    if (start == NIL || !count) goto done;
    bytes = (LONG)(count+1)*(sizeof(OBJECT)+sizeof(WORD));
    tree = aes_temporary_alloc(bytes);
    if (!tree) goto done;
    map = (WORD *)(tree+count+1);
    tree[0] = *box; tree[0].ob_next = NIL; tree[0].ob_head = 1; tree[0].ob_tail = count;
    for (item = box->ob_head, i = 1; i <= count; i++, item = original[item].ob_next) {
        tree[i] = original[item]; map[i] = item;
        tree[i].ob_next = i == count ? 0 : i+1;
        if (tree[i].ob_flags & INDIRECT) tree[i].ob_spec = *tree[i].ob_spec.indirect;
        tree[i].ob_flags &= ~(INDIRECT|LASTOB);
    }
    if (tree[0].ob_flags & INDIRECT) tree[0].ob_spec = *tree[0].ob_spec.indirect;
    tree[0].ob_flags &= ~(INDIRECT|LASTOB);
    /* xpos/ypos locate the starting item. Keep the native font-row vertical
     * adjustment, but reject oversized menus instead of looping/wrapping. */
    x = (LONG)xpos-tree[start].ob_x; y = (LONG)ypos-tree[start].ob_y;
    if (x < 0) x = 0;
    if (x > m.width-box->ob_width) x = m.width-box->ob_width;
    bottom = m.height-box->ob_height;
    if (y > bottom) y -= ((y-bottom+m.hchar-1)/m.hchar)*m.hchar;
    if (y < m.hbox) y += ((m.hbox-y+m.hchar-1)/m.hchar)*m.hchar;
    if (y > bottom) y = bottom;
    tree[0].ob_x = x; tree[0].ob_y = y;
    /* Include native outlines, bevels and outside borders/shadows. All leaves
     * lie within the box; string/title effects need at most eight extra pixels. */
    pad = tree[0].ob_spec.obspec.framesize;
    if (pad > 127) pad -= 256;
    if (pad < 0) pad = -pad;
    pad = 8+3*pad;
    right = x+box->ob_width+pad; bottom = y+box->ob_height+pad;
    x -= pad; y -= pad;
    if (x < 0) x = 0;
    if (y < m.hbox) y = m.hbox;
    if (right > m.width) right = m.width;
    if (bottom > m.height) bottom = m.height;
    c.global = global; c.clip.g_x = x; c.clip.g_y = y;
    c.clip.g_w = right-x; c.clip.g_h = bottom-y;
    if (c.clip.g_w <= 0 || c.clip.g_h <= 0 ||
        (ULONG)((c.clip.g_w+15L)&~15L)*(UWORD)c.clip.g_h > VDI_SERVICE_MAX_BITMAP/4) goto done;
    bytes = (LONG)((c.clip.g_w+15L)&~15L)*c.clip.g_h*4;
    buffer = aes_temporary_alloc(bytes);
    if (!buffer) goto done;
    if (cuis_call6(CUIS_DISPLAY_LOCK,session,1,1,0,0,0) != E_OK) goto done;
    display = TRUE;
    if (cuis_call6(CUIS_MOUSE_LOCK,session,1,1,0,0,0) != E_OK) goto done;
    grabbed = TRUE;
    if (mt_evnt_button(1,1,0,mouse,mouse+1,mouse+2,mouse+3,global) <= 0) goto done;
    if (aes_graphics_call(VDI_SERVICE_OPEN,(uintptr_t)workin,(uintptr_t)&ws,0,0) != E_OK) goto done;
    opened = TRUE;
    if (aes_graphics_call(VDI_SERVICE_SCREEN_BUFFER,ws.handle,(uintptr_t)&c.clip,(uintptr_t)buffer,0) != E_OK) goto done;
    copied = TRUE;
    selection = mn_popup_track(tree,0,start,&popup_ops,&c,&modifiers,&ok);
    if (ok && mt_evnt_button(1,1,0,mouse,mouse+1,mouse+2,mouse+3,global) <= 0) ok = FALSE;
    if (selection > 0) output.mn_item = map[selection];
    output.mn_keystate = modifiers;
done:
    if (copied && aes_graphics_call(VDI_SERVICE_SCREEN_BUFFER,ws.handle,(uintptr_t)&c.clip,(uintptr_t)buffer,1) != E_OK) ok = FALSE;
    if (opened && aes_graphics_call(VDI_SERVICE_CLOSE,ws.handle,0,0,0) != E_OK) ok = FALSE;
    if (grabbed && cuis_call6(CUIS_MOUSE_LOCK,session,0,0,0,0,0) != E_OK) ok = FALSE;
    if (display && cuis_call6(CUIS_DISPLAY_LOCK,session,0,0,0,0,0) != E_OK) ok = FALSE;
    if (buffer && aes_resource_discard(buffer) != E_OK) ok = FALSE;
    if (tree && aes_resource_discard(tree) != E_OK) ok = FALSE;
    if (ok && selection > 0) { *result = output; return 1; }
    result->mn_keystate = 0;
    return 0;
}
WORD menu_popup(MENU *menu, WORD x, WORD y, MENU *result)
{ return mt_menu_popup(menu,x,y,result,aes_global); }
