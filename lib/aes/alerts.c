/* Standard GEM alerts using the shared native parser, geometry and icons.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "gem.h"
#include "gemerror.h"
#include "vdi_service.h"
#include "application.h"
#include "transport.h"
#include "alert_context.h"
#include "string.h"

ALERT_CONTEXT *aes_alert_context;
static WORD *error_global;
static BOOL error_ok;

WORD mt_form_alert(WORD button, const char *text, WORD *global)
{
    ALERT_CONTEXT c = { 0 }, *saved = aes_alert_context;
    VDI_SERVICE_WORKSTATION ws;
    WORD input[11] = { 1,1,1,1,1,1,1,0,1,1,2 }, i, selection = 0;
    GRECT rect;
    LONG session = aes_application_session(global);
    UQUAD memory = 0;
    ULONG bytes;
    BOOL opened = FALSE, display = FALSE, copied = FALSE, ok = TRUE;
    if (session < 0 || button < 0 || button > MAX_BUTNUM || !text ||
        text[0] != '[' || text[1] < '0' || text[1] > '3' || text[2] != ']' || text[3] != '[' ||
        !aes_layout_metrics(&c.metrics,global)) return 0;
    c.screen.g_w = c.metrics.width; c.screen.g_h = c.metrics.height;
    c.trees[DIALERT] = c.objects;
    c.objects[0].ob_type = G_BOX; c.objects[0].ob_state = OUTLINED;
    c.objects[0].ob_flags = FL3DBAK; /* Original DIALERT resource flags. */
    c.objects[0].ob_spec.index = 0x00011100L;
    for (i = 0; i < MAX_LINENUM; i++) {
        c.objects[MSGOFF+i].ob_type = G_STRING;
        c.objects[MSGOFF+i].ob_spec.free_string = c.lines[i];
    }
    for (i = 0; i < MAX_BUTNUM; i++) {
        c.objects[BUTOFF+i].ob_type = G_BUTTON;
        c.objects[BUTOFF+i].ob_spec.free_string = c.buttons[i];
    }
    aes_alert_context = &c;
    aes_alert_tree(button,(char *)text,&rect);
    if (rect.g_w <= 0 || rect.g_h <= 0 || rect.g_w > 32752) goto done;
    bytes = (ULONG)((rect.g_w+15)&~15) * rect.g_h * 4;
    if (bytes > VDI_SERVICE_MAX_BITMAP) goto done;
    memory = aes_memory_call(0x48,bytes);
    if ((QUAD)memory <= 0) { memory = 0; goto done; }
    if (cuis_call6(CUIS_DISPLAY_LOCK,session,1,1,0,0,0) != E_OK) goto done;
    display = TRUE;
    if (aes_graphics_call(VDI_SERVICE_OPEN,(uintptr_t)input,(uintptr_t)&ws,0,0) != E_OK) goto done;
    opened = TRUE;
    if (aes_graphics_call(VDI_SERVICE_SCREEN_BUFFER,ws.handle,(uintptr_t)&rect,memory,0) != E_OK) goto done;
    copied = TRUE;
    if (!mt_objc_draw(c.objects,0,MAX_DEPTH,rect.g_x,rect.g_y,rect.g_w,rect.g_h,global)) goto done;
    selection = mt_form_do(c.objects,0,global);
    if (selection >= BUTOFF && selection < BUTOFF+MAX_BUTNUM) selection -= BUTOFF-1;
    else selection = 0;
done:
    if (copied && aes_graphics_call(VDI_SERVICE_SCREEN_BUFFER,ws.handle,(uintptr_t)&rect,memory,1) != E_OK) ok = FALSE;
    if (opened && aes_graphics_call(VDI_SERVICE_CLOSE,ws.handle,0,0,0) != E_OK) ok = FALSE;
    if (display && cuis_call6(CUIS_DISPLAY_LOCK,session,0,0,0,0,0) != E_OK) ok = FALSE;
    if (memory && (LONG)aes_memory_call(0x49,memory) != E_OK) ok = FALSE;
    aes_alert_context = saved;
    return ok ? selection : 0;
}
WORD form_alert(WORD button, const char *text) { return mt_form_alert(button,text,aes_global); }

WORD aes_error_show(WORD string, WORD *number, WORD button)
{
    char text[64], digits[6];
    const char *source;
    char *out = text;
    WORD n = 0, result;
    UWORD value;
    if (string < AL18ERR || string > ALXXERR) return 0;
    source = rs_fstr[string];
    if (!number) {
        result = mt_form_alert(button,source,error_global);
        error_ok = result != 0;
        return result;
    }
    /* The original generic alert has a single %u, with a WORD argument. */
    value = (UWORD)*number;
    do { digits[n++] = '0'+value%10; value /= 10; } while (value);
    while (*source && out < text+sizeof(text)-1) {
        if (source[0] == '%' && source[1] == 'u') {
            while (n) *out++ = digits[--n];
            source += 2;
        } else *out++ = *source++;
    }
    *out = 0;
    result = mt_form_alert(button,text,error_global);
    error_ok = result != 0;
    return result;
}
WORD mt_form_error(WORD error, WORD *global)
{
    WORD *saved = error_global, result;
    BOOL saved_ok = error_ok, ok;
    if (aes_application_session(global) < 0) return 0;
    error_global = global; error_ok = TRUE;
    result = aes_error_core(error);
    ok = error_ok;
    error_global = saved; error_ok = saved_ok;
    return ok ? result : 0;
}
WORD form_error(WORD error) { return mt_form_error(error,aes_global); }
