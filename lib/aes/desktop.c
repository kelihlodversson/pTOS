/* GEM desktop backgrounds stay in application memory and use the AES painter.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "desktop.h"
#include "application.h"
#include "app_context.h"
#include "transport.h"
#include "resource_context.h"
#include "layout.h"
#include "gemerror.h"

static __inline__ AES_DESKTOP_STATE *desktop_state(AESGLOBAL *global)
{
    return &aes_app_context(global)->desktop;
}

typedef struct desktop_region {
    struct desktop_region *next;
    GRECT rect;
} DESKTOP_REGION;
static void free_regions(DESKTOP_REGION **head)
{
    DESKTOP_REGION *item;
    while ((item = *head) != NULL) {
        *head = item->next; (void)aes_resource_discard(item);
    }
}
BOOL aes_desktop_registered(AESGLOBAL *global)
{
    AES_DESKTOP_STATE *retained = desktop_state(global);
    return retained->installed && !retained->painting;
}
void aes_desktop_reset(AESGLOBAL *global)
{
    AES_DESKTOP_STATE *retained = desktop_state(global);
    free_regions(&retained->regions);
    if (retained->screen_buffer) (void)aes_resource_discard(retained->screen_buffer);
    retained->screen_buffer = NULL; retained->screen_bytes = 0;
    retained->tree = NULL; retained->root = 0; retained->installed = retained->painting = FALSE;
}
BOOL aes_desktop_geometry(GRECT *rect, BOOL work, AESGLOBAL *global)
{
    LONG width, height, top = 0;
    if (aes_application_session(global) < 0) return FALSE;
    width = cuis_call6(CUIS_DISPLAY_INFO,0,0,0,0,0,0);
    height = cuis_call6(CUIS_DISPLAY_INFO,1,0,0,0,0,0);
    if (work) top = cuis_call6(CUIS_DISPLAY_INFO,2,0,0,0,0,0);
    if (work && !top) {
        WORD wbox, hbox;
        /* GEM reserves its menu strip before the first menu_bar() call.
         * Desktops query this area while placing their initial objects. */
        if (!aes_frame_metrics(&wbox,&hbox,NULL,NULL,global)) return FALSE;
        top = hbox;
    }
    if (width <= 0 || width > 32767 || height <= 0 || height > 32767 || top < 0 || top > height) return FALSE;
    rect->g_x = 0; rect->g_y = top; rect->g_w = width; rect->g_h = height-top;
    return TRUE;
}
/* Capture the complete native list before replacing the caller's iterator. */
static BOOL collect_regions(DESKTOP_REGION **head, AESGLOBAL *global)
{
    LONG session = aes_application_session(global), state[4], result;
    ULONG index;
    DESKTOP_REGION *item, **tail = head;
    if (session < 0) return FALSE;
    for (index = 0; ; index++) {
        result = cuis_call6(CUIS_BACKGROUND_REGION,session,index,(uintptr_t)&state[0],
                            (uintptr_t)&state[1],(uintptr_t)&state[2],(uintptr_t)&state[3]);
        if (!result) return TRUE;
        if (result < 0 || state[0] < 0 || state[0] > 32767 || state[1] < 0 || state[1] > 32767 ||
            state[2] <= 0 || state[2] > 32767 || state[3] <= 0 || state[3] > 32767 ||
            state[0]+state[2] > 32767 || state[1]+state[3] > 32767) return FALSE;
        item = aes_temporary_alloc(sizeof(*item));
        if (!item) return FALSE;
        item->next = NULL;
        item->rect.g_x = state[0]; item->rect.g_y = state[1];
        item->rect.g_w = state[2]; item->rect.g_h = state[3];
        *tail = item; tail = &item->next;
    }
}
WORD aes_desktop_region(BOOL first, GRECT *rect, AESGLOBAL *global)
{
    AES_DESKTOP_STATE *retained = desktop_state(global);
    DESKTOP_REGION *snapshot = NULL, *item;
    if (first) {
        BOOL ok;
        if (!mt_wind_update(BEG_UPDATE,global)) return 0;
        ok = collect_regions(&snapshot,global);
        if (!mt_wind_update(END_UPDATE,global)) ok = FALSE;
        if (!ok) { free_regions(&snapshot); return 0; }
        free_regions(&retained->regions); retained->regions = snapshot;
    }
    item = retained->regions;
    if (item) {
        *rect = item->rect; retained->regions = item->next;
        (void)aes_resource_discard(item);
    } else rect->g_x = rect->g_y = rect->g_w = rect->g_h = 0;
    return 1;
}
LONG aes_desktop_dispatch(AESGLOBAL *global)
{
    AES_DESKTOP_STATE *retained;
    LONG session = aes_application_session(global), status, result = E_OK;
    DESKTOP_REGION *snapshot = NULL, *item;
    OBJECT fallback = { NIL,NIL,NIL,G_BOX,LASTOB,NORMAL,{ 0x00001143L },0,0,0,0 };
    OBJECT *tree;
    GRECT full;
    if (session < 0) return session;
    retained = desktop_state(global);
    tree = retained->tree;
    if (!retained->installed || retained->painting || aes_frames_suspended(global)) return E_OK;
    status = cuis_call6(CUIS_BACKGROUND,session,2,0,0,0,0);
    if (status < 0) return status;
    if ((status & 3) != 3) return E_OK;
    result = cuis_call6(CUIS_DISPLAY_LOCK,session,1,0,0,0,0);
    if (result != E_OK) return result;
    retained->painting = TRUE;
    /* Recheck after acquiring ownership: a peer may have installed a tree. */
    status = cuis_call6(CUIS_BACKGROUND,session,2,0,0,0,0);
    if (status < 0) result = status;
    else if ((status & 3) == 3) {
        if (!tree) {
            if (!aes_desktop_geometry(&full,FALSE,global)) result = EINVFN;
            else { fallback.ob_width = full.g_w; fallback.ob_height = full.g_h; tree = &fallback; }
        }
        /* Clear before callbacks can invalidate the background again. */
        if (result == E_OK) result = cuis_call6(CUIS_BACKGROUND,session,3,0,0,0,0);
        if (result == E_OK && !collect_regions(&snapshot,global)) result = ENSMEM;
        for (item = snapshot; item && result == E_OK; item = item->next)
            if (!mt_objc_draw(tree,retained->tree ? retained->root : ROOT,8,
                item->rect.g_x,item->rect.g_y,item->rect.g_w,item->rect.g_h,global)) result = EACCDN;
        if (result != E_OK) (void)cuis_call6(CUIS_BACKGROUND,session,4,0,0,0,0);
    }
    free_regions(&snapshot); retained->painting = FALSE;
    if (cuis_call6(CUIS_DISPLAY_LOCK,session,0,0,0,0,0) != E_OK) result = EACCDN;
    return result;
}
WORD aes_desktop_set(OBJECT *tree, WORD root, AESGLOBAL *global)
{
    AES_DESKTOP_STATE *retained;
    LONG session = aes_application_session(global), result;
    if (session < 0) return 0;
    retained = desktop_state(global);
    if (retained->painting || (tree && root < 0) ||
        (tree && !mt_objc_draw(tree,root,8,0,0,0,0,global)) ||
        !mt_wind_update(BEG_UPDATE,global)) return 0;
    result = cuis_call6(CUIS_BACKGROUND,session,1,0,0,0,0);
    if (result == E_OK) {
        retained->tree = tree; retained->root = tree ? root : ROOT; retained->installed = TRUE;
        free_regions(&retained->regions);
    }
    if (!mt_wind_update(END_UPDATE,global)) result = EACCDN;
    return result == E_OK;
}
WORD aes_desktop_get(OBJECT **tree, WORD *root, AESGLOBAL *global)
{
    AES_DESKTOP_STATE *retained;
    LONG session = aes_application_session(global), status;
    if (session < 0) return 0;
    retained = desktop_state(global);
    status = cuis_call6(CUIS_BACKGROUND,session,2,0,0,0,0);
    if (status < 0 || (retained->installed && !(status & 1)) ||
        (!retained->installed && cuis_call6(CUIS_DISPLAY_INFO,3,0,0,0,0,0) != 0)) return 0;
    if (tree) *tree = retained->tree;
    if (root) *root = retained->root;
    return 1;
}
void *aes_screen_buffer(LONG *bytes, AESGLOBAL *global)
{
    AES_DESKTOP_STATE *retained;
    LAYOUT_METRICS m;
    LONG size;
    void *replacement;
    if (!aes_layout_metrics(&m,global) || m.wchar <= 0 || m.hchar <= 0 ||
        (LONG)m.wchar*m.hchar > 1024L*1024L/(640*4)) return NULL;
    retained = desktop_state(global);
    /* Native AES reserves 640 character cells for menu/alert saves. This
     * private buffer permits four bytes per pixel, including true colour. */
    size = 640L*m.wchar*m.hchar*4;
    if (size > retained->screen_bytes) {
        replacement = aes_temporary_alloc(size);
        if (!replacement) return NULL;
        if (retained->screen_buffer) (void)aes_resource_discard(retained->screen_buffer);
        retained->screen_buffer = replacement; retained->screen_bytes = size;
    }
    *bytes = retained->screen_bytes;
    return retained->screen_buffer;
}
