/* Classic GEM object painting in the caller, over existing VDI primitives.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "gem.h"
#include "gemerror.h"
#include "vdi_service.h"
#include "transport.h"
#include "application.h"

typedef struct {
    WORD handle, top, cellw, cellh, default_top;
    LONG left, upper, right, lower;
    BOOL ok;
} PAINTER;

static OBSPEC specification(const OBJECT *object)
{
    return object->ob_flags & INDIRECT ? *object->ob_spec.indirect : object->ob_spec;
}
static BOOL text_type(WORD type)
{
    return type == G_TEXT || type == G_BOXTEXT || type == G_FTEXT || type == G_FBOXTEXT;
}
static LONG length(const char *text);
static BOOL supported(const OBJECT *object)
{
    WORD type = object->ob_type & 255;
    OBSPEC spec;
    if (object->ob_flags & FL3DMASK) return FALSE;
    spec = specification(object);
    if (spec.index == -1) return TRUE;
    if (text_type(type))
        return spec.tedinfo && spec.tedinfo->te_ptext &&
               (spec.tedinfo->te_font == IBM || spec.tedinfo->te_font == SMALL) &&
               spec.tedinfo->te_just >= 0 && spec.tedinfo->te_just <= 2 &&
               ((type != G_FTEXT && type != G_FBOXTEXT) ||
                (spec.tedinfo->te_ptmplt && length(spec.tedinfo->te_ptmplt) <= 256));
    switch (type) {
    case G_BOX: case G_IBOX: case G_BOXCHAR: return TRUE;
    case G_STRING: case G_BUTTON: case G_TITLE: return spec.free_string != NULL;
    case G_USERDEF: return spec.userblk && spec.userblk->ub_code;
    default: return FALSE;
    }
}
static BOOL draw_call(PAINTER *p, VDI_SERVICE_DRAW_REQUEST *request, VDI_SERVICE_DRAW_RESULT *result)
{
    if (!p->ok) return FALSE;
    p->ok = aes_graphics_call(VDI_SERVICE_DRAW,p->handle,(uintptr_t)request,(uintptr_t)result,0) == E_OK;
    return p->ok;
}
static void attribute(PAINTER *p, WORD code, WORD value)
{
    VDI_SERVICE_DRAW_REQUEST request = { 0 };
    VDI_SERVICE_DRAW_RESULT result;
    request.code = code; request.nintin = 1; request.intin[0] = value;
    draw_call(p,&request,&result);
}
static void fill(PAINTER *p, LONG x, LONG y, LONG w, LONG h, WORD mode, WORD color, WORD pattern)
{
    VDI_SERVICE_DRAW_REQUEST request = { 0 };
    VDI_SERVICE_DRAW_RESULT result;
    LONG right = x + w - 1, lower = y + h - 1;
    if (w <= 0 || h <= 0 || x > p->right || y > p->lower || right < p->left || lower < p->upper) return;
    if (x < p->left) x = p->left;
    if (y < p->upper) y = p->upper;
    if (right > p->right) right = p->right;
    if (lower > p->lower) lower = p->lower;
    attribute(p,32,mode); attribute(p,25,color);
    attribute(p,23,pattern == IP_SOLID ? FIS_SOLID : pattern == IP_HOLLOW ? FIS_HOLLOW : FIS_PATTERN);
    if (pattern > IP_HOLLOW && pattern < IP_SOLID) attribute(p,24,pattern);
    request.code = 114; request.nptsin = 2;
    request.ptsin[0] = x; request.ptsin[1] = y;
    request.ptsin[2] = right; request.ptsin[3] = lower;
    draw_call(p,&request,&result);
}
static void border(PAINTER *p, LONG x, LONG y, LONG w, LONG h, WORD thickness, WORD color)
{
    LONG i, inset;
    for (i = 0; i < (thickness < 0 ? -thickness : thickness); i++) {
        inset = thickness < 0 ? -i-1 : i;
        fill(p,x+inset,y+inset,w-2*inset,1,MD_REPLACE,color,IP_SOLID);
        fill(p,x+inset,y+h-inset-1,w-2*inset,1,MD_REPLACE,color,IP_SOLID);
        fill(p,x+inset,y+inset,1,h-2*inset,MD_REPLACE,color,IP_SOLID);
        fill(p,x+w-inset-1,y+inset,1,h-2*inset,MD_REPLACE,color,IP_SOLID);
    }
}
static void font(PAINTER *p, WORD which)
{
    VDI_SERVICE_DRAW_REQUEST request = { 0 };
    VDI_SERVICE_DRAW_RESULT result;
    request.code = 12; request.nptsin = 1;
    request.ptsin[1] = which == SMALL ? 4 : p->default_top;
    if (draw_call(p,&request,&result)) {
        p->top = result.ptsout[1]; p->cellw = result.ptsout[2]; p->cellh = result.ptsout[3];
        if (p->cellw <= 0 || p->cellh <= 0) p->ok = FALSE;
    }
}
static LONG length(const char *text)
{
    LONG n = 0;
    while (n < 32767 && text[n]) n++;
    return n;
}
static void text(PAINTER *p, const char *string, LONG x, LONG y, LONG w, LONG h,
                 WORD justification, WORD which, WORD color, WORD mode)
{
    VDI_SERVICE_DRAW_REQUEST request = { 0 };
    VDI_SERVICE_DRAW_RESULT result;
    LONG i, count = length(string), at = 0, delta;
    font(p,which);
    delta = w - count * p->cellw;
    if (justification >= 3) {
        if (justification == 4) x += delta/2;
        y += (h-p->cellh)/2;
    } else {
        if (delta > 0) x += justification == 2 ? (delta+1)/2 : justification == 1 ? delta : 0;
        if (h > p->cellh) y += (h-p->cellh+1)/2;
    }
    y += p->top;
    attribute(p,32,mode); attribute(p,22,color);
    while (at < count && p->ok) {
        request.code = 8; request.nptsin = 1;
        request.nintin = count-at > VDI_SERVICE_MAX_TEXT ? VDI_SERVICE_MAX_TEXT : count-at;
        for (i = 0; i < VDI_SERVICE_MAX_TEXT; i++) request.intin[i] = i < request.nintin ? (UBYTE)string[at+i] : 0;
        if (x >= -32767 && x <= 32767 && y >= -32767 && y <= 32767) {
            request.ptsin[0] = x; request.ptsin[1] = y;
            draw_call(p,&request,&result);
        }
        at += request.nintin; x += request.nintin*p->cellw;
    }
}
static BOOL format(const TEDINFO *ted, char output[257])
{
    LONG i, n = length(ted->te_ptmplt), at, count = length(ted->te_ptext);
    if (n > 256) return FALSE;
    if (ted->te_ptext[0] == '@') count = 0;
    at = ted->te_just == 1 ? count-1 : 0;
    for (i = 0; i < n; i++) {
        LONG index = ted->te_just == 1 ? n-i-1 : i;
        output[index] = ted->te_ptmplt[index];
        if (output[index] == '_' && at >= 0 && at < count) {
            output[index] = ted->te_ptext[at]; at += ted->te_just == 1 ? -1 : 1;
        }
    }
    output[n] = 0;
    return TRUE;
}
static void cross_line(PAINTER *p, LONG x, LONG y, LONG x2, LONG y2)
{
    WORD points[4];
    if (x < -32767 || x > 32767 || y < -32767 || y > 32767 ||
        x2 < -32767 || x2 > 32767 || y2 < -32767 || y2 > 32767) { p->ok = FALSE; return; }
    points[0] = x; points[1] = y; points[2] = x2; points[3] = y2;
    attribute(p,32,MD_TRANS);
    if (p->ok) p->ok = aes_graphics_call(VDI_SERVICE_COLOR,p->handle,WHITE,0,0) == E_OK;
    if (p->ok) p->ok = aes_graphics_call(VDI_SERVICE_LINE,p->handle,2,(uintptr_t)points,0) == E_OK;
}
static void paint(PAINTER *p, OBJECT *tree, WORD object, LONG x, LONG y, UWORD previous, BOOL toggle)
{
    OBJECT *o = &tree[object];
    WORD type = o->ob_type & 255, th = 0, color, pattern, border_color, text_color, mode;
    LONG w = o->ob_width, h = o->ob_height, inset, outset;
    UWORD state = o->ob_state, colors = 0x1100;
    OBSPEC spec;
    const char *string = NULL;
    char formatted[257], character[2];
    TEDINFO *ted = NULL;
    if ((o->ob_flags & HIDETREE) || w <= 0 || h <= 0) return;
    spec = specification(o);
    if (spec.index == -1) return;
    if (type == G_BOX || type == G_IBOX || type == G_BOXCHAR) {
        colors = (UWORD)spec.index; th = (SBYTE)((ULONG)spec.index >> 16);
    } else if (type == G_BUTTON) th = -1 - !!(o->ob_flags & EXIT) - !!(o->ob_flags & DEFAULT);
    else if (type == G_TITLE) th = 1;
    else if (text_type(type)) { ted = spec.tedinfo; colors = ted->te_color; th = ted->te_thickness; }
    outset = state & OUTLINED ? 3 : 3*(th < 0 ? -th : th);
    if (x-outset > p->right || y-outset > p->lower ||
        x+w+outset <= p->left || y+h+outset <= p->upper) return;
    inset = th > 0 ? th : 0;
    if (toggle && type != G_USERDEF) {
        fill(p,x+inset,y+inset,w-2*inset,h-2*inset,MD_XOR,BLACK,IP_SOLID);
        return;
    }
    border_color = (colors >> 12) & 15; text_color = (colors >> 8) & 15;
    pattern = (colors >> 4) & 7; color = colors & 15;
    mode = colors & 128 ? MD_REPLACE : MD_TRANS;
    if (type == G_BOX || type == G_BOXCHAR || type == G_IBOX || type == G_BUTTON || type == G_BOXTEXT || type == G_FBOXTEXT) {
        border(p,x,y,w,h,th,border_color);
        if (type != G_IBOX) fill(p,x+inset,y+inset,w-2*inset,h-2*inset,MD_REPLACE,color,pattern);
    }
    if (ted) {
        string = ted->te_ptext;
        if (type == G_FTEXT || type == G_FBOXTEXT) {
            if (!format(ted,formatted)) { p->ok = FALSE; return; }
            string = formatted;
        }
        text(p,string,x+inset,y+inset,w-2*inset,h-2*inset,ted->te_just,ted->te_font,text_color,mode);
    } else if (type == G_BOXCHAR) {
        character[0] = (UBYTE)((ULONG)spec.index >> 24); character[1] = 0;
        text(p,character,x+inset,y+inset,w-2*inset,h-2*inset,2,IBM,text_color,mode);
    } else if (type == G_STRING || type == G_TITLE || type == G_BUTTON) {
        text(p,spec.free_string,x,y,w,h,type == G_BUTTON ? 4 : 3,IBM,BLACK,MD_TRANS);
    } else if (type == G_USERDEF) {
        PARMBLK pb;
        pb.pb_tree = tree; pb.pb_obj = object;
        pb.pb_prevstate = previous; pb.pb_currstate = state;
        pb.pb_x = x; pb.pb_y = y; pb.pb_w = w; pb.pb_h = h;
        pb.pb_xc = p->left; pb.pb_yc = p->upper;
        pb.pb_wc = p->right-p->left+1; pb.pb_hc = p->lower-p->upper+1;
        pb.pb_parm = spec.userblk->ub_parm;
        state = spec.userblk->ub_code(&pb);
    }
    if (state & OUTLINED) {
        border(p,x-3,y-3,w+6,h+6,1,BLACK);
        border(p,x-2,y-2,w+4,h+4,2,WHITE);
    }
    if ((state & SHADOWED) && th) {
        LONG size = th < 0 ? -th : th;
        fill(p,x+inset,y+inset+h-2*inset+size,w-2*inset+size,2*size,MD_REPLACE,border_color,IP_SOLID);
        fill(p,x+inset+w-2*inset+size,y+inset,2*size,h-2*inset+3*size,MD_REPLACE,border_color,IP_SOLID);
    }
    if (state & CHECKED) text(p,"\010",x+inset+2,y+inset,w,0,0,IBM,BLACK,MD_TRANS);
    if (state & CROSSED) {
        cross_line(p,x+inset,y+inset,x+w-inset-1,y+h-inset-1);
        cross_line(p,x+inset,y+h-inset-1,x+w-inset-1,y+inset);
    }
    if (state & DISABLED) fill(p,x+inset,y+inset,w-2*inset,h-2*inset,MD_TRANS,WHITE,IP_4PATT);
    if (state & SELECTED) fill(p,x+inset,y+inset,w-2*inset,h-2*inset,MD_XOR,BLACK,IP_SOLID);
}
/* Bounded recursion follows GEM's MAX_DEPTH, retaining sibling draw order.
 * Preflight checks the same visible subtree before any VDI work or painting. */
static BOOL walk(PAINTER *p, OBJECT *tree, WORD object, WORD depth, LONG x, LONG y)
{
    WORD child;
    OBJECT *o = &tree[object];
    if (o->ob_flags & HIDETREE) return TRUE;
    if (!supported(o)) return FALSE;
    x += o->ob_x; y += o->ob_y;
    if (p) {
        paint(p,tree,object,x,y,o->ob_state,FALSE);
        if (!p->ok) return FALSE;
    }
    if (depth && o->ob_head != NIL) {
        for (child = o->ob_head; child != object; child = tree[child].ob_next) {
            if (child < 0 || !walk(p,tree,child,depth-1,x,y)) return FALSE;
            if (p && !p->ok) return FALSE;
        }
    }
    return !p || p->ok;
}
static WORD render(OBJECT *tree, WORD start, WORD depth, WORD cx, WORD cy, WORD cw, WORD ch,
                   WORD *global, BOOL change, UWORD old)
{
    PAINTER p = { 0 };
    LONG session = aes_application_session(global);
    VDI_SERVICE_WORKSTATION ws;
    VDI_SERVICE_DRAW_REQUEST request = { 0 };
    VDI_SERVICE_DRAW_RESULT result;
    WORD input[11] = { 1,1,1,1,1,1,1,0,1,1,2 }, clip[4], x, y;
    BOOL opened = FALSE, success = FALSE;
    if (session < 0 || !walk(NULL,tree,start,depth,0,0)) return 0;
    if (cw <= 0 || ch <= 0) return 1;
    if (cuis_call6(CUIS_DISPLAY_LOCK,session,1,1,0,0,0) != E_OK) return 0;
    if (aes_graphics_call(VDI_SERVICE_OPEN,(uintptr_t)input,(uintptr_t)&ws,0,0) != E_OK) goto done;
    opened = TRUE; p.handle = ws.handle; p.ok = TRUE;
    p.left = cx < 0 ? 0 : cx; p.upper = cy < 0 ? 0 : cy;
    p.right = (LONG)cx+cw-1; p.lower = (LONG)cy+ch-1;
    if (p.right > ws.intout[0]) p.right = ws.intout[0];
    if (p.lower > ws.intout[1]) p.lower = ws.intout[1];
    if (p.right < p.left || p.lower < p.upper) { success = TRUE; goto done; }
    clip[0] = p.left; clip[1] = p.upper; clip[2] = p.right; clip[3] = p.lower;
    if (aes_graphics_call(VDI_SERVICE_CLIP,p.handle,1,(uintptr_t)clip,0) != E_OK) goto done;
    request.code = 38;
    if (!draw_call(&p,&request,&result)) goto done;
    p.default_top = result.ptsout[1];
    if (p.default_top < 1) goto done;
    objc_offset(tree,start,&x,&y);
    if (change) paint(&p,tree,start,x,y,old,(old ^ tree[start].ob_state) == SELECTED);
    else walk(&p,tree,start,depth,(LONG)x-tree[start].ob_x,(LONG)y-tree[start].ob_y);
    success = p.ok;
done:
    if (opened && aes_graphics_call(VDI_SERVICE_CLOSE,ws.handle,0,0,0) != E_OK) success = FALSE;
    /* A USERDEF callback may change the caller's global array. Release the
     * captured session, rather than depending on mutable GEM globals. */
    if (cuis_call6(CUIS_DISPLAY_LOCK,session,0,0,0,0,0) != E_OK) success = FALSE;
    return success ? 1 : 0;
}
WORD mt_objc_draw(OBJECT *tree, WORD start, WORD depth, WORD x, WORD y, WORD w, WORD h, WORD *global)
{
    if (!tree || start < 0 || depth < 0 || depth > MAX_DEPTH) return 0;
    return render(tree,start,depth,x,y,w,h,global,FALSE,0);
}
WORD objc_draw(OBJECT *tree, WORD start, WORD depth, WORD x, WORD y, WORD w, WORD h)
{
    return mt_objc_draw(tree,start,depth,x,y,w,h,aes_global);
}
WORD mt_objc_change(OBJECT *tree, WORD object, WORD reserved, WORD x, WORD y, WORD w, WORD h,
                    WORD state, WORD redraw, WORD *global)
{
    UWORD old;
    (void)reserved;
    if (!tree || object < 0) return 0;
    old = tree[object].ob_state;
    if (old == (UWORD)state) return 1;
    tree[object].ob_state = state;
    if (redraw && !(tree[object].ob_flags & HIDETREE) && !render(tree,object,0,x,y,w,h,global,TRUE,old)) {
        tree[object].ob_state = old;
        return 0;
    }
    return 1;
}
WORD objc_change(OBJECT *tree, WORD object, WORD reserved, WORD x, WORD y, WORD w, WORD h, WORD state, WORD redraw)
{
    return mt_objc_change(tree,object,reserved,x,y,w,h,state,redraw,aes_global);
}
