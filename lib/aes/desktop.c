/* GEM desktop backgrounds stay in application memory and use the AES painter.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "desktop.h"
#include "application.h"
#include "transport.h"
#include "resource_context.h"
#include "layout.h"
#include "gemerror.h"

typedef struct desktop_region {
    struct desktop_region *next;
    GRECT rect;
} DESKTOP_REGION;
static OBJECT *desktop_tree;
static WORD desktop_root;
static BOOL installed, painting;
static DESKTOP_REGION *regions;
static void *screen_buffer;
static LONG screen_bytes;

static void free_regions(DESKTOP_REGION **head)
{
    DESKTOP_REGION *item;
    while ((item = *head) != NULL) {
        *head = item->next; (void)aes_resource_discard(item);
    }
}
BOOL aes_desktop_registered(void) { return installed && !painting; }
void aes_desktop_reset(void)
{
    free_regions(&regions);
    if (screen_buffer) (void)aes_resource_discard(screen_buffer);
    screen_buffer = NULL; screen_bytes = 0;
    desktop_tree = NULL; desktop_root = 0; installed = painting = FALSE;
}
BOOL aes_desktop_geometry(GRECT *rect, BOOL work, WORD *global)
{
    LONG width, height, top = 0;
    if (aes_application_session(global) < 0) return FALSE;
    width = cuis_call6(CUIS_DISPLAY_INFO,0,0,0,0,0,0);
    height = cuis_call6(CUIS_DISPLAY_INFO,1,0,0,0,0,0);
    if (work) top = cuis_call6(CUIS_DISPLAY_INFO,2,0,0,0,0,0);
    if (width <= 0 || width > 32767 || height <= 0 || height > 32767 || top < 0 || top > height) return FALSE;
    rect->g_x = 0; rect->g_y = top; rect->g_w = width; rect->g_h = height-top;
    return TRUE;
}
/* Capture the complete native list before replacing the caller's iterator. */
static BOOL collect_regions(DESKTOP_REGION **head, WORD *global)
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
WORD aes_desktop_region(BOOL first, GRECT *rect, WORD *global)
{
    DESKTOP_REGION *snapshot = NULL, *item;
    if (first) {
        BOOL ok;
        if (!mt_wind_update(BEG_UPDATE,global)) return 0;
        ok = collect_regions(&snapshot,global);
        if (!mt_wind_update(END_UPDATE,global)) ok = FALSE;
        if (!ok) { free_regions(&snapshot); return 0; }
        free_regions(&regions); regions = snapshot;
    }
    item = regions;
    if (item) {
        *rect = item->rect; regions = item->next;
        (void)aes_resource_discard(item);
    } else rect->g_x = rect->g_y = rect->g_w = rect->g_h = 0;
    return 1;
}
LONG aes_desktop_dispatch(WORD *global)
{
    LONG session = aes_application_session(global), status, result = E_OK;
    DESKTOP_REGION *snapshot = NULL, *item;
    OBJECT fallback = { NIL,NIL,NIL,G_BOX,LASTOB,NORMAL,{ 0x00001143L },0,0,0,0 };
    OBJECT *tree = desktop_tree;
    GRECT full;
    if (!installed || painting || aes_frames_suspended()) return E_OK;
    status = cuis_call6(CUIS_BACKGROUND,session,2,0,0,0,0);
    if (status < 0) return status;
    if ((status & 3) != 3) return E_OK;
    result = cuis_call6(CUIS_DISPLAY_LOCK,session,1,0,0,0,0);
    if (result != E_OK) return result;
    painting = TRUE;
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
            if (!mt_objc_draw(tree,desktop_tree ? desktop_root : ROOT,8,
                item->rect.g_x,item->rect.g_y,item->rect.g_w,item->rect.g_h,global)) result = EACCDN;
        if (result != E_OK) (void)cuis_call6(CUIS_BACKGROUND,session,4,0,0,0,0);
    }
    free_regions(&snapshot); painting = FALSE;
    if (cuis_call6(CUIS_DISPLAY_LOCK,session,0,0,0,0,0) != E_OK) result = EACCDN;
    return result;
}
WORD aes_desktop_set(OBJECT *tree, WORD root, WORD *global)
{
    LONG session = aes_application_session(global), result;
    if (session < 0 || painting || (tree && root < 0) ||
        (tree && !mt_objc_draw(tree,root,8,0,0,0,0,global)) ||
        !mt_wind_update(BEG_UPDATE,global)) return 0;
    result = cuis_call6(CUIS_BACKGROUND,session,1,0,0,0,0);
    if (result == E_OK) {
        desktop_tree = tree; desktop_root = tree ? root : ROOT; installed = TRUE;
        free_regions(&regions);
    }
    if (!mt_wind_update(END_UPDATE,global)) result = EACCDN;
    return result == E_OK;
}
WORD aes_desktop_get(OBJECT **tree, WORD *root, WORD *global)
{
    LONG session = aes_application_session(global), status;
    if (session < 0) return 0;
    status = cuis_call6(CUIS_BACKGROUND,session,2,0,0,0,0);
    if (status < 0 || (installed && !(status & 1)) ||
        (!installed && cuis_call6(CUIS_DISPLAY_INFO,3,0,0,0,0,0) != 0)) return 0;
    if (tree) *tree = desktop_tree;
    if (root) *root = desktop_root;
    return 1;
}
void *aes_screen_buffer(LONG *bytes, WORD *global)
{
    LAYOUT_METRICS m;
    LONG size;
    void *replacement;
    if (!aes_layout_metrics(&m,global) || m.wchar <= 0 || m.hchar <= 0 ||
        (LONG)m.wchar*m.hchar > 1024L*1024L/(640*4)) return NULL;
    /* Native AES reserves 640 character cells for menu/alert saves. This
     * private buffer permits four bytes per pixel, including true colour. */
    size = 640L*m.wchar*m.hchar*4;
    if (size > screen_bytes) {
        replacement = aes_temporary_alloc(size);
        if (!replacement) return NULL;
        if (screen_buffer) (void)aes_resource_discard(screen_buffer);
        screen_buffer = replacement; screen_bytes = size;
    }
    *bytes = screen_bytes;
    return screen_buffer;
}
