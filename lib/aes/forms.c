/* Standard modal GEM forms in the application's address space.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "libaes.h"
#include "gemerror.h"
#include "application.h"
#include "transport.h"
#include "modal_context.h"
#include "animation.h"


BOOL aes_modal_enter(MODAL_CONTEXT *aes_modal)
{
    MODAL_CONTEXT *c = aes_modal;
    c->display = cuis_call6(CUIS_DISPLAY_LOCK,c->session,1,1,0,0,0) == E_OK;
    if (!c->display) return c->ok = FALSE;
    c->mouse = cuis_call6(CUIS_MOUSE_LOCK,c->session,1,1,0,0,0) == E_OK;
    if (c->mouse) c->keyboard = cuis_call6(CUIS_KEYBOARD_LOCK,c->session,1,0,0,0,0) == E_OK;
    if (!c->mouse || !c->keyboard) {
        c->ok = FALSE; aes_modal_leave(aes_modal);
    }
    return c->ok;
}
void aes_modal_leave(MODAL_CONTEXT *aes_modal)
{
    MODAL_CONTEXT *c = aes_modal;
    if (c->keyboard && cuis_call6(CUIS_KEYBOARD_LOCK,c->session,0,0,0,0,0) != E_OK) c->ok = FALSE;
    if (c->mouse && cuis_call6(CUIS_MOUSE_LOCK,c->session,0,0,0,0,0) != E_OK) c->ok = FALSE;
    if (c->display && cuis_call6(CUIS_DISPLAY_LOCK,c->session,0,0,0,0,0) != E_OK) c->ok = FALSE;
    c->keyboard = c->mouse = c->display = FALSE;
}
WORD aes_modal_wait(MODAL_CONTEXT *aes_modal, WORD *results)
{
    WORD ready;
    if (!aes_modal->ok) return 0;
    ready = mt_evnt_multi(MU_KEYBD|MU_BUTTON,2,255,1,0,0,0,0,0,0,0,0,0,0,NULL,0,
                          &results[0],&results[1],&results[2],&results[3],&results[4],&results[5],aes_modal->global);
    if (!ready) aes_modal->ok = FALSE;
    return ready;
}
WORD aes_modal_edit(MODAL_CONTEXT *aes_modal, OBJECT *tree, WORD object, WORD key, WORD *index, WORD kind)
{
    WORD result;
    if (!aes_modal->ok && kind != ED_END) return 0;
    result = mt_objc_edit(tree,object,key,index,kind,aes_modal->global);
    if (!result) aes_modal->ok = FALSE;
    return result;
}
WORD aes_modal_keybd(MODAL_CONTEXT *aes_modal, OBJECT *tree, WORD object, WORD *key, WORD *next)
{
    BOOL ok;
    WORD result = aes_form_keybd_checked(tree,object,*next,*key,next,key,aes_modal->global,&ok);
    if (!ok) aes_modal->ok = FALSE;
    return result;
}
WORD aes_modal_button(MODAL_CONTEXT *aes_modal, OBJECT *tree, WORD object, WORD clicks, WORD *next)
{
    BOOL ok;
    WORD result = aes_form_button_checked(tree,object,clicks,next,aes_modal->global,&ok);
    if (!ok) aes_modal->ok = FALSE;
    return result;
}
WORD mt_form_do(OBJECT *tree, WORD start, AESGLOBAL *global)
{
    MODAL_CONTEXT c = { 0 };
    MODAL_CONTEXT *aes_modal = &c;
    WORD result;
    if (!tree || start < 0) return 0;
    c.session = aes_application_session(global);
    if (c.session < 0 || !aes_layout_metrics(&c.metrics,global)) return 0;
    c.global = global; c.ok = TRUE;
    if (!aes_frames_suspend(global)) return 0;
    result = aes_form_do_core(aes_modal, tree,start);
    aes_frames_resume(global);
    return c.ok ? result : 0;
}


WORD mt_form_dial(WORD kind, WORD sx, WORD sy, WORD sw, WORD sh,
                   WORD x, WORD y, WORD w, WORD h, AESGLOBAL *global)
{
    LONG session = aes_application_session(global);
    GRECT small, large;
    BOOL ok;
    if (session < 0 || kind < FMD_START || kind > FMD_FINISH) return 0;
    small.g_x = sx; small.g_y = sy; small.g_w = sw; small.g_h = sh;
    large.g_x = x; large.g_y = y; large.g_w = w; large.g_h = h;
    /* No allocation or kernel ownership; retain only local overlay scope. */
    if (kind == FMD_START) return aes_frames_dialog(TRUE,global);
    if (w <= 0 || h <= 0) return 0;
    if (kind == FMD_GROW || kind == FMD_SHRINK) {
        if (sw <= 0 || sh <= 0 || sw > w || sh > h) return 0;
        if (!aes_frames_suspend(global)) return 0;
        ok = aes_form_animation(kind,&small,&large,global);
        aes_frames_resume(global);
        return ok;
    }
    if (cuis_call6(CUIS_DISPLAY_LOCK,session,1,1,0,0,0) != E_OK) return 0;
    ok = cuis_call6(CUIS_DISPLAY_REDRAW,session,(QUAD)x,(QUAD)y,w,h,0) == E_OK;
    if (cuis_call6(CUIS_DISPLAY_LOCK,session,0,0,0,0,0) != E_OK) ok = FALSE;
    if (ok) (void)aes_frames_dialog(FALSE,global);
    return ok ? 1 : 0;
}
