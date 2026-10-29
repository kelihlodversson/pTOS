/* Userspace binding of the original AES renderer and GSX drawing code.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "emutos.h"
#include "gem.h"
#include "gemerror.h"
#include "vdi_service.h"
#include "transport.h"
#include "application.h"
#include "../../aes/shared/gemobjtree.h"
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
#include "frames.h"
#include "gemmovegeom.h"
#include "gemslidegeom.h"
#include "gemsizegeom.h"
#include "gemobjbounds.h"
#include "gemframegeom.h"

#include "render_context.h"
#include "resource_context.h"
#include "gemcicon.h"
#include "vdi_colmap.h"
#include "../../aes/gemobed.h"
#include "../../aes/gemgrlib.h"
WORD aes_edit_core(OBJECT *tree, WORD object, WORD key, WORD *index, WORD kind);
#include "../../aes/shared/gemrender.h"

/* Save/restore around nested USERDEF calls. */
PAINTER *aes_painter;
AES_3D_STYLE aes_3d_style;


/* Only these low-level GSX bindings differ from the native renderer. They
 * translate bounded primitive buffers, never OBJECT or USERBLK pointers. */
static void invoke(VDI_SERVICE_DRAW_REQUEST *request)
{
    VDI_SERVICE_DRAW_RESULT result = { 0 };
    if (!aes_painter->ok) return;
    aes_painter->ok = aes_graphics_call(VDI_SERVICE_DRAW,aes_painter->handle,(uintptr_t)request,(uintptr_t)&result,0) == E_OK;
    if (aes_painter->ok) {
        memcpy(aes_painter->output,result.intout,sizeof(result.intout));
        memcpy(aes_painter->outpoints,result.ptsout,sizeof(result.ptsout));
    }
}
/* System GSX fonts use fixed cells and a left-aligned baseline. Drop only
 * complete off-screen cells, retaining the origin of partially clipped glyphs. */
static BOOL visible_text(VDI_SERVICE_DRAW_REQUEST *request)
{
    PAINTER *p = aes_painter;
    LONG x = request->ptsin[0], y = request->ptsin[1];
    WORD width = p->font == SMALL ? p->wschar : p->wchar;
    WORD height = p->font == SMALL ? p->hschar : p->hchar;
    WORD top = p->font == SMALL ? p->small_top : p->char_top;
    WORD first = 0, last = request->nintin, i;
    LONG right = (LONG)p->clip.g_x+p->clip.g_w;
    if (y-top >= (LONG)p->clip.g_y+p->clip.g_h || y-top+height <= p->clip.g_y)
        return FALSE;
    while (first < last && x+(LONG)(first+1)*width <= p->clip.g_x) first++;
    while (last > first && x+(LONG)(last-1)*width >= right) last--;
    if (first == last) return FALSE;
    for (i = first; i < last; i++) request->intin[i-first] = request->intin[i];
    for (i = last-first; i < request->nintin; i++) request->intin[i] = 0;
    request->nintin = last-first;
    request->ptsin[0] = x+(LONG)first*width;
    return TRUE;
}
void gsx2(void)
{
    VDI_SERVICE_DRAW_REQUEST request = { 0 };
    WORD i;
    if (!aes_painter->ok) return;
    if (aes_painter->control.code == SET_LINE_COLOR) {
        aes_painter->ok = aes_graphics_call(VDI_SERVICE_COLOR,aes_painter->handle,(UWORD)aes_painter->input[0],0,0) >= 0;
        return;
    }
    if (aes_painter->control.nintin < 0 || aes_painter->control.nintin > 128 || aes_painter->control.nptsin < 0 || aes_painter->control.nptsin > 2) {
        aes_painter->ok = FALSE; return;
    }
    request.code = aes_painter->control.code; request.nintin = aes_painter->control.nintin; request.nptsin = aes_painter->control.nptsin;
    for (i = 0; i < aes_painter->control.nintin; i++) request.intin[i] = aes_painter->input[i];
    for (i = 0; i < aes_painter->control.nptsin*2; i++) request.ptsin[i] = aes_painter->points[i];
    if (request.code == TEXT && !visible_text(&request)) return;
    /* Native VDI clamps style zero to one; preserve that result within the
     * typed service's canonical range (solid/hollow fills ignore the style). */
    if (request.code == SET_FILL_STYLE && request.intin[0] == 0) request.intin[0] = 1;
    if ((request.code == SET_TEXT_COLOR || request.code == SET_FILL_COLOR) && request.intin[0] < 0)
        request.intin[0] = BLACK;
    invoke(&request);
}
void gsx_1code(WORD code, WORD value)
{
    aes_painter->control.code = code; aes_painter->control.nintin = 1; aes_painter->control.nptsin = 0;
    aes_painter->input[0] = value;
    gsx2();
}
/* Match native VDI's Cohen-Sutherland clipping and rounded intersections.
 * Wider intermediates avoid overflow with arbitrary WORD object geometry. */
static LONG line_intersection(LONG a, LONG b, LONG divisor)
{
    QUAD q = (QUAD)a * b * 2 / divisor;
    return (LONG)(q >= 0 ? (q+1)/2 : (q-1)/2);
}
static WORD line_outcode(LONG x, LONG y)
{
    GRECT *c = &aes_painter->clip;
    return (x < c->g_x ? 1 : x >= (LONG)c->g_x+c->g_w ? 2 : 0) |
           (y < c->g_y ? 4 : y >= (LONG)c->g_y+c->g_h ? 8 : 0);
}
static BOOL clip_segment(LONG *points)
{
    LONG x1 = points[0], y1 = points[1], x2 = points[2], y2 = points[3];
    LONG x, y, dx, dy, bound;
    WORD first, last, code;
    GRECT *c = &aes_painter->clip;
    while ((first = line_outcode(x1,y1)) | (last = line_outcode(x2,y2))) {
        if (first & last) return FALSE;
        code = first ? first : last;
        dx = x2-x1; dy = y2-y1;
        if (code & 3) {
            bound = code & 1 ? c->g_x : (LONG)c->g_x+c->g_w-1;
            x = bound; y = y1+line_intersection(dy,bound-x1,dx);
        } else {
            bound = code & 4 ? c->g_y : (LONG)c->g_y+c->g_h-1;
            y = bound; x = x1+line_intersection(dx,bound-y1,dy);
        }
        if (first) { x1 = x; y1 = y; } else { x2 = x; y2 = y; }
    }
    points[0] = x1; points[1] = y1; points[2] = x2; points[3] = y2;
    return TRUE;
}
static void draw_segment(LONG *segment)
{
    WORD points[4], i;
    if (!aes_painter->ok || !clip_segment(segment)) return;
    for (i = 0; i < 4; i++) points[i] = segment[i];
    aes_painter->ok = aes_graphics_call(VDI_SERVICE_LINE,aes_painter->handle,2,(uintptr_t)points,0) == E_OK;
}
void aes_render_line(LONG x1, LONG y1, LONG x2, LONG y2)
{
    LONG segment[4] = { x1,y1,x2,y2 };
    draw_segment(segment);
}
BOOL aes_render_extent(const GRECT *rect, WORD state, WORD thickness)
{
    GRECT *clip = &aes_painter->clip;
    LONG pad = state & OUTLINED ? 3 : 3L*thickness;
    if (pad < 0) pad = -pad;
    return (LONG)rect->g_x+rect->g_w+pad >= clip->g_x &&
           (LONG)rect->g_y+rect->g_h+pad >= clip->g_y &&
           (LONG)rect->g_x-pad < (LONG)clip->g_x+clip->g_w &&
           (LONG)rect->g_y-pad < (LONG)clip->g_y+clip->g_h;
}
void aes_render_box(LONG x, LONG y, LONG width, LONG height)
{
    LONG right = x+width-1, bottom = y+height-1;
    LONG top_edge[4] = { x,y,right,y }, right_edge[4] = { right,y,right,bottom };
    LONG bottom_edge[4] = { right,bottom,x,bottom }, left_edge[4] = { x,bottom,x,y };
    if (width <= 0 || height <= 0) return;
    draw_segment(top_edge); draw_segment(right_edge);
    draw_segment(bottom_edge); draw_segment(left_edge);
}
void v_pline(WORD count, WORD *points)
{
    WORD i, j;
    LONG segment[4];
    BOOL oversized = FALSE;
    if (!aes_painter->ok) return;
    for (i = 0; i < count*2; i++)
        if (points[i] < -MAX_COORDINATE || points[i] > MAX_COORDINATE) oversized = TRUE;
    if (!oversized) {
        aes_painter->ok = aes_graphics_call(VDI_SERVICE_LINE,aes_painter->handle,count,(uintptr_t)points,0) == E_OK;
        return;
    }
    /* The classic painter uses solid replace/transparent border lines.
     * Clip each segment, never the box as a whole: its off-screen edges
     * must not turn into visible edges along the clipping rectangle. */
    for (i = 1; i < count && aes_painter->ok; i++) {
        for (j = 0; j < 4; j++) segment[j] = points[(i-1)*2+j];
        draw_segment(segment);
    }
}
void vs_clip(WORD enabled, WORD *points)
{
    if (aes_painter->ok) aes_painter->ok = aes_graphics_call(VDI_SERVICE_CLIP,aes_painter->handle,enabled,(uintptr_t)points,0) == E_OK;
}
void aes_render_fill(LONG x, LONG y, LONG width, LONG height)
{
    VDI_SERVICE_DRAW_REQUEST request = { 0 };
    LONG left = max(x,aes_painter->clip.g_x);
    LONG top = max(y,aes_painter->clip.g_y);
    LONG right = min(x+width-1,(LONG)aes_painter->clip.g_x+aes_painter->clip.g_w-1);
    LONG bottom = min(y+height-1,(LONG)aes_painter->clip.g_y+aes_painter->clip.g_h-1);
    if (width <= 0 || height <= 0 || left > right || top > bottom) return;
    request.code = FILL_RECTANGLE; request.nptsin = 2;
    request.ptsin[0] = left; request.ptsin[1] = top;
    request.ptsin[2] = right; request.ptsin[3] = bottom;
    invoke(&request);
}
void vr_recfl(WORD *points)
{
    aes_render_fill(points[0],points[1],(LONG)points[2]-points[0]+1,(LONG)points[3]-points[1]+1);
}
void vst_height(WORD height, WORD *charw, WORD *charh, WORD *cellw, WORD *cellh)
{
    VDI_SERVICE_DRAW_REQUEST request = { 0 };
    request.code = SET_CHAR_HEIGHT; request.nptsin = 1; request.ptsin[1] = height;
    invoke(&request);
    if (aes_painter->ok) { *charw = aes_painter->outpoints[0]; *charh = aes_painter->outpoints[1]; *cellw = aes_painter->outpoints[2]; *cellh = aes_painter->outpoints[3]; }
}
/* The VDI service protects each framebuffer write with balanced cursor
 * removal/recomposition. USERDEF VDI writes receive the same protection. */
void gsx_moff(void) { }
void gsx_mon(void) { }
/* Mono forms remain in application memory. Submit only visible runs of set
 * bits through the existing solid-fill service: no bitmap or object pointers
 * cross the kernel boundary. The original image/icon painter uses MD_TRANS. */
void gsx_blt(void *address, WORD sx, WORD sy, WORD dx, WORD dy, WORD w, WORD h, WORD rule, WORD fg, WORD bg)
{
    const UWORD *bitmap = address;
    GRECT *clip = &aes_painter->clip;
    LONG left, top, right, bottom, x, y, run, source_x, source_y, stride;
    (void)bg;
    if (!aes_painter->ok) return;
    if (!bitmap || rule != MD_TRANS || fg < 0 || fg > 255 || w <= 0 || (w & 15) ||
        h <= 0 || sx < 0 || sy < 0 || sx >= w || sy >= h) {
        aes_painter->ok = FALSE; return;
    }
    stride = w/16;
    left = max((LONG)dx,clip->g_x); top = max((LONG)dy,clip->g_y);
    right = min((LONG)dx+w-sx,(LONG)clip->g_x+clip->g_w);
    bottom = min((LONG)dy+h-sy,(LONG)clip->g_y+clip->g_h);
    if (left >= right || top >= bottom) return;
    vsf_color(fg);
    for (y = top; y < bottom && aes_painter->ok; y++) {
        source_y = sy+y-dy;
        run = -1;
        for (x = left; x <= right; x++) {
            source_x = sx+x-dx;
            if (x < right && (bitmap[source_y*stride+source_x/16] & (0x8000U >> (source_x & 15)))) {
                if (run < 0) run = x;
            } else if (run >= 0) {
                aes_render_bb_fill(MD_REPLACE,FIS_SOLID,IP_SOLID,run,y,x-run,1);
                run = -1;
                if (!aes_painter->ok) break;
            }
        }
    }
}

/* Colour icons use unchanged standard-format planes in application memory.
 * The x32 packed-screen adapter emits palette-index runs through VDI, matching
 * native rs_cicon_pack without changing the caller's CICON or its bitmaps. */
static WORD colour_pen(UWORD index)
{
    WORD pen, colours = aes_painter->workstation.ws_ncolors;
    if (index >= (UWORD)colours) index = 0; /* Native pack's invalid-index rule. */
    if (index == (UWORD)(colours-1)) return BLACK;
    if (index == 15 && colours > 16) return colours-1;
    for (pen = 0; pen < 16; pen++)
        if (pen != BLACK && vdi_pen_order[pen] == index) return pen;
    return index;
}
void *aes_render_alloc(LONG bytes) { return aes_temporary_alloc(bytes); }
WORD aes_render_free(void *memory) { return aes_resource_discard(memory); }
CICON *aes_render_cicon(CICONBLK *block)
{
    VDI_SERVICE_DRAW_REQUEST request = { 0 };
    if (!aes_painter->ok || !block->mainlist) return NULL;
    if (!aes_painter->colour_planes) {
        request.code = 102; request.nintin = 1; request.intin[0] = 1;
        invoke(&request);
        if (!aes_painter->ok) return NULL;
        aes_painter->colour_planes = aes_painter->output[4];
        /* External x32 graphics currently uses packed GOP screens. Native
         * planar AES retains its transform/OR path unchanged. */
        if ((aes_painter->colour_planes != 16 && aes_painter->colour_planes != 32) ||
            aes_painter->workstation.ws_ncolors != 256) {
            aes_painter->ok = FALSE; return NULL;
        }
    }
    return rs_cicon_best(block,aes_painter->colour_planes);
}
void aes_render_colourblit(const WORD *data, const GRECT *rect, WORD planes)
{
    LONG left, right, top, bottom, x, y, word, stride, plane_words, start;
    WORD plane, previous, pen;
    UWORD index, mask;
    GRECT *clip = &aes_painter->clip;
    if (!aes_painter->ok) return;
    stride = rect->g_w/16; plane_words = stride*rect->g_h;
    left = max(rect->g_x,clip->g_x); top = max(rect->g_y,clip->g_y);
    right = min((LONG)rect->g_x+rect->g_w,(LONG)clip->g_x+clip->g_w);
    bottom = min((LONG)rect->g_y+rect->g_h,(LONG)clip->g_y+clip->g_h);
    for (y = top; y < bottom && aes_painter->ok; y++) {
        start = left; previous = -1;
        for (x = left; x <= right; x++) {
            pen = -1;
            if (x < right) {
                word = (y-rect->g_y)*stride+(x-rect->g_x)/16;
                mask = 0x8000U >> ((x-rect->g_x)&15); index = 0;
                for (plane = 0; plane < planes; plane++)
                    if ((UWORD)data[word+plane*plane_words] & mask) index |= 1U << plane;
                pen = colour_pen(index);
            }
            if (pen != previous) {
                if (previous >= 0) {
                    vsf_color(previous);
                    aes_render_bb_fill(MD_REPLACE,FIS_SOLID,IP_SOLID,start,y,x-start,1);
                    if (!aes_painter->ok) break;
                }
                start = x; previous = pen;
            }
        }
    }
}

BOOL ob_render_cancelled(OBJECT *tree)
{
    (void)tree;
    return !aes_painter->ok;
}

static LONG bounded_length(const char *string, LONG limit)
{
    LONG n = 0;
    if (!string) return limit+1;
    while (n <= limit && string[n]) n++;
    return n;
}
static BOOL word_position(LONG x, LONG y)
{
    return x >= -32768L && x <= 32767L && y >= -32768L && y <= 32767L;
}
static BOOL icon_position(const ICONBLK *icon, LONG x, LONG y)
{
    LONG ix = (LONG)x+icon->ib_xicon, iy = (LONG)y+icon->ib_yicon;
    LONG tx = (LONG)x+icon->ib_xtext, ty = (LONG)y+icon->ib_ytext;
    /* The shared native icon painter stores translated origins in WORDs.
     * Reject unrepresentable geometry before those assignments or its later
     * character/label layout additions can wrap into the visible screen. */
    if (!word_position(ix,iy) || !word_position(tx,ty)) return FALSE;
    if ((icon->ib_char & 255) && !word_position(ix+icon->ib_xchar,iy+icon->ib_ychar)) return FALSE;
    if (icon->ib_ptext[0] && (icon->ib_wtext < 0 || icon->ib_htext < 0 ||
        !word_position(tx+icon->ib_wtext,ty+icon->ib_htext))) return FALSE;
    return TRUE;
}
static BOOL supported_payload(OBJECT *object, LONG x, LONG y)
{
    WORD type = object->ob_type & 255;
    OBSPEC spec;
    TEDINFO *ted;
    BITBLK *bitmap;
    ICONBLK *icon;
    CICON *variant, *slow, *fast;
    ULONG bytes;
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
    case G_IMAGE:
        bitmap = spec.bitblk;
        return bitmap && bitmap->bi_pdata && bitmap->bi_wb > 0 && !(bitmap->bi_wb & 1) &&
            bitmap->bi_wb <= 4094 && bitmap->bi_hl > 0 &&
            (ULONG)bitmap->bi_wb*bitmap->bi_hl <= VDI_SERVICE_MAX_BITMAP &&
            bitmap->bi_x >= 0 && bitmap->bi_x < bitmap->bi_wb*8 &&
            bitmap->bi_y >= 0 && bitmap->bi_y < bitmap->bi_hl &&
            bitmap->bi_color >= 0 && bitmap->bi_color <= 255;
    case G_CICON:
        if (!spec.ciconblk) return FALSE;
        slow = fast = spec.ciconblk->mainlist;
        while (fast && fast->next_res) {
            slow = slow->next_res; fast = fast->next_res->next_res;
            if (slow == fast) return FALSE;
        }
        icon = &spec.ciconblk->monoblk;
        if (icon->ib_wicon <= 0 || (icon->ib_wicon & 15) || icon->ib_hicon <= 0) return FALSE;
        bytes = (ULONG)(icon->ib_wicon/8)*icon->ib_hicon;
        for (variant = spec.ciconblk->mainlist; variant; variant = variant->next_res)
            if (variant->num_planes <= 0 || variant->num_planes > 16 ||
                bytes > VDI_SERVICE_MAX_BITMAP/(UWORD)variant->num_planes ||
                !variant->col_data || !variant->col_mask ||
                (variant->sel_data && !variant->sel_mask)) return FALSE;
        FALLTHROUGH;
    case G_ICON:
        icon = spec.iconblk;
        return icon && icon->ib_pdata && icon->ib_pmask && icon->ib_wicon > 0 &&
            !(icon->ib_wicon & 15) && icon->ib_hicon > 0 &&
            (ULONG)(icon->ib_wicon/8)*icon->ib_hicon <= VDI_SERVICE_MAX_BITMAP &&
            bounded_length(icon->ib_ptext,127) <= 127 && icon_position(icon,x,y);
    case G_USERDEF: return spec.userblk && spec.userblk->ub_code;
    default: return FALSE;
    }
}
static BOOL supported(OBJECT *object, LONG x, LONG y)
{
    LONG payload_x = x, payload_y = y;
    /* Icon bitmap, label and character offsets use the enlarged 3D origin. */
    if (object->ob_flags & FL3DOBJ) { payload_x -= ADJ3DSTD; payload_y -= ADJ3DSTD; }
    if (!word_position(x,y) || !supported_payload(object,payload_x,payload_y)) return FALSE;
    if (object->ob_flags & FL3DMASK) {
        OBSPEC effective = object->ob_flags & INDIRECT ? *object->ob_spec.indirect : object->ob_spec;
        LONG pad, w = object->ob_width, h = object->ob_height;
        /* The original 3D painter uses WORD rectangles/line points. Keep
         * every outline, bevel and selected-text addition representable. */
        WORD border, dummy;
        GRECT logical;
        OBSPEC ignored;
        if (effective.index == -1L) return TRUE;
        ob_sst(object,0,&ignored,&dummy,&dummy,&dummy,&logical,&border);
        pad = ob_3d_padding(border);
        if (w <= 0 || h <= 0 || (LONG)x-pad < -32768L || (LONG)y-pad < -32768L ||
            (LONG)x+w+pad > 32767L || (LONG)y+h+pad > 32767L ||
            w+2*pad > 32767L || h+2*pad > 32767L) return FALSE;
    }
    return TRUE;
}
BOOL aes_render_ready(OBJECT *tree, WORD object, WORD x, WORD y)
{
    /* USERDEF can change a later sibling after the subtree preflight. */
    if (aes_painter->ok && !supported(&tree[object],x,y)) aes_painter->ok = FALSE;
    return aes_painter->ok;
}
static BOOL preflight(OBJECT *tree, WORD object, WORD depth, LONG x, LONG y)
{
    WORD child;
    if (tree[object].ob_flags & HIDETREE) return TRUE;
    if (!supported(&tree[object],x,y)) return FALSE;
    if (depth && tree[object].ob_head != NIL)
        for (child = tree[object].ob_head; child != object; child = tree[child].ob_next)
            if (child < 0 || !preflight(tree,child,depth-1,x+tree[child].ob_x,y+tree[child].ob_y)) return FALSE;
    return TRUE;
}
/* Resolve the start's ancestors once, before narrowing any accumulated sum.
 * The shared wide walker carries descendant positions through callbacks. */
static void absolute_position(OBJECT *tree, WORD object, LONG *x, LONG *y)
{
    *x = *y = 0;
    do {
        *x += tree[object].ob_x; *y += tree[object].ob_y;
        object = get_par(tree,object);
    } while (object != NIL);
}
static void draw_object(OBJECT *tree, WORD object, LONG x, LONG y)
{
    if (!aes_painter->ok || (tree[object].ob_flags & HIDETREE)) return;
    if (!supported(&tree[object],x,y)) { aes_painter->ok = FALSE; return; }
    ob_render_object(tree,object,(WORD)x,(WORD)y);
}
static WORD render(OBJECT *tree, WORD start, WORD depth, WORD cx, WORD cy, WORD cw, WORD ch,
                   WORD *global, BOOL change, UWORD state, WORD key, WORD *index, WORD kind, void (*operation)(void *), void *argument)
{
    PAINTER p = { 0 }, *saved = aes_painter;
    LONG session = aes_application_session(global), right, bottom, lock, x, y;
    VDI_SERVICE_WORKSTATION ws;
    VDI_SERVICE_DRAW_REQUEST request = { 0 };
    WORD input[11] = { 1,1,1,1,1,1,1,0,1,1,2 }, parent, dummy;
    BOOL opened = FALSE, success = FALSE;
    if (session < 0) return 0;
    /* Resolve ancestors once; each descendant inherits its parent's absolute
     * position without rescanning the sibling chain through get_par(). */
    x = y = 0;
    if (!(tree[start].ob_flags & HIDETREE)) absolute_position(tree,start,&x,&y);
    if (!preflight(tree,start,depth,x,y)) return 0;
    if (cw <= 0 || ch <= 0) {
        if (change) tree[start].ob_state = state;
        return 1;
    }
    lock = cuis_call6(CUIS_DISPLAY_LOCK,session,1,1,0,0,0);
    /* A blocking acquisition returns EAGAIN only when this caller already
     * owns a per-window section. Borrow its immutable clip without upgrading
     * to whole-screen ownership or changing its lifetime. */
    if (lock != E_OK && lock != EAGAIN) return 0;
    if (aes_graphics_call(VDI_SERVICE_OPEN,(uintptr_t)input,(uintptr_t)&ws,0,0) != E_OK) goto done;
    opened = TRUE; aes_painter = &p; p.handle = ws.handle; p.ok = TRUE;
    memcpy(&p.workstation,ws.intout,sizeof(ws.intout));
    memcpy((WORD *)&p.workstation+45,ws.ptsout,sizeof(ws.ptsout));
    if (!aes_3d_style.ready) { aes_init_3d(); aes_3d_style.ready = TRUE; }
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
    p.char_width = p.outpoints[0]; p.char_top = p.outpoints[1]; p.wchar = p.outpoints[2]; p.hchar = p.outpoints[3];
    /* Same normal/small font setup as gsx_start(), without kernel globals. */
    p.workstation.ws_chmaxh = p.char_top;
    vst_height(p.workstation.ws_chminh,&p.small_width,&p.small_top,&p.wschar,&p.hschar);
    vst_height(p.workstation.ws_chmaxh,&dummy,&dummy,&dummy,&dummy);
    if (!p.ok || p.wchar <= 0 || p.hchar <= 0 || p.wschar <= 0 || p.hschar <= 0) goto done;
    if (operation) operation(argument);
    else if (index) aes_edit_core(tree,start,key,index,kind);
    else if (change) ob_render_change(tree,start,state,TRUE);
    else {
        parent = get_par(tree,start);
        if (parent != NIL) absolute_position(tree,parent,&x,&y); else x = y = 0;
        everyobj(tree,start,start == ROOT ? NIL : tree[start].ob_next,draw_object,x,y,depth);
    }
    success = p.ok;
done:
    if (opened && aes_graphics_call(VDI_SERVICE_CLOSE,ws.handle,0,0,0) != E_OK) success = FALSE;
    aes_painter = saved;
    if (lock == E_OK && cuis_call6(CUIS_DISPLAY_LOCK,session,0,0,0,0,0) != E_OK) success = FALSE;
    return success ? 1 : 0;
}
WORD mt_objc_draw(OBJECT *tree, WORD start, WORD depth, WORD x, WORD y, WORD w, WORD h, WORD *global)
{
    if (!tree || start < 0 || depth < 0 || depth > MAX_DEPTH) return 0;
    return render(tree,start,depth,x,y,w,h,global,FALSE,0,0,NULL,0,NULL,NULL);
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
    if (!render(tree,object,0,x,y,w,h,global,TRUE,(UWORD)state,0,NULL,0,NULL,NULL)) {
        tree[object].ob_state = old;
        return 0;
    }
    return 1;
}
WORD objc_change(OBJECT *tree, WORD object, WORD reserved, WORD x, WORD y, WORD w, WORD h, WORD state, WORD redraw)
{
    return mt_objc_change(tree,object,reserved,x,y,w,h,state,redraw,aes_global);
}

/* The editor redraws into the same workstation and immutable update clip. */
void aes_edit_draw(OBJECT *tree, WORD object, WORD depth)
{
    WORD parent;
    LONG x = 0, y = 0;
    parent = get_par(tree,object);
    if (parent != NIL) absolute_position(tree,parent,&x,&y);
    everyobj(tree,object,tree[object].ob_next,draw_object,x,y,depth);
}

WORD mt_objc_edit(OBJECT *tree, WORD object, WORD key, WORD *index, WORD kind, WORD *global)
{
    TEDINFO *ted;
    OBSPEC spec;
    LONG raw, template, valid, slots = 0, i;
    WORD type, staged;
    if (!tree || !index || kind < EDSTART || kind > EDEND) return 0;
    if (kind == EDSTART || object <= 0) return 1;
    type = tree[object].ob_type & 255;
    if (type != G_FTEXT && type != G_FBOXTEXT) return 0;
    spec = tree[object].ob_flags & INDIRECT ? *tree[object].ob_spec.indirect : tree[object].ob_spec;
    ted = spec.tedinfo;
    if (!ted || ted->te_txtlen < 2 || ted->te_txtlen > MAX_LEN ||
        ted->te_tmplen < 1 || ted->te_tmplen >= MAX_LEN) return 0;
    raw = bounded_length(ted->te_ptext,ted->te_txtlen-1);
    template = bounded_length(ted->te_ptmplt,ted->te_tmplen-1);
    valid = bounded_length(ted->te_pvalid,MAX_LEN-1);
    if (raw >= ted->te_txtlen || template >= ted->te_tmplen || !valid || valid >= MAX_LEN) return 0;
    for (i = 0; i < template; i++) if (ted->te_ptmplt[i] == '_') slots++;
    /* find_pos() needs a template slot for every legal insertion position. */
    if (slots < ted->te_txtlen-1 || (kind != EDINIT && (*index < 0 || *index > raw))) return 0;
    staged = kind == EDINIT ? 0 : *index;
    if (!render(tree,object,0,0,0,32767,32767,global,FALSE,0,key,&staged,kind,NULL,NULL)) return 0;
    *index = staged;
    return 1;
}
WORD objc_edit(OBJECT *tree, WORD object, WORD key, WORD *index, WORD kind)
{
    return mt_objc_edit(tree,object,key,index,kind,aes_global);
}

typedef struct { WORD kind; GRECT small, large; } FORM_ANIMATION;
static void animate_form(void *argument)
{
    FORM_ANIMATION *a = argument;
    PAINTER *p = aes_painter;
    LONG hbox = (LONG)p->hchar + 3, wbox;
    /* Match gsx_start(), including non-square pixel aspect and the minimum
     * button width. Corner lengths in gsx_xcbox() must still fit a WORD. */
    if (p->workstation.ws_wpixel <= 0 || p->workstation.ws_hpixel <= 0 || hbox > 16383) {
        p->ok = FALSE; return;
    }
    wbox = hbox * p->workstation.ws_hpixel / p->workstation.ws_wpixel;
    if (wbox < (LONG)p->wchar + 4) wbox = (LONG)p->wchar + 4;
    if (wbox > 16383) { p->ok = FALSE; return; }
    p->wbox = wbox; p->hbox = hbox;
    if (a->kind == FMD_GROW) gr_growbox(&a->small,&a->large);
    else gr_shrinkbox(&a->small,&a->large);
}
WORD aes_form_animation(WORD kind, const GRECT *small, const GRECT *large, WORD *global)
{
    OBJECT dummy = { 0 };
    FORM_ANIMATION a;
    dummy.ob_type = G_IBOX; dummy.ob_flags = LASTOB;
    dummy.ob_next = dummy.ob_head = dummy.ob_tail = NIL;
    a.kind = kind; a.small = *small; a.large = *large;
    return render(&dummy,0,0,0,0,32767,32767,global,FALSE,0,0,NULL,0,animate_form,&a);
}

typedef struct {
    LONG native, session, press_x, press_y;
    const WM_FRAME_STATE *state;
    GRECT result;
    BOOL completed, sizing, sliding;
    WORD slider_value;
    WM_SLIDER_GEOMETRY slider;
    WORD wchar, hchar, width, height;
    WM_FRAME_INSETS insets;
} FRAME_DRAG;
static BOOL drag_position(FRAME_DRAG *drag, LONG x, LONG y, GRECT *outline)
{
    if (drag->sliding) return wm_slider_position(&drag->slider,drag->press_x,drag->press_y,x,y,&drag->slider_value,outline);
    if (drag->sizing) return wm_size_position(&drag->state->border,drag->state->kind,x,y,
        drag->wchar,drag->hchar,drag->state->wbox,drag->state->hbox,drag->width,drag->height,outline);
    return wm_move_position(&drag->state->border,drag->state->kind,drag->press_x,drag->press_y,x,y,
        drag->width,drag->height,drag->state->wbox,drag->state->hbox,outline);
}
static void drag_outline(FRAME_DRAG *drag, GRECT *outline)
{
    GRECT work;
    gsx_xbox(outline);
    if (drag->sizing) {
        work.g_x = outline->g_x+drag->insets.left; work.g_y = outline->g_y+drag->insets.top;
        work.g_w = outline->g_w-drag->insets.left-drag->insets.right;
        work.g_h = outline->g_h-drag->insets.top-drag->insets.bottom;
        gsx_xbox(&work); /* Native gr_rubwind's second, work-area box. */
    }
}
static void drag_frame(void *argument)
{
    FRAME_DRAG *drag = argument;
    PAINTER *p = aes_painter;
    GRECT outline;
    LONG event[4], result;
    drag->width = p->workstation.ws_xres+1; drag->height = p->workstation.ws_yres+1;
    drag->wchar = p->wchar; drag->hchar = p->hchar;
    if (!wm_frame_insets(drag->state->kind,drag->state->wbox,drag->state->hbox,TRUE,
                         drag->width,drag->height,&drag->insets) ||
        !drag_position(drag,drag->press_x,drag->press_y,&outline)) {
        p->ok = FALSE; return;
    }
    gsx_attr(FALSE,MD_XOR,BLACK);
    vsl_type(7); /* gsx_xbox supplies user-defined dotted line patterns. */
    for (;;) {
        /* Native gr_dragbox uses this same patterned XOR outline. Display
         * ownership spans the wait, so peers cannot overwrite its pixels. */
        drag_outline(drag,&outline);
        if (!p->ok) break;
        result = cuis_call6(CUIS_CONTROL_READ,drag->native,(uintptr_t)&event[0],
            (uintptr_t)&event[1],(uintptr_t)&event[2],(uintptr_t)&event[3],0);
        while (!result) {
            result = cuis_call6(CUIS_EVENT_WAIT,drag->native,CUIS_EVENT_CONTROL,(QUAD)-1,0,0,0);
            if (result <= 0) break;
            result = cuis_call6(CUIS_CONTROL_READ,drag->native,(uintptr_t)&event[0],
                (uintptr_t)&event[1],(uintptr_t)&event[2],(uintptr_t)&event[3],0);
        }
        drag_outline(drag,&outline);
        if (!p->ok || result <= 0 || (event[2] & ~1L)) break;
        if (!drag_position(drag,event[0],event[1],&outline)) {
            p->ok = FALSE; break;
        }
        if (!(event[2] & 1)) {
            drag->completed = (event[3] & 1) != 0; drag->result = outline; break;
        }
    }
}
static BOOL track_frame(WORD object, LONG native, const WM_FRAME_STATE *state, LONG press_x, LONG press_y,
                         GRECT *result, WORD *global)
{
    OBJECT dummy = { 0 };
    FRAME_DRAG drag;
    BOOL ok;
    dummy.ob_type = G_IBOX; dummy.ob_flags = LASTOB;
    dummy.ob_next = dummy.ob_head = dummy.ob_tail = NIL;
    drag.native = native; drag.session = aes_application_session(global);
    drag.state = state; drag.press_x = press_x; drag.press_y = press_y;
    drag.completed = FALSE; drag.sizing = object == W_SIZER;
    drag.sliding = object == W_HELEV || object == W_VELEV;
    if (drag.sliding && !wm_slider_geometry(state,object,&drag.slider)) return FALSE;
    ok = render(&dummy,0,0,0,state->hbox,32767,32767-state->hbox,global,
                FALSE,0,0,NULL,0,drag_frame,&drag) != 0;
    if (!ok) (void)cuis_call6(CUIS_DISPLAY_REDRAW,drag.session,0,0,32767,32767,0);
    if (ok && drag.completed) {
        *result = drag.result;
        if (drag.sliding) { result->g_x = drag.slider_value; result->g_y = result->g_w = result->g_h = 0; }
    }
    return ok && drag.completed;
}

BOOL aes_frame_move(LONG native, const WM_FRAME_STATE *state, LONG press_x, LONG press_y, GRECT *result, WORD *global)
{
    return track_frame(W_NAME,native,state,press_x,press_y,result,global);
}
BOOL aes_frame_size(LONG native, const WM_FRAME_STATE *state, LONG press_x, LONG press_y, GRECT *result, WORD *global)
{
    return track_frame(W_SIZER,native,state,press_x,press_y,result,global);
}

BOOL aes_frame_slide(LONG native, const WM_FRAME_STATE *state, WORD object, LONG press_x, LONG press_y, GRECT *result, WORD *global)
{
    return (object == W_HELEV || object == W_VELEV) && track_frame(object,native,state,press_x,press_y,result,global);
}


WORD mt_objc_sysvar(WORD mode, WORD which, WORD in1, WORD in2, WORD *out1, WORD *out2, WORD *global)
{
    PAINTER p = { 0 }, *saved = aes_painter;
    VDI_SERVICE_WORKSTATION ws;
    WORD input[11] = { 1,1,1,1,1,1,1,0,1,1,2 }, first = 0, second = 0, result;
    if ((mode != SV_INQUIRE && mode != SV_SET) || aes_application_session(global) < 0) return 0;
    /* Negative colours are not VDI pens and cannot index xor_color's table. */
    if (mode == SV_SET && (which == INDBUTCOL || which == ACTBUTCOL || which == BACKGRCOL) && in1 < 0) return 0;
    if (aes_graphics_call(VDI_SERVICE_OPEN,(uintptr_t)input,(uintptr_t)&ws,0,0) != E_OK) return 0;
    if (aes_graphics_call(VDI_SERVICE_CLOSE,ws.handle,0,0,0) != E_OK ||
        ws.intout[13] <= 0 || ws.intout[13] > 256) return 0;
    p.workstation.ws_ncolors = ws.intout[13]; aes_painter = &p;
    if (!aes_3d_style.ready) { aes_init_3d(); aes_3d_style.ready = TRUE; }
    result = aes_sysvar_core(mode,which,in1,in2,&first,&second);
    aes_painter = saved;
    if (result && !mode) {
        if (out1) *out1 = first;
        if (out2 && (which == LK3DIND || which == LK3DACT || which == AD3DVALUE)) *out2 = second;
    }
    return result;
}
WORD objc_sysvar(WORD mode, WORD which, WORD in1, WORD in2, WORD *out1, WORD *out2)
{ return mt_objc_sysvar(mode,which,in1,in2,out1,out2,aes_global); }
