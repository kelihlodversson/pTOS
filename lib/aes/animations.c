/* Standard GEM box animation bindings over the original shared AES core.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "animation.h"
#include "application.h"
#include "transport.h"
#include "gemerror.h"

static BOOL valid_rect(const GRECT *r)
{
    return r && r->g_w > 0 && r->g_h > 0 &&
        (LONG)r->g_x+r->g_w <= 32768L && (LONG)r->g_y+r->g_h <= 32768L;
}
static WORD animate(WORD kind, const GRECT *small, const GRECT *large, WORD *global)
{
    LONG session = aes_application_session(global);
    BOOL ok;
    if (session < 0 || !valid_rect(small) || !valid_rect(large) ||
        (kind != AES_ANIMATION_MOVE && (small->g_w > large->g_w || small->g_h > large->g_h))) return 0;
    if (!aes_frames_suspend()) return 0;
    if (cuis_call6(CUIS_DISPLAY_LOCK,session,1,1,0,0,0) != E_OK) { aes_frames_resume(); return 0; }
    ok = aes_form_animation(kind,small,large,global) != 0;
    if (cuis_call6(CUIS_DISPLAY_LOCK,session,0,0,0,0,0) != E_OK) ok = FALSE;
    aes_frames_resume();
    return ok ? 1 : 0;
}
WORD mt_graf_mbox(WORD w, WORD h, WORD sx, WORD sy, WORD dx, WORD dy, WORD *global)
{
    GRECT source = { sx,sy,w,h }, destination = { dx,dy,w,h };
    return animate(AES_ANIMATION_MOVE,&source,&destination,global);
}
WORD graf_mbox(WORD w, WORD h, WORD sx, WORD sy, WORD dx, WORD dy)
{ return mt_graf_mbox(w,h,sx,sy,dx,dy,aes_global); }
WORD mt_graf_growbox_grect(const GRECT *small, const GRECT *large, WORD *global)
{ return animate(FMD_GROW,small,large,global); }
WORD graf_growbox_grect(const GRECT *small, const GRECT *large)
{ return mt_graf_growbox_grect(small,large,aes_global); }
WORD mt_graf_growbox(WORD sx, WORD sy, WORD sw, WORD sh, WORD x, WORD y, WORD w, WORD h, WORD *global)
{
    GRECT small = { sx,sy,sw,sh }, large = { x,y,w,h };
    return mt_graf_growbox_grect(&small,&large,global);
}
WORD graf_growbox(WORD sx, WORD sy, WORD sw, WORD sh, WORD x, WORD y, WORD w, WORD h)
{ return mt_graf_growbox(sx,sy,sw,sh,x,y,w,h,aes_global); }
/* GEM passes the destination (small) first, then the source (large). */
WORD mt_graf_shrinkbox_grect(const GRECT *small, const GRECT *large, WORD *global)
{ return animate(FMD_SHRINK,small,large,global); }
WORD graf_shrinkbox_grect(const GRECT *small, const GRECT *large)
{ return mt_graf_shrinkbox_grect(small,large,aes_global); }
WORD mt_graf_shrinkbox(WORD sx, WORD sy, WORD sw, WORD sh, WORD x, WORD y, WORD w, WORD h, WORD *global)
{
    GRECT small = { sx,sy,sw,sh }, large = { x,y,w,h };
    return mt_graf_shrinkbox_grect(&small,&large,global);
}
WORD graf_shrinkbox(WORD sx, WORD sy, WORD sw, WORD sh, WORD x, WORD y, WORD w, WORD h)
{ return mt_graf_shrinkbox(sx,sy,sw,sh,x,y,w,h,aes_global); }
