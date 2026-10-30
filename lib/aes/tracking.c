/* Standard GEM tracking bindings; writable state remains caller-local.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "tracking.h"
#include "application.h"
#include "gemerror.h"
#include "resource_context.h"
#include "gemobjop.h"
#include "gemobjbounds.h"

static BOOL rectangle(WORD x, WORD y, WORD w, WORD h)
{
    return w > 0 && h > 0 && (LONG)x+w <= 32767 && (LONG)y+h <= 32767;
}
WORD mt_graf_rubberbox(WORD x, WORD y, WORD wmin, WORD hmin, WORD *w, WORD *h, WORD *global)
{
    AES_TRACK t = { 0 };
    if (!w || !h || wmin < 0 || hmin < 0 || (LONG)x+wmin > 32767 || (LONG)y+hmin > 32767) return 0;
    t.kind = AES_TRACK_RUBBER; t.global = global;
    t.box.g_x = x; t.box.g_y = y; t.box.g_w = wmin; t.box.g_h = hmin;
    if (!aes_graphics_track(&t)) return 0;
    *w = t.result1; *h = t.result2; return 1;
}
WORD graf_rubberbox(WORD x, WORD y, WORD wmin, WORD hmin, WORD *w, WORD *h)
{ return mt_graf_rubberbox(x,y,wmin,hmin,w,h,aes_global); }
WORD mt_graf_dragbox(WORD w, WORD h, WORD x, WORD y, WORD bx, WORD by, WORD bw, WORD bh,
                     WORD *rx, WORD *ry, WORD *global)
{
    AES_TRACK t = { 0 };
    if (!rx || !ry || !rectangle(x,y,w,h) || !rectangle(bx,by,bw,bh)) return 0;
    t.kind = AES_TRACK_DRAG; t.global = global;
    t.box.g_x = x; t.box.g_y = y; t.box.g_w = w; t.box.g_h = h;
    t.bounds.g_x = bx; t.bounds.g_y = by; t.bounds.g_w = bw; t.bounds.g_h = bh;
    if (!aes_graphics_track(&t)) return 0;
    *rx = t.result1; *ry = t.result2; return 1;
}
WORD graf_dragbox(WORD w, WORD h, WORD x, WORD y, WORD bx, WORD by, WORD bw, WORD bh, WORD *rx, WORD *ry)
{ return mt_graf_dragbox(w,h,x,y,bx,by,bw,bh,rx,ry,aes_global); }

WORD mt_graf_slidebox(OBJECT *tree, WORD parent, WORD object, WORD vertical, WORD *global)
{
    AES_TRACK t = { 0 };
    LONG x = 0, y = 0, width, height;
    WORD count, current, steps, i, child, last, border, dummy;
    WORD *parents;
    OBSPEC spec;
    GRECT rect;
    BOOL valid = FALSE;
    if (!tree || parent < 0 || object <= 0 || parent == object || aes_application_session(global) < 0) return 0;
    /* A LASTOB-terminated caller array is the GEM contract. Each child is
     * visited at most once while building a bounded, unambiguous parent map. */
    for (count = 0; count < 4096; count++) if (tree[count].ob_flags & LASTOB) break;
    if (count == 4096 || parent > count || object > count) return 0;
    parents = aes_temporary_alloc((count+1L)*sizeof(*parents));
    if (!parents) return 0;
    for (i = 0; i <= count; i++) parents[i] = NIL;
    for (i = 0; i <= count; i++) {
        child = tree[i].ob_head; last = NIL;
        if (child == NIL) { if (tree[i].ob_tail != NIL) goto done; continue; }
        while (child != i) {
            if (child <= ROOT || child > count || parents[child] != NIL) goto done;
            parents[child] = i; last = child; child = tree[child].ob_next;
        }
        if (last == NIL || last != tree[i].ob_tail) goto done;
    }
    if (parents[object] != parent) goto done;
    current = parent;
    for (steps = 0; ; steps++) {
        if (steps > count || current < 0 || (tree[current].ob_flags & HIDETREE)) goto done;
        x += tree[current].ob_x; y += tree[current].ob_y;
        if (current == ROOT) break;
        current = parents[current];
    }
    ob_sst(tree,parent,&spec,&dummy,&dummy,&dummy,&rect,&border);
    ob_3d_bounds(tree+parent,border,FALSE,&x,&y,&width,&height);
    if (x < -32768L || x > 32767L || y < -32768L || y > 32767L || width <= 0 || width > 32767L ||
        height <= 0 || height > 32767L || !rectangle(x,y,width,height) || (tree[object].ob_flags & HIDETREE)) goto done;
    t.bounds.g_x = x; t.bounds.g_y = y; t.bounds.g_w = width; t.bounds.g_h = height;
    t.tree[0] = tree[parent]; t.tree[1] = tree[object];
    t.kind = AES_TRACK_SLIDE; t.vertical = vertical; t.global = global;
    width = tree[object].ob_width; height = tree[object].ob_height;
    x += tree[object].ob_x; y += tree[object].ob_y;
    if (tree[object].ob_flags & FL3DOBJ) {
        width += 2L*ADJ3DSTD; height += 2L*ADJ3DSTD;
        if (!(tree[parent].ob_flags & FL3DOBJ)) { x -= ADJ3DSTD; y -= ADJ3DSTD; }
    }
    if (width <= 0 || width > 32767L || height <= 0 || height > 32767L ||
        (vertical ? height > t.bounds.g_h : width > t.bounds.g_w) ||
        x < -32768L || x > 32767L || y < -32768L || y > 32767L || !rectangle(x,y,width,height)) goto done;
    t.box.g_x = x; t.box.g_y = y; t.box.g_w = width; t.box.g_h = height;
    valid = TRUE;
done:
    if (aes_resource_discard(parents) != E_OK) valid = FALSE;
    return valid && aes_graphics_track(&t) ? t.result1 : 0;
}
WORD graf_slidebox(OBJECT *tree, WORD parent, WORD object, WORD vertical)
{ return mt_graf_slidebox(tree,parent,object,vertical,aes_global); }
