/* Standard GEM graphics setup and caller-local native cursor semantics.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "libaes.h"
#include "gemerror.h"
#include "transport.h"
#include "application.h"
#include "app_context.h"
#include "layout.h"
#include "graphics.h"
#include "vdi_service.h"
#include "mforms.h"
static void cursor_init(AES_GRAPHICS_STATE *state)
{
    if (!state->cursor_initialized) {
        state->current = state->previous = state->saved = *mform_rs_data[ARROW];
        state->cursor_initialized = TRUE;
    }
}
void aes_graphics_reset(AESGLOBAL *global)
{
    AES_GRAPHICS_STATE *state = &aes_app_context(global)->graphics;
    if (state->handle > 0) (void)aes_graphics_call(VDI_SERVICE_CLOSE,state->handle,0,0,0);
    state->handle = state->mouse_depth = 0; state->cursor_initialized = FALSE;
}
void aes_graphics_wind_new(AESGLOBAL *global)
{
    aes_app_context(global)->graphics.mouse_depth = 0;
}
short mt_graf_handle(short *wchar, short *hchar, short *wbox, short *hbox, AESGLOBAL *global)
{
    AES_GRAPHICS_STATE *state;
    VDI_SERVICE_WORKSTATION ws;
    WORD input[11] = { 1,1,1,1,1,1,1,0,1,1,2 };
    LAYOUT_METRICS metrics;
    WORD box;
    LONG checked;
    if (aes_application_session(global) < 0) return 0;
    state = &aes_app_context(global)->graphics;
    if (state->handle) {
        checked = aes_graphics_call(VDI_SERVICE_CHECK,state->handle,0,0,0);
        if (checked == EIHNDL) state->handle = 0;
        else if (checked != E_OK) return 0;
    }
    if (!state->handle) {
        if (aes_graphics_call(VDI_SERVICE_OPEN,(uintptr_t)input,(uintptr_t)&ws,0,0) != E_OK) return 0;
        if (ws.handle <= 0 || !aes_workstation_metrics(ws.handle,ws.intout,&metrics,&box)) {
            if (ws.handle > 0) (void)aes_graphics_call(VDI_SERVICE_CLOSE,ws.handle,0,0,0);
            return 0;
        }
        state->handle = ws.handle; state->metrics = metrics; state->wbox = box;
    }
    if (wchar) *wchar = state->metrics.wchar;
    if (hchar) *hchar = state->metrics.hchar;
    if (wbox) *wbox = state->wbox;
    if (hbox) *hbox = state->metrics.hbox;
    return state->handle;
}

short mt_graf_mouse(short mode, const MFORM *form, AESGLOBAL *global)
{
    AES_GRAPHICS_STATE *state;
    LONG session = aes_application_session(global);
    MFORM next;
    WORD depth;
    BOOL change = FALSE;
    if (session < 0) return 0;
    state = &aes_app_context(global)->graphics;
    cursor_init(state); next = state->current; depth = state->mouse_depth;
    switch (mode) {
    case USER_DEF: if (!form) return 0; next = *form; change = TRUE; break;
    case M_OFF: if (depth == 32767) return 0; depth++; break;
    case M_ON: if (depth) depth--; break;
    case M_SAVE: state->saved = state->current; return 1;
    case M_RESTORE: next = state->saved; change = TRUE; break;
    case M_PREVIOUS: next = state->previous; change = TRUE; break;
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
    if (change) { state->previous = state->current; state->current = next; }
    state->mouse_depth = depth;
    return 1;
}
