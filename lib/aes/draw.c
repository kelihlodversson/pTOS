/* Userspace binding of the original AES renderer and GSX drawing code.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "emutos.h"
#include "gem.h"
#include "gemerror.h"
#include "vdi_service.h"
#include "transport.h"
#include "application.h"
#include "../../aes/gemobjtree.h"
#include "../../aes/gemobjop.h"
#include "../../aes/gemoblib.h"
#include "../../aes/gemgraf.h"
#include "../../aes/gemgsxif.h"
#include "../../aes/gsx2.h"
#include "aesext.h"
#include "../../aes/funcdef.h"
#include "optimopt.h"
#include "intmath.h"
#include "string.h"
#include "rectfunc.h"

typedef struct painter {
    WORD handle;
    BOOL ok;
    WS workstation;
    GRECT clip;
    WORD wchar, hchar, wschar, hschar, wbox, hbox;
    WORD mode, text_color, line_color, interior, pattern, font;
    WORD char_width, char_top, small_width, small_top;
    VDICONTROL control;
    WORD input[128], points[20], output[45], outpoints[12];
    struct { char g_rawstr[MAX_LEN], g_tmpstr[MAX_LEN], g_fmtstr[MAX_LEN]; } scratch;
} PAINTER;
/* Per-process, temporarily rebound around a draw. Nested USERDEF drawings
 * save and restore this pointer, retaining independent GSX caches and buffers. */
static PAINTER *active;


/* Only these low-level GSX bindings differ from the native renderer. They
 * translate bounded primitive buffers, never OBJECT or USERBLK pointers. */
static void invoke(VDI_SERVICE_DRAW_REQUEST *request)
{
    VDI_SERVICE_DRAW_RESULT result = { 0 };
    if (!active->ok) return;
    active->ok = aes_graphics_call(VDI_SERVICE_DRAW,active->handle,(uintptr_t)request,(uintptr_t)&result,0) == E_OK;
    if (active->ok) {
        memcpy(active->output,result.intout,sizeof(result.intout));
        memcpy(active->outpoints,result.ptsout,sizeof(result.ptsout));
    }
}
void gsx2(void)
{
    VDI_SERVICE_DRAW_REQUEST request = { 0 };
    WORD i;
    if (!active->ok) return;
    if (active->control.code == SET_LINE_COLOR) {
        active->ok = aes_graphics_call(VDI_SERVICE_COLOR,active->handle,(UWORD)active->input[0],0,0) >= 0;
        return;
    }
    if (active->control.nintin < 0 || active->control.nintin > 128 || active->control.nptsin < 0 || active->control.nptsin > 2) {
        active->ok = FALSE; return;
    }
    request.code = active->control.code; request.nintin = active->control.nintin; request.nptsin = active->control.nptsin;
    for (i = 0; i < active->control.nintin; i++) request.intin[i] = active->input[i];
    for (i = 0; i < active->control.nptsin*2; i++) request.ptsin[i] = active->points[i];
    /* Native VDI clamps style zero to one; preserve that result within the
     * typed service's canonical range (solid/hollow fills ignore the style). */
    if (request.code == SET_FILL_STYLE && request.intin[0] == 0) request.intin[0] = 1;
    if ((request.code == SET_TEXT_COLOR || request.code == SET_FILL_COLOR) && request.intin[0] < 0)
        request.intin[0] = BLACK;
    invoke(&request);
}
void gsx_1code(WORD code, WORD value)
{
    active->control.code = code; active->control.nintin = 1; active->control.nptsin = 0;
    active->input[0] = value;
    gsx2();
}
void v_pline(WORD count, WORD *points)
{
    if (active->ok) active->ok = aes_graphics_call(VDI_SERVICE_LINE,active->handle,count,(uintptr_t)points,0) == E_OK;
}
void vs_clip(WORD enabled, WORD *points)
{
    if (active->ok) active->ok = aes_graphics_call(VDI_SERVICE_CLIP,active->handle,enabled,(uintptr_t)points,0) == E_OK;
}
void vr_recfl(WORD *points)
{
    VDI_SERVICE_DRAW_REQUEST request = { 0 };
    request.code = FILL_RECTANGLE; request.nptsin = 2;
    memcpy(request.ptsin,points,sizeof(request.ptsin));
    invoke(&request);
}
void vst_height(WORD height, WORD *charw, WORD *charh, WORD *cellw, WORD *cellh)
{
    VDI_SERVICE_DRAW_REQUEST request = { 0 };
    request.code = SET_CHAR_HEIGHT; request.nptsin = 1; request.ptsin[1] = height;
    invoke(&request);
    if (active->ok) { *charw = active->outpoints[0]; *charh = active->outpoints[1]; *cellw = active->outpoints[2]; *cellh = active->outpoints[3]; }
}
/* Cursor lifetime belongs to the display service; these legacy nesting calls
 * do not issue global cursor commands from an application's private painter. */
void gsx_moff(void) { }
void gsx_mon(void) { }
/* Image/icon adapters follow separately. Preflight rejects those types. */
void gsx_blt(void *address, WORD sx, WORD sy, WORD dx, WORD dy, WORD w, WORD h, WORD rule, WORD fg, WORD bg)
{
    (void)address; (void)sx; (void)sy; (void)dx; (void)dy; (void)w; (void)h; (void)rule; (void)fg; (void)bg;
    active->ok = FALSE;
}
void gr_gicon(WORD state, ICONBLK *icon, CICON *color)
{
    (void)state; (void)icon; (void)color; active->ok = FALSE;
}

#define gl_handle (active->handle)
#define gl_ws (active->workstation)
#define gl_clip (active->clip)
#define gl_wchar (active->wchar)
#define gl_hchar (active->hchar)
#define gl_wschar (active->wschar)
#define gl_hschar (active->hschar)
#define gl_wbox (active->wbox)
#define gl_hbox (active->hbox)
#define gl_mode (active->mode)
#define gl_tcolor (active->text_color)
#define gl_lcolor (active->line_color)
#define gl_fis (active->interior)
#define gl_patt (active->pattern)
#define gl_font (active->font)
#define gl_wptschar (active->char_width)
#define gl_hptschar (active->char_top)
#define gl_wsptschar (active->small_width)
#define gl_hsptschar (active->small_top)
#define contrl (active->control)
#define intin (active->input)
#define ptsin (active->points)
#define intout (active->output)
#define ptsout (active->outpoints)
#define D (active->scratch)

/* The original implementations, also included by their native AES units. */
#include "../../aes/gemgclip.inc"
#include "../../aes/gemgraphutil.c"
#include "../../aes/gemgline.inc"
#include "../../aes/gemgtext.inc"
#include "../../aes/gemgbox.inc"
#include "../../aes/gemobwalk.inc"
#define OB_RENDER_3D 0
#define OB_RENDER_NICELINES 0
#define OB_RENDER_COLOUR_ICONS 0
#define OB_RENDER_EXTENDED 0
#define OB_RENDER_CANCELLED(tree) (!active->ok)
#include "../../aes/gemobdraw.inc"
#include "../../aes/gemobchange.inc"
#undef intin
#undef ptsin
#undef intout
#undef ptsout
#undef contrl
#undef D

static LONG bounded_length(const char *string, LONG limit)
{
    LONG n = 0;
    if (!string) return limit+1;
    while (n <= limit && string[n]) n++;
    return n;
}
static BOOL supported(OBJECT *object)
{
    WORD type = object->ob_type & 255;
    OBSPEC spec;
    TEDINFO *ted;
    if (object->ob_flags & FL3DMASK) return FALSE;
    spec = object->ob_flags & INDIRECT ? *object->ob_spec.indirect : object->ob_spec;
    if (spec.index == -1) return TRUE;
    switch (type) {
    case G_BOX: case G_IBOX: case G_BOXCHAR: return TRUE;
    case G_STRING: case G_BUTTON: case G_TITLE: return bounded_length(spec.free_string,127) <= 127;
    case G_TEXT: case G_BOXTEXT: case G_FTEXT: case G_FBOXTEXT:
        ted = spec.tedinfo;
        return ted && (ted->te_font == IBM || ted->te_font == SMALL) && ted->te_just >= 0 && ted->te_just <= 2 &&
            bounded_length(ted->te_ptext,(type == G_FTEXT || type == G_FBOXTEXT) ? MAX_LEN-1 : 127) <=
                ((type == G_FTEXT || type == G_FBOXTEXT) ? MAX_LEN-1 : 127) &&
            ((type != G_FTEXT && type != G_FBOXTEXT) || bounded_length(ted->te_ptmplt,MAX_LEN-1) < MAX_LEN);
    case G_USERDEF: return spec.userblk && spec.userblk->ub_code;
    default: return FALSE;
    }
}
static BOOL preflight(OBJECT *tree, WORD object, WORD depth)
{
    WORD child;
    if (tree[object].ob_flags & HIDETREE) return TRUE;
    if (!supported(&tree[object])) return FALSE;
    if (depth && tree[object].ob_head != NIL)
        for (child = tree[object].ob_head; child != object; child = tree[child].ob_next)
            if (child < 0 || !preflight(tree,child,depth-1)) return FALSE;
    return TRUE;
}
static WORD render(OBJECT *tree, WORD start, WORD depth, WORD cx, WORD cy, WORD cw, WORD ch,
                   WORD *global, BOOL change, UWORD state)
{
    PAINTER p = { 0 }, *saved = active;
    LONG session = aes_application_session(global), right, bottom;
    VDI_SERVICE_WORKSTATION ws;
    VDI_SERVICE_DRAW_REQUEST request = { 0 };
    WORD input[11] = { 1,1,1,1,1,1,1,0,1,1,2 }, parent, x, y, dummy;
    BOOL opened = FALSE, success = FALSE;
    if (session < 0 || !preflight(tree,start,depth)) return 0;
    if (cw <= 0 || ch <= 0) {
        if (change) tree[start].ob_state = state;
        return 1;
    }
    if (cuis_call6(CUIS_DISPLAY_LOCK,session,1,1,0,0,0) != E_OK) return 0;
    if (aes_graphics_call(VDI_SERVICE_OPEN,(uintptr_t)input,(uintptr_t)&ws,0,0) != E_OK) goto done;
    opened = TRUE; active = &p; p.handle = ws.handle; p.ok = TRUE;
    memcpy(&p.workstation,ws.intout,sizeof(ws.intout));
    memcpy((WORD *)&p.workstation+45,ws.ptsout,sizeof(ws.ptsout));
    p.mode = p.text_color = p.line_color = p.interior = p.pattern = p.font = -1;
    p.clip.g_x = cx < 0 ? 0 : cx; p.clip.g_y = cy < 0 ? 0 : cy;
    right = (LONG)cx+cw; bottom = (LONG)cy+ch;
    if (right > (LONG)ws.intout[0]+1) right = (LONG)ws.intout[0]+1;
    if (bottom > (LONG)ws.intout[1]+1) bottom = (LONG)ws.intout[1]+1;
    if (right <= p.clip.g_x || bottom <= p.clip.g_y) {
        if (change) tree[start].ob_state = state;
        success = TRUE; goto done;
    }
    p.clip.g_w = right-p.clip.g_x; p.clip.g_h = bottom-p.clip.g_y;
    gsx_sclip(&p.clip);
    request.code = INQ_TEXT_ATTRIBUTES;
    invoke(&request);
    if (!p.ok) goto done;
    gl_wptschar = p.outpoints[0]; gl_hptschar = p.outpoints[1]; gl_wchar = p.outpoints[2]; gl_hchar = p.outpoints[3];
    /* Same normal/small font setup as gsx_start(), without kernel globals. */
    gl_ws.ws_chmaxh = gl_hptschar;
    vst_height(gl_ws.ws_chminh,&gl_wsptschar,&gl_hsptschar,&gl_wschar,&gl_hschar);
    vst_height(gl_ws.ws_chmaxh,&dummy,&dummy,&dummy,&dummy);
    if (!p.ok || gl_wchar <= 0 || gl_hchar <= 0 || gl_wschar <= 0 || gl_hschar <= 0) goto done;
    if (change) ob_change_native(tree,start,state,TRUE);
    else {
        parent = get_par(tree,start);
        if (parent != NIL) ob_offset(tree,parent,&x,&y); else x = y = 0;
        everyobj(tree,start,start == ROOT ? NIL : tree[start].ob_next,just_draw,x,y,depth);
    }
    success = p.ok;
done:
    if (opened && aes_graphics_call(VDI_SERVICE_CLOSE,ws.handle,0,0,0) != E_OK) success = FALSE;
    active = saved;
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
    if (!redraw || (tree[object].ob_flags & HIDETREE)) { tree[object].ob_state = state; return 1; }
    if (!render(tree,object,0,x,y,w,h,global,TRUE,(UWORD)state)) {
        tree[object].ob_state = old;
        return 0;
    }
    return 1;
}
WORD objc_change(OBJECT *tree, WORD object, WORD reserved, WORD x, WORD y, WORD w, WORD h, WORD state, WORD redraw)
{
    return mt_objc_change(tree,object,reserved,x,y,w,h,state,redraw,aes_global);
}
