/* Standard GEM graphics setup and caller-local native cursor semantics.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "gem.h"
#include "gemerror.h"
#include "transport.h"
#include "application.h"
#include "layout.h"
#include "graphics.h"
#include "vdi_service.h"
#include "mforms.h"
static WORD graphics_handle, graphics_wbox, mouse_depth;
static LAYOUT_METRICS graphics_metrics;
static MFORM current, previous, saved;
static BOOL cursor_initialized;
static void cursor_init(void)
{
    if (!cursor_initialized) {
        current = previous = saved = *mform_rs_data[ARROW];
        cursor_initialized = TRUE;
    }
}
void aes_graphics_reset(void)
{
    if (graphics_handle > 0) (void)aes_graphics_call(VDI_SERVICE_CLOSE,graphics_handle,0,0,0);
    graphics_handle = mouse_depth = 0; cursor_initialized = FALSE;
}
void aes_graphics_wind_new(void) { mouse_depth = 0; }
short mt_graf_handle(short *wchar, short *hchar, short *wbox, short *hbox, short *global)
{
    VDI_SERVICE_WORKSTATION ws;
    WORD input[11] = { 1,1,1,1,1,1,1,0,1,1,2 };
    LAYOUT_METRICS metrics;
    WORD box;
    LONG checked;
    if (aes_application_session(global) < 0) return 0;
    if (graphics_handle) {
        checked = aes_graphics_call(VDI_SERVICE_CHECK,graphics_handle,0,0,0);
        if (checked == EIHNDL) graphics_handle = 0;
        else if (checked != E_OK) return 0;
    }
    if (!graphics_handle) {
        if (aes_graphics_call(VDI_SERVICE_OPEN,(uintptr_t)input,(uintptr_t)&ws,0,0) != E_OK) return 0;
        if (ws.handle <= 0 || !aes_workstation_metrics(ws.handle,ws.intout,&metrics,&box)) {
            if (ws.handle > 0) (void)aes_graphics_call(VDI_SERVICE_CLOSE,ws.handle,0,0,0);
            return 0;
        }
        graphics_handle = ws.handle; graphics_metrics = metrics; graphics_wbox = box;
    }
    if (wchar) *wchar = graphics_metrics.wchar;
    if (hchar) *hchar = graphics_metrics.hchar;
    if (wbox) *wbox = graphics_wbox;
    if (hbox) *hbox = graphics_metrics.hbox;
    return graphics_handle;
}
short graf_handle(short *wchar, short *hchar, short *wbox, short *hbox)
{
    return mt_graf_handle(wchar,hchar,wbox,hbox,aes_global);
}
short mt_graf_mouse(short mode, const MFORM *form, short *global)
{
    LONG session = aes_application_session(global);
    MFORM next;
    WORD depth;
    BOOL change = FALSE;
    if (session < 0) return 0;
    cursor_init(); next = current; depth = mouse_depth;
    switch (mode) {
    case USER_DEF: if (!form) return 0; next = *form; change = TRUE; break;
    case M_OFF: if (depth == 32767) return 0; depth++; break;
    case M_ON: if (depth) depth--; break;
    case M_SAVE: saved = current; return 1;
    case M_RESTORE: next = saved; change = TRUE; break;
    case M_PREVIOUS: next = previous; change = TRUE; break;
    default:
        if (mode > USER_DEF) return 1; /* Native unknown high modes are ignored. */
        if (mode < ARROW || mode > OUTLN_CROSS) mode = ARROW;
        next = *mform_rs_data[mode]; change = TRUE;
        break;
    }
    /* Ensure VDI is initialized before publishing a style. A failed syscall
     * cannot replace the local current/previous forms or visibility count. */
    if (!mt_graf_handle(NULL,NULL,NULL,NULL,global) ||
        cuis_call6(CUIS_POINTER_STYLE,session,(uintptr_t)&next,depth,0,0,0) != E_OK) return 0;
    if (change) { previous = current; current = next; }
    mouse_depth = depth;
    return 1;
}
short graf_mouse(short mode, const MFORM *form) { return mt_graf_mouse(mode,form,aes_global); }
