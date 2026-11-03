/* Standard GEM file selection over the original AES selector and resources.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "fsel_context.h"
#include "application.h"
#include "transport.h"
#include "resource_context.h"
#include "gemlayout.h"
#include "gemobjbounds.h"
#include "gemobjtree.h"
#include "vdi_service.h"
#include "gemerror.h"
#include "string.h"

FSEL_CONTEXT *aes_fsel;
static BOOL checked_string(const char *text, ULONG size)
{
    ULONG i;
    if (!text) return FALSE;
    for (i = 0; i < size; i++) if (!text[i]) return TRUE;
    return FALSE;
}
void aes_fsel_draw(OBJECT *tree, WORD object, WORD depth)
{
    FSEL_CONTEXT *c = aes_fsel;
    if (c->ok && !mt_objc_draw(tree,object,depth,c->clip.g_x,c->clip.g_y,c->clip.g_w,c->clip.g_h,c->global)) c->ok = FALSE;
}
void aes_fsel_change(OBJECT *tree, WORD object, UWORD state, WORD redraw)
{
    FSEL_CONTEXT *c = aes_fsel;
    if (c->ok && !mt_objc_change(tree,object,0,c->clip.g_x,c->clip.g_y,c->clip.g_w,c->clip.g_h,state,redraw,c->global)) c->ok = FALSE;
}
void aes_fsel_rect(OBJECT *tree, WORD object, GRECT *rect)
{
    LONG x, y, width, height;
    ob_offset(tree,object,&rect->g_x,&rect->g_y);
    x = rect->g_x; y = rect->g_y;
    ob_3d_bounds(tree+object,0,FALSE,&x,&y,&width,&height);
    if (x < -32768 || x > 32767 || y < -32768 || y > 32767 ||
        width <= 0 || width > 32767 || height <= 0 || height > 32767) {
        aes_fsel->ok = FALSE; rect->g_w = rect->g_h = 0; return;
    }
    rect->g_x = x; rect->g_y = y; rect->g_w = width; rect->g_h = height;
}
void aes_fsel_clip(const GRECT *rect) { aes_fsel->clip = *rect; }
void aes_fsel_get_clip(GRECT *rect) { *rect = aes_fsel->clip; }
void aes_fsel_pointer(WORD *x, WORD *y)
{
    if (aes_fsel->ok && !mt_graf_mkstate(x,y,NULL,NULL,aes_fsel->global)) aes_fsel->ok = FALSE;
}
void aes_fsel_cursor(WORD mode)
{
    if (aes_fsel->ok && !mt_graf_mouse(mode,NULL,aes_fsel->global)) aes_fsel->ok = FALSE;
}
WORD aes_fsel_form(OBJECT *tree, WORD start)
{
    WORD object = aes_fsel->ok ? mt_form_do(tree,start,aes_fsel->global) : 0;
    if (!object) aes_fsel->ok = FALSE;
    return object ? object : FSCANCEL;
}
WORD aes_fsel_dial(WORD kind, GRECT *small, GRECT *large)
{
    WORD result;
    if (kind == FMD_FINISH && !aes_fsel->dial) return 1;
    if (kind != FMD_FINISH && !aes_fsel->ok) return 0;
    result = mt_form_dial(kind,small->g_x,small->g_y,small->g_w,small->g_h,
                         large->g_x,large->g_y,large->g_w,large->g_h,aes_fsel->global);
    if (!result) aes_fsel->ok = FALSE;
    if (result && kind == FMD_START) aes_fsel->dial = TRUE;
    if (kind == FMD_FINISH) {
        if (!result) (void)aes_frames_dialog(FALSE);
        aes_fsel->dial = FALSE;
    }
    return result;
}
WORD aes_fsel_slide(OBJECT *tree, WORD parent, WORD child, WORD vertical)
{
    WORD value = aes_fsel->ok ? mt_graf_slidebox(tree,parent,child,vertical,aes_fsel->global) : -1;
    if (value < 0) { aes_fsel->ok = FALSE; return 0; }
    return value;
}
WORD aes_fsel_error(WORD error)
{
    WORD result = aes_fsel->ok ? mt_form_error(error,aes_fsel->global) : 0;
    if (!result) aes_fsel->ok = FALSE;
    return result;
}
WORD aes_fsel_show(WORD string, WORD *number, WORD button)
{
    WORD result;
    (void)number;
    if (!aes_fsel->ok || string != ALFSMEM) return 0;
    result = mt_form_alert(button,rs_fstr[string],aes_fsel->global);
    if (!result) aes_fsel->ok = FALSE;
    return result;
}
WORD aes_fsel_drive(void)
{
    LONG result = (LONG)aes_gemdos_call(0x19,0,0,0);
    if (result < 0 || result > 25) { aes_fsel->ok = FALSE; return 0; }
    return result;
}
void *aes_fsel_get_dta(void)
{
    UQUAD result = aes_gemdos_call(0x2f,0,0,0);
    if ((QUAD)result < 0) { aes_fsel->ok = FALSE; return NULL; }
    return (void *)(uintptr_t)result;
}
void aes_fsel_set_dta(void *dta)
{
    if (!aes_fsel->ok) return;
    /* Fsetdta has no defined scalar result. Verify through its getter. */
    (void)aes_gemdos_call(0x1a,(uintptr_t)dta,0,0);
    if (aes_gemdos_call(0x2f,0,0,0) != (uintptr_t)dta) aes_fsel->ok = FALSE;
}
WORD aes_fsel_first(char *path, WORD attributes)
{
    return aes_fsel->ok ? (WORD)aes_gemdos_call(0x4e,(uintptr_t)path,attributes,0) : EACCDN;
}
WORD aes_fsel_next(void)
{
    return aes_fsel->ok ? (WORD)aes_gemdos_call(0x4f,0,0,0) : EACCDN;
}
void aes_fsel_copy_screen(WORD sx, WORD sy, WORD dx, WORD dy, WORD width, WORD height)
{
    FSEL_CONTEXT *c = aes_fsel;
    VDI_SERVICE_SCREEN_REQUEST request = { 0 };
    WORD clip[4];
    if (!c->ok || width <= 0 || height <= 0) return;
    request.mode = 3;
    request.xy[0] = sx; request.xy[1] = sy;
    request.xy[2] = sx+width-1; request.xy[3] = sy+height-1;
    request.xy[4] = dx; request.xy[5] = dy;
    request.xy[6] = dx+width-1; request.xy[7] = dy+height-1;
    clip[0] = c->clip.g_x; clip[1] = c->clip.g_y;
    clip[2] = clip[0]+c->clip.g_w-1; clip[3] = clip[1]+c->clip.g_h-1;
    if (aes_graphics_call(VDI_SERVICE_CLIP,c->handle,1,(uintptr_t)clip,0) != E_OK ||
        aes_graphics_call(VDI_SERVICE_SCREEN_COPY,c->handle,(uintptr_t)&request,0,0) != E_OK) c->ok = FALSE;
    if (aes_graphics_call(VDI_SERVICE_CLIP,c->handle,0,(uintptr_t)clip,0) != E_OK) c->ok = FALSE;
}

WORD mt_fsel_exinput(char *path, char *selection, WORD *button, const char *title, WORD *global)
{
    FSEL_CONTEXT c = { 0 }, *saved = aes_fsel;
    VDI_SERVICE_WORKSTATION ws;
    WORD input[11] = { 1,1,1,1,1,1,1,0,1,1,2 }, count, i, result = 0, pressed = 0;
    char staged_path[MAXPATHLEN], staged_selection[LEN_ZFNAME];
    void *snapshot = NULL, *original_dta = NULL;
    ULONG bytes;
    BOOL display = FALSE, mouse = FALSE, keyboard = FALSE, overlay = FALSE, opened = FALSE, copied = FALSE, dta = FALSE;
    c.session = aes_application_session(global); c.global = global; c.ok = TRUE;
    if (c.session < 0 || !button || !checked_string(path,sizeof(staged_path)) ||
        !checked_string(selection,sizeof(staged_selection)) || (title && !checked_string(title,128)) ||
        !aes_layout_metrics(&c.metrics,global)) return 0;
    strcpy(staged_path,path); strcpy(staged_selection,selection);
    count = gem_fsel_copy(c.objects,c.teds,c.text);
    c.trees[FSELECTR] = c.objects;
    for (i = 0; i < count; i++)
        rs_obfix_metrics(c.objects,i,c.metrics.wchar,c.metrics.hchar,c.metrics.width);
    c.screen.g_w = c.metrics.width; c.screen.g_h = c.metrics.height;
    c.center.g_x = c.metrics.width/2; c.center.g_y = c.metrics.height/2;
    aes_fsel = &c;
    aes_fsel_start();
    if (c.bounds.g_x < 0 || c.bounds.g_y < 0 || c.bounds.g_w <= 0 || c.bounds.g_h <= 0 ||
        (LONG)c.bounds.g_x+c.bounds.g_w > c.metrics.width || (LONG)c.bounds.g_y+c.bounds.g_h > c.metrics.height) goto done;
    original_dta = aes_fsel_get_dta(); dta = c.ok;
    c.drives = aes_drive_map();
    if (!c.ok || c.drives < 0) goto done;
    bytes = (ULONG)((c.bounds.g_w+15)&~15)*c.bounds.g_h*4;
    if (bytes > VDI_SERVICE_MAX_BITMAP) goto done;
    snapshot = aes_temporary_alloc(bytes);
    if (!snapshot) goto done;
    if (cuis_call6(CUIS_DISPLAY_LOCK,c.session,1,1,0,0,0) != E_OK) goto done;
    display = TRUE;
    if (cuis_call6(CUIS_MOUSE_LOCK,c.session,1,1,0,0,0) != E_OK) goto done;
    mouse = TRUE;
    if (cuis_call6(CUIS_KEYBOARD_LOCK,c.session,1,0,0,0,0) != E_OK) goto done;
    keyboard = TRUE;
    if (!aes_frames_suspend()) goto done;
    overlay = TRUE;
    if (aes_graphics_call(VDI_SERVICE_OPEN,(uintptr_t)input,(uintptr_t)&ws,0,0) != E_OK) goto done;
    opened = TRUE; c.handle = ws.handle;
    if (aes_graphics_call(VDI_SERVICE_SCREEN_BUFFER,ws.handle,(uintptr_t)&c.bounds,(uintptr_t)snapshot,0) != E_OK) goto done;
    copied = TRUE;
    result = aes_fsel_core(staged_path,staged_selection,&pressed,(char *)title);
    if (!c.ok) result = 0;
done:
    if (c.dial) (void)aes_fsel_dial(FMD_FINISH,&c.center,&c.bounds);
    if (dta) {
        (void)aes_gemdos_call(0x1a,(uintptr_t)original_dta,0,0);
        if (aes_gemdos_call(0x2f,0,0,0) != (uintptr_t)original_dta) c.ok = FALSE;
    }
    if (copied && aes_graphics_call(VDI_SERVICE_SCREEN_BUFFER,ws.handle,(uintptr_t)&c.bounds,(uintptr_t)snapshot,1) != E_OK) c.ok = FALSE;
    if (opened && aes_graphics_call(VDI_SERVICE_CLOSE,ws.handle,0,0,0) != E_OK) c.ok = FALSE;
    if (keyboard && cuis_call6(CUIS_KEYBOARD_LOCK,c.session,0,0,0,0,0) != E_OK) c.ok = FALSE;
    if (mouse && cuis_call6(CUIS_MOUSE_LOCK,c.session,0,0,0,0,0) != E_OK) c.ok = FALSE;
    if (display && cuis_call6(CUIS_DISPLAY_LOCK,c.session,0,0,0,0,0) != E_OK) c.ok = FALSE;
    if (snapshot) (void)aes_resource_discard(snapshot);
    if (overlay) aes_frames_resume();
    aes_fsel = saved;
    if (!result || !c.ok || (pressed != 0 && pressed != 1)) return 0;
    strcpy(path,staged_path); strcpy(selection,staged_selection); *button = pressed;
    return 1;
}
WORD fsel_exinput(char *path, char *selection, WORD *button, const char *title)
{ return mt_fsel_exinput(path,selection,button,title,aes_global); }
WORD mt_fsel_input(char *path, char *selection, WORD *button, WORD *global)
{ return mt_fsel_exinput(path,selection,button,NULL,global); }
WORD fsel_input(char *path, char *selection, WORD *button)
{ return mt_fsel_input(path,selection,button,aes_global); }
