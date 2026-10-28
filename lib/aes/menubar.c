/* Standard menu bars: caller-owned GEM trees, native painting and tracking.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "gem.h"
#include "gemerror.h"
#include "application.h"
#include "transport.h"
#include "layout.h"
#include "resource_context.h"
#include "menubar.h"
#include "gemmenubar.h"
#include "gemobjbounds.h"
#include "gemobjtree.h"
#include "vdi_service.h"
#include "string.h"

typedef struct {
    OBJECT *tree;
    MENU_BAR_ENTRY *entries;
    WORD count, pairs, height;
    LAYOUT_METRICS metrics;
    BOOL busy, pending;
    WORD message[8];
} BAR;
static BAR bar;
static WORD workin[11] = { 1,1,1,1,1,1,1,0,1,1,2 };

BOOL aes_menu_registered(void) { return bar.tree && !bar.busy; }
BOOL aes_menu_listening(void) { return bar.tree && !bar.busy && !bar.pending; }
static BOOL paint(WORD *global, WORD handle)
{
    WORD points[4] = { 0,0,0,0 };
    points[1] = points[3] = bar.height-1; points[2] = bar.metrics.width-1;
    if (!mt_objc_draw(bar.tree,1,2,0,0,bar.metrics.width,bar.height,global)) return FALSE;
    if (aes_graphics_call(VDI_SERVICE_COLOR,handle,1,0,0) < 0) return FALSE;
    return aes_graphics_call(VDI_SERVICE_LINE,handle,2,(uintptr_t)points,0) == E_OK;
}
static BOOL snapshot_rect(WORD object, GRECT *rect)
{
    OBJECT *o = bar.tree+object;
    OBSPEC spec;
    WORD sx, sy;
    LONG x, y, right, bottom, pad;
    if ((o->ob_flags & INDIRECT) && !o->ob_spec.indirect) return FALSE;
    spec = o->ob_flags & INDIRECT ? *o->ob_spec.indirect : o->ob_spec;
    pad = spec.obspec.framesize;
    if (pad > 127) pad -= 256;
    if (pad < 0) pad = -pad;
    pad = 8+3*pad;
    ob_offset(bar.tree,object,&sx,&sy); x = sx; y = sy;
    right = x+o->ob_width+pad; bottom = y+o->ob_height+pad;
    x -= pad; y -= pad;
    if (x < 0) x = 0;
    if (y < bar.height) y = bar.height;
    if (right > bar.metrics.width) right = bar.metrics.width;
    if (bottom > bar.metrics.height) bottom = bar.metrics.height;
    if (x >= right || y >= bottom) return FALSE;
    rect->g_x = x; rect->g_y = y; rect->g_w = right-x; rect->g_h = bottom-y;
    return TRUE;
}
static void *snapshot_allocate(const GRECT *rect)
{
    ULONG pixels = (ULONG)((rect->g_w+15L)&~15L)*(UWORD)rect->g_h;
    return pixels && pixels <= VDI_SERVICE_MAX_BITMAP/4 ? aes_temporary_alloc(pixels*4) : NULL;
}
static BOOL change(WORD object, BOOL selected, WORD *global, const GRECT *clip)
{
    UWORD state = bar.tree[object].ob_state;
    if (selected) state |= SELECTED; else state &= ~SELECTED;
    return mt_objc_change(bar.tree,object,0,clip ? clip->g_x : 0,clip ? clip->g_y : 0,
                          clip ? clip->g_w : bar.metrics.width,clip ? clip->g_h : bar.height,state,1,global);
}
static WORD title_at(WORD x, WORD y)
{
    WORD i, title;
    if (y < 0 || y >= bar.height) return NIL;
    title = objc_find(bar.tree,2,1,x,y);
    for (i = 0; i < bar.pairs; i++) if (bar.entries[i].title == title &&
        !(bar.tree[title].ob_state & DISABLED) && !(bar.tree[title].ob_flags & HIDETREE)) return i;
    return NIL;
}
static BOOL flush(LONG session)
{
    LONG result;
    if (!bar.pending) return TRUE;
    result = cuis_call6(CUIS_STREAM_WRITE,session,(uintptr_t)bar.message,16,0,0,0);
    if (result == 16) { bar.pending = FALSE; return TRUE; }
    return result == EAGAIN;
}
static BOOL refresh(void)
{
    WORD pairs = mn_bar_index(bar.tree,bar.count,NULL,0);
    MENU_BAR_ENTRY *entries = bar.entries;
    UBYTE *seen;
    if (!pairs) return FALSE;
    if (pairs != bar.pairs) {
        entries = aes_temporary_alloc((LONG)pairs*sizeof(*entries)+(bar.count+7L)/8);
        if (!entries) return FALSE;
    }
    seen = (UBYTE *)(entries+pairs);
    if (!mn_bar_admit(bar.tree,bar.count,seen,(bar.count+7L)/8) ||
        !mn_bar_index(bar.tree,bar.count,entries,pairs)) {
        if (entries != bar.entries) (void)aes_resource_discard(entries);
        return FALSE;
    }
    if (entries != bar.entries) (void)aes_resource_discard(bar.entries);
    bar.entries = entries; bar.pairs = pairs;
    return TRUE;
}
LONG aes_menu_flush(WORD *global)
{
    LONG session = aes_application_session(global);
    return !bar.tree || (session >= 0 && flush(session)) ? E_OK : EACCDN;
}
void aes_menu_reset(void)
{
    if (bar.entries) (void)aes_resource_discard(bar.entries);
    memset(&bar,0,sizeof(bar));
}

WORD mt_menu_bar(OBJECT *tree, WORD show, WORD *global)
{
    LONG session = aes_application_session(global), registered;
    BAR next = { 0 }, old = bar;
    LONG count, bytes;
    WORD original_width = 0, i;
    UBYTE *seen;
    VDI_SERVICE_WORKSTATION ws;
    GRECT rect;
    void *buffer = NULL;
    BOOL locked = FALSE, opened = FALSE, installed = FALSE, ok = FALSE;
    if (session < 0 || (show != 0 && show != 1) || bar.busy) return 0;
    if (!flush(session) || bar.pending) return 0;
    if (!show) {
        if (cuis_call6(CUIS_SURFACE_REGISTER,session,0,0,0,0,0) != E_OK) return 0;
        aes_menu_reset(); return 1;
    }
    if (!tree || !aes_layout_metrics(&next.metrics,global)) return 0;
    for (count = 0; count < 32767; count++) if (tree[count].ob_flags & LASTOB) break;
    if (count == 32767) return 0;
    next.count = count+1; next.tree = tree; next.height = next.metrics.hbox;
    next.pairs = mn_bar_index(tree,next.count,NULL,0);
    if (!next.pairs) return 0;
    bytes = (LONG)next.pairs*sizeof(MENU_BAR_ENTRY)+(next.count+7L)/8;
    next.entries = aes_temporary_alloc(bytes);
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
    buffer = snapshot_allocate(&rect);
    if (!buffer) { tree[1].ob_width = original_width; goto done; }
    if (cuis_call6(CUIS_DISPLAY_LOCK,session,1,1,0,0,0) != E_OK) { tree[1].ob_width = original_width; goto done; }
    locked = TRUE;
    if (aes_graphics_call(VDI_SERVICE_OPEN,(uintptr_t)workin,(uintptr_t)&ws,0,0) != E_OK) goto rollback_width;
    opened = TRUE;
    if (aes_graphics_call(VDI_SERVICE_SCREEN_BUFFER,ws.handle,(uintptr_t)&rect,(uintptr_t)buffer,0) != E_OK) goto rollback_width;
    registered = cuis_call6(CUIS_SURFACE_REGISTER,session,next.height,0,0,0,0);
    if (registered != E_OK) goto rollback_width;
    installed = TRUE; old = bar; bar = next; bar.busy = TRUE;
    ok = paint(global,ws.handle);
    bar.busy = FALSE;
    if (!ok) {
        (void)aes_graphics_call(VDI_SERVICE_SCREEN_BUFFER,ws.handle,(uintptr_t)&rect,(uintptr_t)buffer,1);
        bar = old;
        (void)cuis_call6(CUIS_SURFACE_REGISTER,session,old.tree ? old.height : 0,0,0,0,0);
        tree[1].ob_width = original_width;
    }
    /* Keep the previous registration until workstation cleanup succeeds. */
    goto done;
rollback_width:
    tree[1].ob_width = original_width;
done:
    if (opened && aes_graphics_call(VDI_SERVICE_CLOSE,ws.handle,0,0,0) != E_OK) {
        if (installed && ok) {
            (void)aes_graphics_call(VDI_SERVICE_SCREEN_BUFFER,ws.handle,(uintptr_t)&rect,(uintptr_t)buffer,1);
            bar = old;
            (void)cuis_call6(CUIS_SURFACE_REGISTER,session,old.tree ? old.height : 0,0,0,0,0);
            tree[1].ob_width = original_width;
        }
        ok = FALSE;
    }
    if (locked && cuis_call6(CUIS_DISPLAY_LOCK,session,0,0,0,0,0) != E_OK) ok = FALSE;
    if (buffer) (void)aes_resource_discard(buffer);
    if (installed && bar.entries == next.entries) {
        if (old.entries) (void)aes_resource_discard(old.entries);
        next.entries = NULL;
    }
    if (next.entries) (void)aes_resource_discard(next.entries);
    return ok;
}
WORD menu_bar(OBJECT *tree, WORD show) { return mt_menu_bar(tree,show,aes_global); }

LONG aes_menu_dispatch(WORD *global)
{
    LONG session = aes_application_session(global), flags;
    VDI_SERVICE_WORKSTATION ws;
    WORD pair, current = NIL, active = NIL, candidate, title, item, mx, my, buttons, modifiers;
    WORD events, mouse[4];
    BOOL locked = FALSE, grabbed = FALSE, opened = FALSE, copied = FALSE, ok = TRUE, pressed;
    GRECT rect;
    void *buffer = NULL;
    if (!bar.tree || bar.busy || session < 0) return E_OK;
    if (!flush(session)) return EACCDN;
    if (bar.pending) return E_OK;
    flags = cuis_call6(CUIS_SURFACE_STATUS,session,0,0,0,0,0);
    if (flags < 0) return flags;
    if (!(flags & CUIS_SURFACE_OWNED) || !(flags & (CUIS_SURFACE_DIRTY|CUIS_SURFACE_POINTER))) return E_OK;
    /* Recheck caller edits before any drawing can invoke an unsupported
     * payload or use stale title/dropdown correspondence. */
    if (!refresh()) return EACCDN;
    bar.busy = TRUE;
    if (cuis_call6(CUIS_DISPLAY_LOCK,session,1,1,0,0,0) != E_OK) { ok = FALSE; goto done; }
    locked = TRUE;
    flags = cuis_call6(CUIS_SURFACE_STATUS,session,1,0,0,0,0);
    if (flags < 0) { ok = FALSE; goto done; }
    if (!(flags & CUIS_SURFACE_OWNED)) goto done;
    if (aes_graphics_call(VDI_SERVICE_OPEN,(uintptr_t)workin,(uintptr_t)&ws,0,0) != E_OK) { ok = FALSE; goto done; }
    opened = TRUE;
    if ((flags & CUIS_SURFACE_DIRTY) && !paint(global,ws.handle)) { ok = FALSE; goto done; }
    if (!(flags & CUIS_SURFACE_POINTER)) goto done;
    if (!mt_graf_mkstate(&mx,&my,&buttons,&modifiers,global)) { ok = FALSE; goto done; }
    pair = title_at(mx,my);
    if (pair == NIL) goto done;
    if (cuis_call6(CUIS_MOUSE_LOCK,session,1,1,0,0,0) != E_OK) { ok = FALSE; goto done; }
    grabbed = TRUE; pressed = (buttons & 1) != 0;
    for (;;) {
        if (pair != NIL && pair != current) {
            if (copied && aes_graphics_call(VDI_SERVICE_SCREEN_BUFFER,ws.handle,(uintptr_t)&rect,(uintptr_t)buffer,1) != E_OK) { ok = FALSE; break; }
            copied = FALSE;
            if (buffer) { (void)aes_resource_discard(buffer); buffer = NULL; }
            if (active != NIL) { bar.tree[active].ob_state &= ~SELECTED; active = NIL; }
            if (current != NIL && !change(bar.entries[current].title,FALSE,global,NULL)) { ok = FALSE; break; }
            current = pair;
            if (!change(bar.entries[current].title,TRUE,global,NULL) || !snapshot_rect(bar.entries[current].menu,&rect)) { ok = FALSE; break; }
            buffer = snapshot_allocate(&rect);
            if (!buffer || aes_graphics_call(VDI_SERVICE_SCREEN_BUFFER,ws.handle,(uintptr_t)&rect,(uintptr_t)buffer,0) != E_OK) { ok = FALSE; break; }
            copied = TRUE;
            if (!mt_objc_draw(bar.tree,bar.entries[current].menu,1,rect.g_x,rect.g_y,rect.g_w,rect.g_h,global)) { ok = FALSE; break; }
        }
        candidate = objc_find(bar.tree,bar.entries[current].menu,1,mx,my);
        if (candidate == bar.entries[current].menu || candidate < 0 ||
            (bar.tree[candidate].ob_state & DISABLED) || (bar.tree[candidate].ob_flags & HIDETREE)) candidate = NIL;
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
        pair = title_at(mx,my);
        if (events & MU_BUTTON) {
            if (!pressed) { pressed = TRUE; continue; }
            pressed = FALSE;
            item = objc_find(bar.tree,bar.entries[current].menu,1,mx,my);
            if (pair != NIL) continue;
            if (item > 0 && item != bar.entries[current].menu && !(bar.tree[item].ob_state & DISABLED) && !(bar.tree[item].ob_flags & HIDETREE)) {
                title = bar.entries[current].title;
                memset(bar.message,0,sizeof(bar.message));
                bar.message[0] = MN_SELECTED; bar.message[3] = title; bar.message[4] = item;
                bar.message[5] = (uintptr_t)bar.tree >> 16; bar.message[6] = (uintptr_t)bar.tree;
                bar.message[7] = bar.entries[current].menu;
                bar.pending = TRUE;
            }
            break;
        }
    }
done:
    if (active != NIL) bar.tree[active].ob_state &= ~SELECTED;
    if (copied && aes_graphics_call(VDI_SERVICE_SCREEN_BUFFER,ws.handle,(uintptr_t)&rect,(uintptr_t)buffer,1) != E_OK) ok = FALSE;
    if (current != NIL && (!bar.pending || !ok) && !change(bar.entries[current].title,FALSE,global,NULL)) ok = FALSE;
    if (opened && aes_graphics_call(VDI_SERVICE_CLOSE,ws.handle,0,0,0) != E_OK) ok = FALSE;
    if (grabbed && cuis_call6(CUIS_MOUSE_LOCK,session,0,0,0,0,0) != E_OK) ok = FALSE;
    if (locked && cuis_call6(CUIS_DISPLAY_LOCK,session,0,0,0,0,0) != E_OK) ok = FALSE;
    if (buffer && aes_resource_discard(buffer) != E_OK) ok = FALSE;
    bar.busy = FALSE;
    if (!ok) {
        bar.pending = FALSE;
        if (current != NIL) bar.tree[bar.entries[current].title].ob_state &= ~SELECTED;
        (void)cuis_call6(CUIS_SURFACE_STATUS,session,2,0,0,0,0);
        return EACCDN;
    }
    return flush(session) ? E_OK : EACCDN;
}
