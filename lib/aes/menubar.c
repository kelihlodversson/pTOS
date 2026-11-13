/* Standard menu bars: caller-owned GEM trees, native painting and tracking.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "libaes.h"
#include "object_helpers.h"
#include "gemerror.h"
#include "application.h"
#include "app_context.h"
#include "transport.h"
#include "layout.h"
#include "resource_context.h"
#include "menubar.h"
#include "gemmenubar.h"
#include "gemobjbounds.h"
#include "gemobjtree.h"
#include "vdi_service.h"
#include "string.h"

static const WORD workin[11] = { 1,1,1,1,1,1,1,0,1,1,2 };

BOOL aes_menu_registered(AESGLOBAL *global)
{
    AES_MENU_STATE *retained = &aes_app_context(global)->menu;
    return retained->tree && !retained->busy;
}
BOOL aes_menu_listening(AESGLOBAL *global)
{
    AES_MENU_STATE *retained = &aes_app_context(global)->menu;
    return retained->tree && !retained->busy && !retained->pending;
}
static BOOL paint(AESGLOBAL *global, WORD handle)
{
    AES_MENU_STATE *retained = &aes_app_context(global)->menu;
    WORD points[4] = { 0,0,0,0 };
    points[1] = points[3] = retained->height-1; points[2] = retained->metrics.width-1;
    if (!mt_objc_draw(retained->tree,1,2,0,0,retained->metrics.width,retained->height,global)) return FALSE;
    if (aes_graphics_call(VDI_SERVICE_COLOR,handle,1,0,0) < 0) return FALSE;
    return aes_graphics_call(VDI_SERVICE_LINE,handle,2,(uintptr_t)points,0) == E_OK;
}
static BOOL snapshot_rect(WORD object, GRECT *rect, AESGLOBAL *global)
{
    AES_MENU_STATE *retained = &aes_app_context(global)->menu;
    OBJECT *o = retained->tree+object;
    OBSPEC spec;
    WORD sx, sy;
    LONG x, y, right, bottom, pad;
    if ((o->ob_flags & INDIRECT) && !o->ob_spec.indirect) return FALSE;
    spec = o->ob_flags & INDIRECT ? *o->ob_spec.indirect : o->ob_spec;
    pad = spec.obspec.framesize;
    if (pad > 127) pad -= 256;
    if (pad < 0) pad = -pad;
    pad = 8+3*pad;
    ob_offset(retained->tree,object,&sx,&sy); x = sx; y = sy;
    right = x+o->ob_width+pad; bottom = y+o->ob_height+pad;
    x -= pad; y -= pad;
    if (x < 0) x = 0;
    if (y < retained->height) y = retained->height;
    if (right > retained->metrics.width) right = retained->metrics.width;
    if (bottom > retained->metrics.height) bottom = retained->metrics.height;
    if (x >= right || y >= bottom) return FALSE;
    rect->g_x = x; rect->g_y = y; rect->g_w = right-x; rect->g_h = bottom-y;
    return TRUE;
}
static void *snapshot_allocate(const GRECT *rect, AESGLOBAL *global)
{
    ULONG pixels = (ULONG)((rect->g_w+15L)&~15L)*(UWORD)rect->g_h;
    return pixels && pixels <= VDI_SERVICE_MAX_BITMAP/4 ? aes_temporary_alloc(pixels*4,global) : NULL;
}
static BOOL restore_snapshot(VDI_SERVICE_WORKSTATION *ws, BOOL *opened, const GRECT *rect, void *buffer)
{
    if (!*opened) {
        if (aes_graphics_call(VDI_SERVICE_OPEN,(uintptr_t)workin,(uintptr_t)ws,0,0) != E_OK) return FALSE;
        *opened = TRUE;
    }
    return aes_graphics_call(VDI_SERVICE_SCREEN_BUFFER,ws->handle,(uintptr_t)rect,(uintptr_t)buffer,1) == E_OK;
}
static BOOL change(WORD object, BOOL selected, AESGLOBAL *global, const GRECT *clip)
{
    AES_MENU_STATE *retained = &aes_app_context(global)->menu;
    UWORD state = retained->tree[object].ob_state;
    if (selected) state |= SELECTED; else state &= ~SELECTED;
    return mt_objc_change(retained->tree,object,0,clip ? clip->g_x : 0,clip ? clip->g_y : 0,
                          clip ? clip->g_w : retained->metrics.width,clip ? clip->g_h : retained->height,state,1,global);
}
static WORD title_at(WORD x, WORD y, AESGLOBAL *global)
{
    AES_MENU_STATE *retained = &aes_app_context(global)->menu;
    WORD i, title;
    if (y < 0 || y >= retained->height) return NIL;
    title = aes_object_find(retained->tree,2,1,x,y);
    for (i = 0; i < retained->pairs; i++) if (retained->entries[i].title == title &&
        !(retained->tree[title].ob_state & DISABLED) && !(retained->tree[title].ob_flags & HIDETREE)) return i;
    return NIL;
}
static BOOL flush(LONG session, BOOL block, AESGLOBAL *global)
{
    AES_MENU_STATE *retained = &aes_app_context(global)->menu;
    LONG result;
    if (!retained->pending) return TRUE;
    result = cuis_call6(CUIS_STREAM_WRITE,retained->message[0] == AC_OPEN ? retained->recipient : session,
                        (uintptr_t)retained->message,16,retained->message[0] == AC_OPEN && retained->recipient != session && block,0,0);
    if (result == 16) { retained->pending = FALSE; return TRUE; }
    /* A selected accessory may exit while its queue is full. Its session
     * handle is never recycled; discard instead of opening a replacement. */
    if (retained->message[0] == AC_OPEN && result == EACCDN &&
        cuis_call6(CUIS_PEER_PID,retained->recipient,0,0,0,0,0) < 0) { retained->pending = FALSE; return TRUE; }
    return result == EAGAIN;
}
static BOOL refresh(AESGLOBAL *global)
{
    AES_MENU_STATE *retained = &aes_app_context(global)->menu;
    WORD pairs = mn_bar_index(retained->tree,retained->count,NULL,0);
    MENU_BAR_ENTRY *entries = retained->entries;
    UBYTE *seen;
    if (!pairs) return FALSE;
    if (pairs != retained->pairs) {
        entries = aes_temporary_alloc((LONG)pairs*sizeof(*entries)+(retained->count+7L)/8,global);
        if (!entries) return FALSE;
    }
    seen = (UBYTE *)(entries+pairs);
    if (!mn_bar_admit(retained->tree,retained->count,seen,(retained->count+7L)/8) ||
        !mn_bar_index(retained->tree,retained->count,entries,pairs)) {
        if (entries != retained->entries) (void)aes_resource_discard(entries);
        return FALSE;
    }
    if (entries != retained->entries) (void)aes_resource_discard(retained->entries);
    retained->entries = entries; retained->pairs = pairs;
    return TRUE;
}
LONG aes_menu_flush(AESGLOBAL *global)
{
    AES_MENU_STATE *retained;
    LONG session = aes_application_session(global);
    if (session < 0) return EACCDN;
    retained = &aes_app_context(global)->menu;
    return !retained->tree || flush(session,FALSE,global) ? E_OK : EACCDN;
}
void aes_menu_reset(AESGLOBAL *global)
{
    AES_MENU_STATE *retained = &aes_app_context(global)->menu;
    if (retained->entries) (void)aes_resource_discard(retained->entries);
    memset(retained,0,sizeof(*retained));
}

WORD mt_menu_bar(OBJECT *tree, WORD show, AESGLOBAL *global)
{
    AES_MENU_STATE *retained;
    LONG session = aes_application_session(global);
    AES_MENU_STATE next = { 0 }, old;
    LONG count, bytes;
    WORD original_width = 0, i;
    UBYTE *seen;
    VDI_SERVICE_WORKSTATION ws;
    GRECT rect;
    void *buffer = NULL;
    BOOL locked = FALSE, opened = FALSE, staged = FALSE, installed = FALSE, ok = FALSE;
    if (session < 0 || (show != 0 && show != 1)) return 0;
    retained = &aes_app_context(global)->menu;
    if (retained->busy) return 0;
    if (!flush(session,FALSE,global) || retained->pending) return 0;
    old = *retained;
    if (!show) {
        if (cuis_call6(CUIS_SURFACE_REGISTER,session,0,0,0,0,0) != E_OK) return 0;
        aes_menu_reset(global); return 1;
    }
    if (!tree || !aes_layout_metrics(&next.metrics,global)) return 0;
    for (count = 0; count < 32767; count++) if (tree[count].ob_flags & LASTOB) break;
    if (count == 32767) return 0;
    next.count = count+1; next.tree = tree; next.height = next.metrics.hbox;
    next.pairs = mn_bar_index(tree,next.count,NULL,0);
    if (!next.pairs) return 0;
    bytes = (LONG)next.pairs*sizeof(MENU_BAR_ENTRY)+(next.count+7L)/8;
    next.entries = aes_temporary_alloc(bytes,global);
    if (!next.entries) return 0;
    seen = (UBYTE *)(next.entries+next.pairs);
    if (!mn_bar_admit(tree,next.count,seen,(next.count+7L)/8) ||
        !mn_bar_index(tree,next.count,next.entries,next.pairs)) goto done;
    /* Native menu_bar extends object 1 to the right screen edge. Keep all
     * accumulated origins representable before the shared WORD painter. */
    if (tree[0].ob_x || tree[0].ob_y || tree[1].ob_x < 0 || tree[1].ob_y ||
        tree[1].ob_x >= next.metrics.width || tree[1].ob_height <= 0 || tree[1].ob_height > next.height ||
        tree[2].ob_x < 0 || tree[2].ob_y < 0 || tree[2].ob_width <= 0 || tree[2].ob_height <= 0 ||
        (LONG)tree[1].ob_x+tree[2].ob_x+tree[2].ob_width > next.metrics.width ||
        (LONG)tree[2].ob_y+tree[2].ob_height > next.height) goto done;
    for (i = 0; i < next.count; i++) if ((seen[i >> 3] & (1U << (i & 7))) &&
        (tree[i].ob_flags & INDIRECT) && !tree[i].ob_spec.indirect) {
        if (!(tree[i].ob_flags & HIDETREE)) goto done;
    }
    original_width = tree[1].ob_width;
    tree[1].ob_width = next.metrics.width-tree[1].ob_x;
    if (!mt_objc_draw(tree,1,2,0,0,0,0,global)) { tree[1].ob_width = original_width; goto done; }
    for (i = 0; i < next.pairs; i++) if (!mt_objc_draw(tree,next.entries[i].menu,1,0,0,0,0,global)) {
        tree[1].ob_width = original_width; goto done;
    }
    rect.g_x = rect.g_y = 0; rect.g_w = next.metrics.width; rect.g_h = next.height;
    buffer = snapshot_allocate(&rect,global);
    if (!buffer) { tree[1].ob_width = original_width; goto done; }
    if (cuis_call6(CUIS_DISPLAY_LOCK,session,1,1,0,0,0) != E_OK) { tree[1].ob_width = original_width; goto done; }
    locked = TRUE;
    if (aes_graphics_call(VDI_SERVICE_OPEN,(uintptr_t)workin,(uintptr_t)&ws,0,0) != E_OK) goto rollback_width;
    opened = TRUE;
    if (aes_graphics_call(VDI_SERVICE_SCREEN_BUFFER,ws.handle,(uintptr_t)&rect,(uintptr_t)buffer,0) != E_OK) goto rollback_width;
    if (cuis_call6(CUIS_SURFACE_BEGIN,session,next.height,0,0,0,0) != E_OK) goto rollback_width;
    staged = TRUE; *retained = next; retained->busy = TRUE;
    ok = paint(global,ws.handle);
    if (ok) {
        if (aes_graphics_call(VDI_SERVICE_CLOSE,ws.handle,0,0,0) != E_OK) ok = FALSE;
        else opened = FALSE;
    }
    if (ok) {
        if (cuis_call6(CUIS_SURFACE_END,session,1,0,0,0,0) != E_OK) ok = FALSE;
        else { staged = FALSE; installed = TRUE; retained->busy = FALSE; }
    }
    if (!ok) {
        (void)restore_snapshot(&ws,&opened,&rect,buffer);
        (void)cuis_call6(CUIS_SURFACE_END,session,0,0,0,0,0);
        staged = FALSE; *retained = old;
        tree[1].ob_width = original_width;
    }
    goto done;
rollback_width:
    tree[1].ob_width = original_width;
done:
    if (staged) (void)cuis_call6(CUIS_SURFACE_END,session,0,0,0,0,0);
    if (opened && aes_graphics_call(VDI_SERVICE_CLOSE,ws.handle,0,0,0) != E_OK) ok = FALSE;
    if (locked && cuis_call6(CUIS_DISPLAY_LOCK,session,0,0,0,0,0) != E_OK) ok = FALSE;
    if (buffer) (void)aes_resource_discard(buffer);
    if (installed && retained->entries == next.entries) {
        if (old.entries) (void)aes_resource_discard(old.entries);
        next.entries = NULL;
    }
    if (next.entries) (void)aes_resource_discard(next.entries);
    return ok;
}


typedef struct {
    OBJECT *tree;
    LONG owners[6];
    WORD slots[6], count, first;
    char labels[6][CUIS_ENTRY_BYTES];
} DESK_MENU;

static BOOL desk_menu(LONG session, DESK_MENU *desk, AESGLOBAL *global)
{
    AES_MENU_STATE *retained = &aes_app_context(global)->menu;
    WORD i, box = retained->entries[0].menu, about = retained->tree[box].ob_head;
    WORD separator, width, x, y;
    LONG owner, bytes, needed;
    const char *labels[6];
    OBJECT *tree;
    memset(desk,0,sizeof(*desk));
    if (about < 0 || about >= retained->count) return TRUE; /* Empty custom Desk menu. */
    for (i = 0; i < 6; i++) {
        owner = cuis_call6(CUIS_ENTRY_OWNER,session,i,0,0,0,0);
        if (owner == EFILNF) continue;
        if (owner < 0) return FALSE;
        bytes = cuis_call6(CUIS_ENTRY_READ,session,i,owner,(uintptr_t)desk->labels[desk->count],CUIS_ENTRY_BYTES,0);
        if (bytes == EFILNF || bytes == EAGAIN) continue;
        if (bytes < 0) return FALSE;
        /* Native entries are opaque. Only a nonempty, terminated GEM title
         * belongs in the Desk menu; unrelated binary data is not an error
         * in the application currently tracking its own menus. */
        if (bytes <= 1 || bytes > (LONG)CUIS_ENTRY_BYTES || desk->labels[desk->count][bytes-1] ||
            strlen(desk->labels[desk->count]) != (ULONG)bytes-1) continue;
        desk->owners[desk->count] = owner; desk->slots[desk->count++] = i;
    }
    if (retained->count > 32759 || retained->metrics.hchar <= 0 || retained->metrics.hchar > 4095) return FALSE;
    tree = aes_temporary_alloc(((LONG)retained->count+8)*sizeof(*tree),global);
    if (!tree) return FALSE;
    memcpy(tree,retained->tree,(LONG)retained->count*sizeof(*tree));
    desk->tree = tree;
    /* Keep the caller's resource and its object IDs intact. Only the private
     * tracking tree replaces reserved Desk placeholders with live entries. */
    separator = retained->count; desk->first = separator+1;
    width = tree[box].ob_width;
    ob_offset(tree,box,&x,&y);
    if (x < 0 || x >= retained->metrics.width) return FALSE;
    for (i = 0; i < desk->count; i++) {
        needed = ((LONG)strlen(desk->labels[i])+1)*retained->metrics.wchar;
        if (needed > width) width = needed < retained->metrics.width-x ? needed : retained->metrics.width-x;
    }
    tree[about].ob_height = retained->metrics.hchar;
    for (i = 0; i <= desk->count; i++) {
        tree[separator+i] = tree[about];
        tree[separator+i].ob_flags = 0;
        tree[separator+i].ob_type = G_STRING;
        tree[separator+i].ob_state = i ? 0 : DISABLED;
        tree[separator+i].ob_x = tree[about].ob_x;
        tree[separator+i].ob_y = (i+1)*retained->metrics.hchar;
        tree[separator+i].ob_width = width;
        tree[separator+i].ob_spec.free_string = "--------------------";
        if (i) labels[i-1] = desk->labels[i-1];
    }
    tree[box].ob_width = width;
    tree[retained->count-1].ob_flags &= ~LASTOB;
    tree[separator+desk->count].ob_flags |= LASTOB;
    mn_desk_items(tree,box,about,separator,labels,desk->count,retained->metrics.hchar);
    return TRUE;
}

LONG aes_menu_dispatch(AESGLOBAL *global)
{
    AES_MENU_STATE *retained;
    LONG session = aes_application_session(global), flags;
    VDI_SERVICE_WORKSTATION ws;
    WORD pair, current = NIL, active = NIL, candidate, title, item, mx, my, buttons, modifiers;
    WORD events, mouse[4];
    BOOL locked = FALSE, grabbed = FALSE, opened = FALSE, copied = FALSE, overlay = FALSE, ok = TRUE, pressed;
    GRECT rect;
    void *buffer = NULL;
    DESK_MENU desk = { 0 };
    OBJECT *original;
    if (session < 0) return E_OK;
    retained = &aes_app_context(global)->menu;
    if (!retained->tree || retained->busy) return E_OK;
    if (!flush(session,FALSE,global)) return EACCDN;
    if (retained->pending) return E_OK;
    flags = cuis_call6(CUIS_SURFACE_STATUS,session,0,0,0,0,0);
    if (flags < 0) return flags;
    if (!(flags & CUIS_SURFACE_OWNED) || !(flags & (CUIS_SURFACE_DIRTY|CUIS_SURFACE_POINTER))) return E_OK;
    /* Recheck caller edits before any drawing can invoke an unsupported
     * payload or use stale title/dropdown correspondence. */
    if (!refresh(global)) return EACCDN;
    original = retained->tree;
    retained->busy = TRUE;
    if (cuis_call6(CUIS_DISPLAY_LOCK,session,1,1,0,0,0) != E_OK) { ok = FALSE; goto done; }
    locked = TRUE;
    if (!aes_frames_suspend(global)) { ok = FALSE; goto done; }
    overlay = TRUE;
    flags = cuis_call6(CUIS_SURFACE_STATUS,session,1,0,0,0,0);
    if (flags < 0) { ok = FALSE; goto done; }
    if (!(flags & CUIS_SURFACE_OWNED)) goto done;
    if (aes_graphics_call(VDI_SERVICE_OPEN,(uintptr_t)workin,(uintptr_t)&ws,0,0) != E_OK) { ok = FALSE; goto done; }
    opened = TRUE;
    if ((flags & CUIS_SURFACE_DIRTY) && !paint(global,ws.handle)) { ok = FALSE; goto done; }
    if (!(flags & CUIS_SURFACE_POINTER)) goto done;
    if (!desk_menu(session,&desk,global)) { ok = FALSE; goto done; }
    if (desk.tree) retained->tree = desk.tree;
    if (!mt_graf_mkstate(&mx,&my,&buttons,&modifiers,global)) { ok = FALSE; goto done; }
    pair = title_at(mx,my,global);
    if (pair == NIL) goto done;
    if (cuis_call6(CUIS_MOUSE_LOCK,session,CUIS_MOUSE_FRESH,1,0,0,0) != E_OK) { ok = FALSE; goto done; }
    grabbed = TRUE; pressed = (buttons & 1) != 0;
    for (;;) {
        if (pair != NIL && pair != current) {
            if (copied && aes_graphics_call(VDI_SERVICE_SCREEN_BUFFER,ws.handle,(uintptr_t)&rect,(uintptr_t)buffer,1) != E_OK) { ok = FALSE; break; }
            copied = FALSE;
            if (buffer) { (void)aes_resource_discard(buffer); buffer = NULL; }
            if (active != NIL) { retained->tree[active].ob_state &= ~SELECTED; active = NIL; }
            if (current != NIL && !change(retained->entries[current].title,FALSE,global,NULL)) { ok = FALSE; break; }
            current = pair;
            if (!change(retained->entries[current].title,TRUE,global,NULL) || !snapshot_rect(retained->entries[current].menu,&rect,global)) { ok = FALSE; break; }
            buffer = snapshot_allocate(&rect,global);
            if (!buffer || aes_graphics_call(VDI_SERVICE_SCREEN_BUFFER,ws.handle,(uintptr_t)&rect,(uintptr_t)buffer,0) != E_OK) { ok = FALSE; break; }
            copied = TRUE;
            if (!mt_objc_draw(retained->tree,retained->entries[current].menu,1,rect.g_x,rect.g_y,rect.g_w,rect.g_h,global)) { ok = FALSE; break; }
        }
        candidate = aes_object_find(retained->tree,retained->entries[current].menu,1,mx,my);
        if (candidate == retained->entries[current].menu || candidate < 0 ||
            (retained->tree[candidate].ob_state & DISABLED) || (retained->tree[candidate].ob_flags & HIDETREE)) candidate = NIL;
        if (candidate != active) {
            if (active != NIL && !change(active,FALSE,global,&rect)) { ok = FALSE; break; }
            active = NIL;
            if (candidate != NIL) {
                if (!change(candidate,TRUE,global,&rect)) { ok = FALSE; break; }
                active = candidate;
            }
        }
        events = mt_evnt_multi(MU_M1|MU_BUTTON,1,255,pressed ? 0 : 1,1,mx,my,1,1,
                               0,0,0,0,0,NULL,0,mouse,mouse+1,mouse+2,mouse+3,NULL,NULL,global);
        if (!events) { ok = FALSE; break; }
        mx = mouse[0]; my = mouse[1]; modifiers = mouse[3];
        pair = title_at(mx,my,global);
        if (events & MU_BUTTON) {
            if (!pressed) { pressed = TRUE; continue; }
            pressed = FALSE;
            item = aes_object_find(retained->tree,retained->entries[current].menu,1,mx,my);
            if (pair != NIL) continue;
            if (item > 0 && item != retained->entries[current].menu && !(retained->tree[item].ob_state & DISABLED) && !(retained->tree[item].ob_flags & HIDETREE)) {
                title = retained->entries[current].title;
                memset(retained->message,0,sizeof(retained->message));
                retained->message[0] = MN_SELECTED; retained->message[3] = title; retained->message[4] = item;
                retained->message[5] = (uintptr_t)original >> 16; retained->message[6] = (uintptr_t)original;
                retained->message[7] = retained->entries[current].menu;
                if (current == 0 && desk.tree && item >= desk.first && item < desk.first+desk.count) {
                    retained->message[0] = AC_OPEN; retained->message[1] = global->ap_id;
                    retained->message[3] = 0; retained->message[4] = desk.slots[item-desk.first];
                    retained->message[5] = retained->message[6] = retained->message[7] = 0;
                    retained->recipient = desk.owners[item-desk.first];
                }
                retained->pending = TRUE;
            }
            break;
        }
    }
done:
    if (active != NIL) retained->tree[active].ob_state &= ~SELECTED;
    if (copied && aes_graphics_call(VDI_SERVICE_SCREEN_BUFFER,ws.handle,(uintptr_t)&rect,(uintptr_t)buffer,1) != E_OK) ok = FALSE;
    if (current != NIL && (!retained->pending || !ok || retained->message[0] == AC_OPEN) && !change(retained->entries[current].title,FALSE,global,NULL)) ok = FALSE;
    if (opened && aes_graphics_call(VDI_SERVICE_CLOSE,ws.handle,0,0,0) != E_OK) ok = FALSE;
    if (grabbed && cuis_call6(CUIS_MOUSE_LOCK,session,0,0,0,0,0) != E_OK) ok = FALSE;
    if (locked && cuis_call6(CUIS_DISPLAY_LOCK,session,0,0,0,0,0) != E_OK) ok = FALSE;
    if (buffer && aes_resource_discard(buffer) != E_OK) ok = FALSE;
    if (overlay) aes_frames_resume(global);
    retained->tree = original;
    if (desk.tree) (void)aes_resource_discard(desk.tree);
    retained->busy = FALSE;
    if (!ok) {
        retained->pending = FALSE;
        if (current != NIL) retained->tree[retained->entries[current].title].ob_state &= ~SELECTED;
        (void)cuis_call6(CUIS_SURFACE_STATUS,session,2,0,0,0,0);
        return EACCDN;
    }
    if (current != NIL && retained->pending && retained->message[0] == MN_SELECTED)
        original[retained->entries[current].title].ob_state |= SELECTED;
    return flush(session,TRUE,global) ? E_OK : EACCDN;
}
