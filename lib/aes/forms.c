/* Standard modal GEM forms in the application's address space.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "gem.h"
#include "gemerror.h"
#include "application.h"
#include "transport.h"
#include "modal_context.h"

MODAL_CONTEXT *aes_modal;

BOOL aes_modal_enter(void)
{
    MODAL_CONTEXT *c = aes_modal;
    c->display = cuis_call6(CUIS_DISPLAY_LOCK,c->session,1,1,0,0,0) == E_OK;
    if (!c->display) return c->ok = FALSE;
    c->mouse = cuis_call6(CUIS_MOUSE_LOCK,c->session,1,1,0,0,0) == E_OK;
    if (c->mouse) c->keyboard = cuis_call6(CUIS_KEYBOARD_LOCK,c->session,1,0,0,0,0) == E_OK;
    if (!c->mouse || !c->keyboard) {
        c->ok = FALSE; aes_modal_leave();
    }
    return c->ok;
}
void aes_modal_leave(void)
{
    MODAL_CONTEXT *c = aes_modal;
    if (c->keyboard && cuis_call6(CUIS_KEYBOARD_LOCK,c->session,0,0,0,0,0) != E_OK) c->ok = FALSE;
    if (c->mouse && cuis_call6(CUIS_MOUSE_LOCK,c->session,0,0,0,0,0) != E_OK) c->ok = FALSE;
    if (c->display && cuis_call6(CUIS_DISPLAY_LOCK,c->session,0,0,0,0,0) != E_OK) c->ok = FALSE;
    c->keyboard = c->mouse = c->display = FALSE;
}
WORD aes_modal_wait(WORD *results)
{
    WORD ready;
    if (!aes_modal->ok) return 0;
    ready = mt_evnt_multi(MU_KEYBD|MU_BUTTON,2,255,1,0,0,0,0,0,0,0,0,0,0,NULL,0,
                          &results[0],&results[1],&results[2],&results[3],&results[4],&results[5],aes_modal->global);
    if (!ready) aes_modal->ok = FALSE;
    return ready;
}
WORD aes_modal_edit(OBJECT *tree, WORD object, WORD key, WORD *index, WORD kind)
{
    WORD result;
    if (!aes_modal->ok && kind != ED_END) return 0;
    result = mt_objc_edit(tree,object,key,index,kind,aes_modal->global);
    if (!result) aes_modal->ok = FALSE;
    return result;
}
WORD aes_modal_keybd(OBJECT *tree, WORD object, WORD *key, WORD *next)
{
    BOOL ok;
    WORD result = aes_form_keybd_checked(tree,object,*next,*key,next,key,aes_modal->global,&ok);
    if (!ok) aes_modal->ok = FALSE;
    return result;
}
WORD aes_modal_button(OBJECT *tree, WORD object, WORD clicks, WORD *next)
{
    BOOL ok;
    WORD result = aes_form_button_checked(tree,object,clicks,next,aes_modal->global,&ok);
    if (!ok) aes_modal->ok = FALSE;
    return result;
}
WORD mt_form_do(OBJECT *tree, WORD start, WORD *global)
{
    MODAL_CONTEXT c = { 0 }, *saved = aes_modal;
    WORD result;
    if (!tree || start < 0) return 0;
    c.session = aes_application_session(global);
    if (c.session < 0 || !aes_layout_metrics(&c.metrics,global)) return 0;
    c.global = global; c.ok = TRUE;
    aes_modal = &c;
    result = aes_form_do_core(tree,start);
    aes_modal = saved;
    return c.ok ? result : 0;
}
WORD form_do(OBJECT *tree, WORD start) { return mt_form_do(tree,start,aes_global); }

WORD aes_form_animation(WORD kind, const GRECT *small, const GRECT *large, WORD *global);
WORD mt_form_dial(WORD kind, WORD sx, WORD sy, WORD sw, WORD sh,
                   WORD x, WORD y, WORD w, WORD h, WORD *global)
{
    LONG session = aes_application_session(global);
    GRECT small, large;
    BOOL ok;
    if (session < 0 || kind < FMD_START || kind > FMD_FINISH) return 0;
    small.g_x = sx; small.g_y = sy; small.g_w = sw; small.g_h = sh;
    large.g_x = x; large.g_y = y; large.g_w = w; large.g_h = h;
    /* Native FMD_START has no persistent allocation or ownership side effect. */
    if (kind == FMD_START) return 1;
    if (w <= 0 || h <= 0) return 0;
    if (kind == FMD_GROW || kind == FMD_SHRINK) {
        if (sw <= 0 || sh <= 0 || sw > w || sh > h) return 0;
        return aes_form_animation(kind,&small,&large,global);
    }
    if (cuis_call6(CUIS_DISPLAY_LOCK,session,1,1,0,0,0) != E_OK) return 0;
    ok = cuis_call6(CUIS_DISPLAY_REDRAW,session,(QUAD)x,(QUAD)y,w,h,0) == E_OK;
    if (cuis_call6(CUIS_DISPLAY_LOCK,session,0,0,0,0,0) != E_OK) ok = FALSE;
    return ok ? 1 : 0;
}
WORD form_dial(WORD kind, WORD sx, WORD sy, WORD sw, WORD sh, WORD x, WORD y, WORD w, WORD h)
{
    return mt_form_dial(kind,sx,sy,sw,sh,x,y,w,h,aes_global);
}
