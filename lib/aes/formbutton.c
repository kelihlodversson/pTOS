/* Standard GEM button tracking over existing CUIS/GEM input and drawing.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "gem.h"
#include "gemerror.h"
#include "transport.h"
#include "application.h"
#include "form_context.h"
#include "gemobjtree.h"
#include "gemobjbounds.h"
#include "../../aes/gemobjop.h"
#include "render_context.h"

FORM_CONTEXT *aes_form_context;

static BOOL grab(void)
{
    FORM_CONTEXT *c = aes_form_context;
    if (!c->ok) return FALSE;
    if (!c->grabbed) {
        c->ok = cuis_call6(CUIS_MOUSE_LOCK,c->session,1,1,0,0,0) == E_OK;
        c->grabbed = c->ok;
    }
    return c->ok;
}
static BOOL metrics(void)
{
    FORM_CONTEXT *c = aes_form_context;
    if (!c->ok) return FALSE;
    if (!c->have_metrics) {
        c->ok = aes_layout_metrics(&c->metrics,c->global);
        c->have_metrics = c->ok;
    }
    return c->ok;
}
void aes_form_clip(const GRECT *clip)
{
    (void)clip;
    aes_form_context->full_screen = TRUE;
}
void aes_form_rect(OBJECT *tree, WORD object, GRECT *rect)
{
    WORD border, dummy;
    OBSPEC spec;
    LONG x, y, width, height;
    ob_offset(tree,object,&rect->g_x,&rect->g_y);
    x = rect->g_x; y = rect->g_y;
    ob_sst(tree,object,&spec,&dummy,&dummy,&dummy,rect,&border);
    ob_3d_bounds(tree+object,border,FALSE,&x,&y,&width,&height);
    if (x < -32768L || x > 32767L || y < -32768L || y > 32767L ||
        width < 0 || width > 32767L || height < 0 || height > 32767L) {
        rect->g_w = rect->g_h = 0;
        if (aes_form_context) aes_form_context->ok = FALSE;
        if (aes_painter) aes_painter->ok = FALSE;
        return;
    }
    rect->g_x = x; rect->g_y = y; rect->g_w = width; rect->g_h = height;
}
UWORD aes_form_flags(OBJECT *tree, WORD object, WORD *flags)
{
    *flags = tree[object].ob_flags;
    return tree[object].ob_state;
}
void aes_form_change(OBJECT *tree, WORD object, UWORD state, WORD redraw)
{
    FORM_CONTEXT *c = aes_form_context;
    WORD top;
    if (!c->ok || tree[object].ob_state == state) return;
    if (!redraw) { tree[object].ob_state = state; return; }
    if (!grab() || !metrics()) return;
    top = c->full_screen ? 0 : c->metrics.hbox;
    /* CUIS makes display acquisition fail fast while we retain mouse
     * control, while preserving own display nesting/per-window borrowing. */
    c->ok = mt_objc_change(tree,object,0,0,top,c->metrics.width,c->metrics.height-top,
                            state,1,c->global) != 0;
}
BOOL aes_form_stilldn(BOOL outside, WORD x, WORD y, WORD width, WORD height)
{
    FORM_CONTEXT *c = aes_form_context;
    WORD ready, mx, my, buttons, modifiers;
    if (!c->ok) return FALSE;
    ready = mt_evnt_multi(MU_BUTTON | MU_M1,1,255,0,outside,x,y,width,height,
                          0,0,0,0,0,NULL,0,&mx,&my,&buttons,&modifiers,NULL,NULL,c->global);
    if (!(ready & (MU_BUTTON | MU_M1))) { c->ok = FALSE; return FALSE; }
    if (ready & MU_BUTTON) {
        c->have_release = TRUE;
        c->released_inside = width > 0 && height > 0 && mx >= x && my >= y &&
                            (LONG)mx < (LONG)x+width && (LONG)my < (LONG)y+height;
        return FALSE;
    }
    return TRUE;
}
WORD aes_form_release_wait(WORD clicks, UWORD mask, UWORD state, WORD *results)
{
    FORM_CONTEXT *c = aes_form_context;
    WORD count;
    if (!grab()) return 0;
    count = mt_evnt_button(clicks,mask,state,&results[0],&results[1],&results[2],&results[3],c->global);
    if (count <= 0) c->ok = FALSE;
    return count;
}
WORD aes_form_watch(OBJECT *tree, WORD object, WORD in_state, WORD out_state)
{
    FORM_CONTEXT *c = aes_form_context;
    WORD inside;
    if (!grab()) return 0;
    c->have_release = FALSE;
    inside = aes_form_watch_core(tree,object,in_state,out_state);
    if (c->ok && c->have_release) {
        /* Button and boundary predicates can become ready together. Use the
         * trusted release coordinates before dropping capture ownership. */
        inside = c->released_inside;
        aes_form_change(tree,object,inside ? in_state : out_state,TRUE);
    }
    return inside;
}
static BOOL finish(FORM_CONTEXT *c, FORM_CONTEXT *saved)
{
    if (c->grabbed && cuis_call6(CUIS_MOUSE_LOCK,c->session,0,0,0,0,0) != E_OK) c->ok = FALSE;
    aes_form_context = saved;
    return c->ok;
}
short mt_graf_watchbox(OBJECT *tree, short object, short in_state, short out_state, short *global)
{
    FORM_CONTEXT c = { 0 }, *saved = aes_form_context;
    WORD inside;
    if (!tree || object < 0) return 0;
    c.session = aes_application_session(global);
    if (c.session < 0) return 0;
    c.global = global; c.ok = TRUE; c.full_screen = TRUE;
    aes_form_context = &c;
    inside = aes_form_watch(tree,object,in_state,out_state);
    return finish(&c,saved) ? inside : 0;
}
short graf_watchbox(OBJECT *tree, short object, short in_state, short out_state)
{
    return mt_graf_watchbox(tree,object,in_state,out_state,aes_global);
}
short aes_form_button_checked(OBJECT *tree, short object, short clicks, short *next, short *global, BOOL *ok)
{
    FORM_CONTEXT c = { 0 }, *saved = aes_form_context;
    WORD cont, target;
    *ok = FALSE;
    if (!tree || object < 0 || !next) return 0;
    /* A root cannot have radio siblings; the native algorithm needs a parent. */
    if (!object && (tree[0].ob_flags & (RBUTTON | SELECTABLE)) == (RBUTTON | SELECTABLE) &&
        !(tree[0].ob_state & DISABLED)) return 0;
    c.session = aes_application_session(global);
    if (c.session < 0) return 0;
    c.global = global; c.ok = TRUE;
    aes_form_context = &c;
    cont = aes_form_button_core(tree,object,clicks,&target);
    if (!finish(&c,saved)) return 0;
    *next = target;
    *ok = TRUE;
    return cont;
}
short mt_form_button(OBJECT *tree, short object, short clicks, short *next, short *global)
{
    BOOL ok;
    return aes_form_button_checked(tree,object,clicks,next,global,&ok);
}
short form_button(OBJECT *tree, short object, short clicks, short *next)
{
    return mt_form_button(tree,object,clicks,next,aes_global);
}
