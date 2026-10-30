/* GEM IDs and plain-window geometry are private to each application.
 * CUIS retains authoritative ownership, visibility and global stacking. */
#include "gem.h"
#include "gemerror.h"
#include "transport.h"
#include "application.h"
#include "layout.h"
#include "gemframegeom.h"
#include "gemmovegeom.h"
#include "gemsizegeom.h"
#include "gemslidegeom.h"
#include "frames.h"
#include "resource_context.h"
#include "desktop.h"
#include "graphics.h"
#include "frame_colours.h"
#include "string.h"

struct visible_region {
    struct visible_region *next;
    LONG bounds[4];
};
static void discard_regions(struct visible_region **head)
{
    struct visible_region *item;
    while ((item = *head) != NULL) {
        *head = item->next;
        (void)aes_resource_discard(item);
    }
}

#define GEM_WINDOW_LIMIT 64
static struct gem_window {
    short id;
    WORD top_colours[NUM_ELEM], bottom_colours[NUM_ELEM];
    UWORD kind;
    char *name, *info;
    LONG native, drag_x, drag_y, arrow_tick;
    BOOL open, control_selected;
    WORD control, control_message, control_action;
    WORD hslide, vslide, hslsiz, vslsiz;
    WM_SLIDER_GEOMETRY drag_slider;
    GRECT control_rectangle;
    struct visible_region *regions;
    GRECT current, previous, full;
} windows[GEM_WINDOW_LIMIT];
static BOOL frame_notice_blocked, frame_display_blocked;
static ULONG frame_overlays, frame_dialogs;
#define ARROW_GADGETS (UPARROW|DNARROW|LFARROW|RTARROW)
#define CONTROL_GADGETS (CLOSER|FULLER|MOVER|SIZER|ARROW_GADGETS|VSLIDE|HSLIDE)
static BOOL control_display_blocked, control_pipe_blocked;
static BOOL thumb_control(WORD object) { return object == W_HELEV || object == W_VELEV; }
static BOOL drag_control(WORD object) { return object == W_NAME || object == W_SIZER || thumb_control(object); }
static BOOL repeat_control(WORD object) { return wm_arrow_action(object) >= 0 || object == W_VSLIDE || object == W_HSLIDE; }
BOOL aes_controls_listening(void)
{
    short i;
    if (frame_overlays || control_display_blocked || control_pipe_blocked) return FALSE;
    for (i = 0; i < GEM_WINDOW_LIMIT; i++)
        if (windows[i].id) return TRUE;
    return FALSE;
}
BOOL aes_controls_repeating(void)
{
    short i;
    if (!aes_controls_listening()) return FALSE;
    for (i = 0; i < GEM_WINDOW_LIMIT; i++)
        if (windows[i].id && windows[i].open && repeat_control(windows[i].control)) return TRUE;
    return FALSE;
}
BOOL aes_frames_suspended(void) { return frame_overlays != 0; }
BOOL aes_frames_wait_display(void) { return (frame_display_blocked || control_display_blocked) && !frame_overlays; }
BOOL aes_frames_suspend(void)
{
    if (frame_overlays == 0xffffffffUL) return FALSE;
    frame_overlays++; return TRUE;
}
void aes_frames_resume(void) { if (frame_overlays) frame_overlays--; }
BOOL aes_frames_dialog(BOOL begin)
{
    if (begin) {
        if (!aes_frames_suspend()) return FALSE;
        frame_dialogs++;
    } else if (frame_dialogs) { frame_dialogs--; aes_frames_resume(); }
    return TRUE;
}
BOOL aes_frames_registered(void)
{
    short i;
    if (aes_desktop_registered()) return TRUE;
    for (i = 0; i < GEM_WINDOW_LIMIT; i++)
        if (windows[i].id && windows[i].open && windows[i].kind) return TRUE;
    return FALSE;
}
BOOL aes_frames_listening(void) { return !frame_notice_blocked && !frame_overlays && aes_frames_registered(); }

void aes_windows_reset(void)
{
    short i;
    for (i = 0; i < GEM_WINDOW_LIMIT; i++) {
        discard_regions(&windows[i].regions);
        windows[i].id = 0;
    }
    frame_notice_blocked = frame_display_blocked = FALSE;
    control_display_blocked = control_pipe_blocked = FALSE;
    frame_overlays = frame_dialogs = 0;
}

static struct gem_window *lookup(short id, short *global)
{
    short i;
    if (id <= 0 || aes_application_session(global) < 0) return NULL;
    for (i = 0; i < GEM_WINDOW_LIMIT; i++)
        if (windows[i].id == id) return &windows[i];
    return NULL;
}

static void output(const GRECT *r, short *x, short *y, short *w, short *h)
{
    if (x) *x = r->g_x;
    if (y) *y = r->g_y;
    if (w) *w = r->g_w;
    if (h) *h = r->g_h;
}

short mt_wind_find(short x, short y, short *global)
{
    LONG identifier;
    if (aes_application_session(global) < 0) return -1;
    identifier = cuis_call6(CUIS_WINDOW_AT, (QUAD)x, (QUAD)y, 0, 0, 0, 0);
    return identifier >= 0 && identifier <= 32767 ? (short)identifier : -1;
}
short wind_find(short x, short y) { return mt_wind_find(x,y,aes_global); }

short mt_wind_calc(short type, short kind, short x, short y, short w, short h,
                   short *ox, short *oy, short *ow, short *oh, short *global)
{
    GRECT r;
    WM_FRAME_INSETS insets;
    WORD wbox = 1, hbox = 1, screen_width = 640, screen_height = 400;
    LONG nx, ny, nw, nh, sign;
    /* Classic gadgets and PC-GEM HOTCLOSE; extension bits are unsupported. */
    if (((UWORD)kind & ~0x1fffU) || (type != WC_BORDER && type != WC_WORK)) return 0;
    if (kind && !aes_frame_metrics(&wbox,&hbox,&screen_width,&screen_height,global)) return 0;
    if (!wm_frame_insets((UWORD)kind,wbox,hbox,TRUE,screen_width,screen_height,&insets)) return 0;
    sign = type == WC_WORK ? 1 : -1;
    nx = x + sign*insets.left; ny = y + sign*insets.top;
    nw = w - sign*(insets.left+insets.right);
    nh = h - sign*(insets.top+insets.bottom);
    /* Preserve negative work extents for small borders, but never wrap. */
    if (nx < -32768 || nx > 32767 || ny < -32768 || ny > 32767 ||
        nw < -32768 || nw > 32767 || nh < -32768 || nh > 32767) return 0;
    r.g_x = nx; r.g_y = ny; r.g_w = nw; r.g_h = nh;
    output(&r, ox, oy, ow, oh);
    return 1;
}
short wind_calc(short type, short kind, short x, short y, short w, short h,
                short *ox, short *oy, short *ow, short *oh)
{
    return mt_wind_calc(type, kind, x, y, w, h, ox, oy, ow, oh, aes_global);
}

static BOOL prepare_window(struct gem_window *window, const GRECT *border, WM_FRAME_STATE *state, WORD *global)
{
    if (!aes_frame_prepare(state,window->kind,border,window->name,window->info,global)) return FALSE;
    state->hslide = window->hslide; state->vslide = window->vslide;
    state->hslsiz = window->hslsiz; state->vslsiz = window->vslsiz;
    {
        LONG focus = cuis_call6(CUIS_FOCUS_QUERY,window->native,0,0,0,0,0);
        if (focus < 0) return FALSE;
        state->colours = focus ? window->top_colours : window->bottom_colours;
    }
    return TRUE;
}
static BOOL paint_window(struct gem_window *window, short *global)
{
    WM_FRAME_STATE state;
    if (!window->kind || !window->open) return TRUE;
    if (frame_overlays) {
        LONG session = aes_application_session(global);
        return session >= 0 && cuis_call6(CUIS_DISPLAY_REDRAW,session,(QUAD)window->current.g_x,
            (QUAD)window->current.g_y,window->current.g_w+2,window->current.g_h+2,0) == E_OK;
    }
    return prepare_window(window,&window->current,&state,global) &&
        aes_frame_paint_control(window->native,&state,window->control,window->control_selected,global);
}

static short local_wind_create(short kind, short x, short y, short w, short h, short *global)
{
    struct gem_window *window;
    short i;
    LONG native, identifier;
    WORD wbox = 1, hbox = 1, width = 640, height = 400;
    WM_FRAME_INSETS insets;
    WORD top[NUM_ELEM], bottom[NUM_ELEM];
    LONG value;
    if (((UWORD)kind & ~(NAME|INFO|CLOSER|FULLER|MOVER|SIZER|ARROW_GADGETS|VSLIDE|HSLIDE)) || aes_application_session(global) < 0) return -1;
    for (i = 0; i < GEM_WINDOW_LIMIT && windows[i].id; i++) ;
    if (i == GEM_WINDOW_LIMIT) return -1;
    if ((kind && !aes_frame_metrics(&wbox,&hbox,&width,&height,global)) ||
        !wm_frame_insets(kind,wbox,hbox,TRUE,width,height,&insets)) return -1;
    for (i = 0; i < NUM_ELEM; i++) {
        value = cuis_call6(CUIS_SETTING,i,0,0,0,0,0);
        if (value != EFILNF && (value < 0 || value > 65535)) return -1;
        top[i] = value == EFILNF ? aes_top_colours[i] : (WORD)value;
        value = cuis_call6(CUIS_SETTING,NUM_ELEM+i,0,0,0,0,0);
        if (value != EFILNF && (value < 0 || value > 65535)) return -1;
        bottom[i] = value == EFILNF ? aes_bottom_colours[i] : (WORD)value;
    }
    for (i = 0; i < GEM_WINDOW_LIMIT && windows[i].id; i++) ;
    /* GEM starts closed with zero current/previous geometry. CUIS requires
     * positive extents even for hidden metadata, so use an invisible placeholder. */
    native = cuis_call6(CUIS_WINDOW_CREATE, 0, 0, 1, 1, 0, 0);
    if (native < 0) return -1;
    identifier = cuis_call6(CUIS_WINDOW_IDENTIFY, native, 0, 0, 0, 0, 0);
    if (identifier <= 0 || identifier > 32767 ||
        cuis_call6(CUIS_WINDOW_OUTSET, native, 2, 2, 0, 0, 0) != E_OK ||
        cuis_call6(CUIS_WINDOW_POLICY, native, 1, 0, 0, 0, 0) != E_OK ||
        cuis_call6(CUIS_WINDOW_CONTENT,native,insets.left,insets.top,insets.right,insets.bottom,0) != E_OK ||
        cuis_call6(CUIS_WINDOW_CORNER,native,(kind & SIZER) ? wbox+2*ADJ3DSTD : 0,
                   (kind & SIZER) ? hbox+2*ADJ3DSTD : 0,0,0,0) != E_OK) {
        (void)cuis_call6(CUIS_WINDOW_DESTROY, native, 0, 0, 0, 0, 0);
        return -1;
    }
    window = &windows[i];
    memcpy(window->top_colours,top,sizeof(top));
    memcpy(window->bottom_colours,bottom,sizeof(bottom));
    window->native = native;
    window->kind = kind; window->name = window->info = "";
    window->hslide = window->vslide = 0; window->hslsiz = window->vslsiz = -1;
    window->control_action = -1;
    window->control_message = 0;
    window->open = FALSE;
    window->control = 0; window->control_selected = FALSE;
    discard_regions(&window->regions);
    window->current.g_x = window->current.g_y = 0;
    window->current.g_w = window->current.g_h = 0;
    window->previous = window->current;
    window->full.g_x = x; window->full.g_y = y;
    window->full.g_w = w; window->full.g_h = h;
    window->id = (short)identifier;
    return window->id;
}
short wind_create(short kind, short x, short y, short w, short h)
{
    return mt_wind_create(kind, x, y, w, h, aes_global);
}

static BOOL configure(struct gem_window *window, short x, short y, short w, short h, short *global)
{
    WM_FRAME_STATE state;
    GRECT border = { x,y,w,h };
    if (window->kind && !prepare_window(window,&border,&state,global)) return FALSE;
    return w > 0 && h > 0 &&
        mt_wind_calc(WC_WORK,0,x,y,w,h,NULL,NULL,NULL,NULL,NULL) &&
        cuis_call6(CUIS_WINDOW_CONFIGURE, window->native,
                     (QUAD)x, (QUAD)y, w, h, 0) == E_OK;
}
static short local_wind_open(short id, short x, short y, short w, short h, short *global)
{
    struct gem_window *window = lookup(id, global);
    GRECT current, previous;
    if (!window || window->open || !configure(window, x, y, w, h, global)) return 0;
    current = window->current; previous = window->previous;
    if (cuis_call6(CUIS_WINDOW_SHOW, window->native, 0, 0, 0, 0, 0) != E_OK) return 0;
    window->current.g_x = x; window->current.g_y = y;
    window->current.g_w = w; window->current.g_h = h;
    window->previous = window->current;
    window->open = TRUE;
    discard_regions(&window->regions);
    if (!paint_window(window,global)) {
        if (cuis_call6(CUIS_WINDOW_HIDE,window->native,0,0,0,0,0) == E_OK) {
            window->open = FALSE;
            window->current = current; window->previous = previous;
        }
        return 0;
    }
    return 1;
}
short wind_open(short id, short x, short y, short w, short h)
{
    return mt_wind_open(id, x, y, w, h, aes_global);
}
static short local_wind_close(short id, short *global)
{
    struct gem_window *window = lookup(id, global);
    if (!window || !window->open ||
        cuis_call6(CUIS_WINDOW_HIDE, window->native, 0, 0, 0, 0, 0) != E_OK) return 0;
    window->previous = window->current;
    window->current.g_x = window->current.g_y = 0;
    window->current.g_w = window->current.g_h = 0;
    window->open = FALSE;
    window->control = 0; window->control_selected = FALSE;
    discard_regions(&window->regions);
    return 1;
}
short wind_close(short id) { return mt_wind_close(id, aes_global); }
static short local_wind_delete(short id, short *global)
{
    struct gem_window *window = lookup(id, global);
    /* Native destruction hides an open window and reclaims its input queues. */
    if (!window || cuis_call6(CUIS_WINDOW_DESTROY, window->native, 0, 0, 0, 0, 0) != E_OK) return 0;
    discard_regions(&window->regions);
    window->id = 0;
    return 1;
}
short wind_delete(short id) { return mt_wind_delete(id, aes_global); }

static short visible_work(struct gem_window *window, BOOL first, GRECT *out, short *global)
{
    LONG state[4], result, left, top, right, bottom, edge;
    ULONG index;
    GRECT work;
    struct visible_region *snapshot = NULL, **tail = &snapshot, *item;
    if (first) {
        /* Copy the complete CUIS snapshot before frame painting can replace it. */
        for (index = 0; ; index++) {
            result = cuis_call6(CUIS_WINDOW_REGION,window->native,index,
                (uintptr_t)&state[0],(uintptr_t)&state[1],
                (uintptr_t)&state[2],(uintptr_t)&state[3]);
            if (result < 0) { discard_regions(&snapshot); return 0; }
            if (!result) break;
            item = aes_temporary_alloc(sizeof(*item));
            if (!item) { discard_regions(&snapshot); return 0; }
            item->next = NULL;
            item->bounds[0] = state[0]; item->bounds[1] = state[1];
            item->bounds[2] = state[2]; item->bounds[3] = state[3];
            *tail = item; tail = &item->next;
        }
        discard_regions(&window->regions);
        window->regions = snapshot;
    }
    if (!mt_wind_calc(WC_WORK, window->kind, window->current.g_x, window->current.g_y,
                  window->current.g_w, window->current.g_h,
                  &work.g_x, &work.g_y, &work.g_w, &work.g_h, global)) return 0;
    for (;;) {
        item = window->regions;
        if (!item) {
            out->g_x = out->g_y = out->g_w = out->g_h = 0;
            return 1;
        }
        state[0] = item->bounds[0]; state[1] = item->bounds[1];
        state[2] = item->bounds[2]; state[3] = item->bounds[3];
        window->regions = item->next;
        (void)aes_resource_discard(item);
        left = state[0] > work.g_x ? state[0] : work.g_x;
        top = state[1] > work.g_y ? state[1] : work.g_y;
        right = state[0] + state[2]; edge = (LONG)work.g_x + work.g_w;
        if (right > edge) right = edge;
        bottom = state[1] + state[3]; edge = (LONG)work.g_y + work.g_h;
        if (bottom > edge) bottom = edge;
        if (work.g_w > 0 && work.g_h > 0 && right > left && bottom > top) {
            out->g_x = (short)left; out->g_y = (short)top;
            out->g_w = (short)(right - left); out->g_h = (short)(bottom - top);
            return 1;
        }
    }
}

static LONG publish_notice(struct gem_window *window, LONG session, LONG kind,
                            short message[8], short *global)
{
    WM_FRAME_STATE state;
    LONG result;
    BOOL frame = kind == CUIS_NOTICE_REDRAW && window->kind && window->open;
    if (!frame) return cuis_call6(CUIS_NOTICE_PUBLISH,session,window->native,kind,
                                 (uintptr_t)message,sizeof(short)*8,0);
    result = cuis_call6(CUIS_DISPLAY_LOCK,session,1,0,0,0,0);
    if (result == EAGAIN) { frame_display_blocked = TRUE; return result; }
    if (result != E_OK) return result;
    if (!prepare_window(window,&window->current,&state,global)) {
        result = EACCDN; goto done;
    }
    /* Acknowledge before painting. Focus/damage arriving during the draw
     * remains pending for the next pass instead of being cleared afterward. */
    result = cuis_call6(CUIS_NOTICE_PUBLISH,session,window->native,kind,(uintptr_t)message,sizeof(short)*8,0);
    if ((result >= 0 || result == EAGAIN) && !aes_frame_paint_control(window->native,&state,window->control,window->control_selected,global)) {
        (void)cuis_call6(CUIS_DISPLAY_REDRAW,session,(QUAD)window->current.g_x,
                        (QUAD)window->current.g_y,window->current.g_w+2,window->current.g_h+2,0);
        if (result != EAGAIN) result = EACCDN;
    }
done:
    if (cuis_call6(CUIS_DISPLAY_LOCK,session,0,0,0,0,0) != E_OK) return EACCDN;
    return result;
}

/* Border input is serviced without grabbing the ordinary button history.
 * CUIS keeps capture and cancels stale topology/ownership; this private
 * watch-box state uses the native frame's objects and SELECTED appearance. */
static LONG flush_control(struct gem_window *window, LONG session)
{
    WORD message[8] = { 0,0,0,0,0,0,0,0 };
    LONG result;
    if (!window->control_message) return E_OK;
    message[0] = window->control_message; message[3] = window->id;
    message[4] = window->control_rectangle.g_x; message[5] = window->control_rectangle.g_y;
    message[6] = window->control_rectangle.g_w; message[7] = window->control_rectangle.g_h;
    result = cuis_call6(CUIS_STREAM_WRITE,session,(uintptr_t)message,16,0,0,0);
    if (result == EAGAIN) { control_pipe_blocked = TRUE; return E_OK; }
    if (result != 16) return result < 0 ? result : EACCDN;
    window->control_message = 0;
    return E_OK;
}
static LONG service_drag(struct gem_window *window, const WM_FRAME_STATE *state, LONG session, WORD *global)
{
    BOOL sizing = window->control == W_SIZER;
    window->control_selected = TRUE;
    if (paint_window(window,global) && (sizing ?
        aes_frame_size(window->native,state,window->drag_x,window->drag_y,&window->control_rectangle,global) :
        thumb_control(window->control) ? aes_frame_slide(window->native,state,window->control,
            window->drag_x,window->drag_y,&window->control_rectangle,global) :
        aes_frame_move(window->native,state,window->drag_x,window->drag_y,&window->control_rectangle,global)))
        window->control_message = sizing ? WM_SIZED : window->control == W_HELEV ? WM_HSLID :
            window->control == W_VELEV ? WM_VSLID : WM_MOVED;
    window->control = 0; window->control_selected = FALSE;
    (void)paint_window(window,global);
    return flush_control(window,session);
}
static LONG poll_drag(struct gem_window *window, LONG session, WORD *global)
{
    LONG current[4], event[4], result;
    WORD wbox, hbox, width, height, n, value;
    GRECT outline;
    LAYOUT_METRICS metrics;
    BOOL sizing = window->control == W_SIZER;
    /* Finish already-released drags without an outline/input wait. Keep
     * held motion queued, so a later blocking tracker sees every position. */
    if (cuis_call6(CUIS_INPUT_STATE,session,(uintptr_t)&current[0],(uintptr_t)&current[1],
                   (uintptr_t)&current[2],(uintptr_t)&current[3],0) != E_OK || current[2]) return E_OK;
    if (!aes_frame_metrics(&wbox,&hbox,&width,&height,global) ||
        (sizing && !aes_layout_metrics(&metrics,global))) return E_OK;
    for (n = 0; n < 16; n++) {
        result = cuis_call6(CUIS_CONTROL_READ,window->native,(uintptr_t)&event[0],
            (uintptr_t)&event[1],(uintptr_t)&event[2],(uintptr_t)&event[3],0);
        if (!result) return E_OK;
        if (result < 0 || (event[2] & ~1L)) {
            window->control = 0;
            return result < 0 && result != ERANGE ? result : E_OK;
        }
        if (event[2] & 1) continue;
        if ((event[3] & 1) && thumb_control(window->control)) {
            if (wm_slider_position(&window->drag_slider,window->drag_x,window->drag_y,event[0],event[1],&value,&outline)) {
                window->control_rectangle.g_x = value;
                window->control_rectangle.g_y = window->control_rectangle.g_w = window->control_rectangle.g_h = 0;
                window->control_message = window->control == W_HELEV ? WM_HSLID : WM_VSLID;
            }
        } else if ((event[3] & 1) && (sizing ?
            wm_size_position(&window->current,window->kind,event[0],event[1],metrics.wchar,metrics.hchar,
                             wbox,hbox,width,height,&window->control_rectangle) :
            wm_move_position(&window->current,window->kind,window->drag_x,window->drag_y,event[0],event[1],width,height,wbox,hbox,
                             &window->control_rectangle)))
            window->control_message = sizing ? WM_SIZED : WM_MOVED;
        window->control = 0;
        return flush_control(window,session);
    }
    return E_OK;
}
static LONG arrow_message(struct gem_window *window, LONG session)
{
    LONG now = cuis_call6(CUIS_CLOCK,session,0,0,0,0,0);
    if (now < 0) return now;
    window->arrow_tick = now;
    window->control_message = WM_ARROWED;
    window->control_rectangle.g_x = window->control_action;
    window->control_rectangle.g_y = window->control_rectangle.g_w = window->control_rectangle.g_h = 0;
    return flush_control(window,session);
}
static LONG service_controls(LONG session, WORD *global, BOOL polling)
{
    short i, n, object;
    LONG result = E_OK, event[4];
    BOOL selected, completed;
    WM_FRAME_STATE state;
    struct gem_window *window;
    control_display_blocked = control_pipe_blocked = FALSE;
    for (i = 0; i < GEM_WINDOW_LIMIT; i++) {
        window = &windows[i];
        if (!window->id) continue;
        result = flush_control(window,session);
        if (result < 0) return result;
    }
    if (control_pipe_blocked) return E_OK;
    if (!aes_controls_listening()) return E_OK;
    result = cuis_call6(CUIS_DISPLAY_LOCK,session,1,0,0,0,0);
    if (result == EAGAIN) { control_display_blocked = TRUE; return E_OK; }
    if (result != E_OK) return result;
    for (i = 0; i < GEM_WINDOW_LIMIT; i++) {
        window = &windows[i];
        if (!window->id) continue;
        /* A poll retains the captured press, without painting or waiting.
         * The next blocking service resumes from the original anchor. */
        if (drag_control(window->control)) {
            if (polling) {
                result = poll_drag(window,session,global);
                if (result < 0 || control_pipe_blocked) goto done;
                continue;
            }
            if (prepare_window(window,&window->current,&state,global)) {
                result = service_drag(window,&state,session,global);
                if (result < 0 || control_pipe_blocked) goto done;
            } else window->control = 0;
        }
        /* Bounded even under a continuously moving producer. */
        for (n = 0; n < 16; n++) {
            result = cuis_call6(CUIS_CONTROL_READ,window->native,(uintptr_t)&event[0],
                (uintptr_t)&event[1],(uintptr_t)&event[2],(uintptr_t)&event[3],0);
            if (!result) break;
            if (result == ERANGE) {
                selected = window->control_selected;
                window->control = 0; window->control_selected = FALSE;
                if (selected && window->open && (window->kind & CONTROL_GADGETS))
                    (void)paint_window(window,global);
                result = E_OK; continue;
            }
            if (result < 0) goto done;
            if (!window->open || !(window->kind & CONTROL_GADGETS)) continue;
            if (!prepare_window(window,&window->current,&state,global)) {
                window->control = 0; window->control_selected = FALSE; break;
            }
            if ((event[3] & 1) && (event[2] & 1) && !window->control) {
                window->control = aes_frame_control(&state,event[0],event[1]);
                window->control_action = wm_arrow_action(window->control);
                if (window->control == W_VSLIDE || window->control == W_HSLIDE) {
                    window->control_action = wm_slider_page(&state,window->control,event[0],event[1]);
                    if (window->control_action < 0) window->control = 0;
                }
                if (thumb_control(window->control) && !wm_slider_geometry(&state,window->control,&window->drag_slider))
                    window->control = 0;
            }
            if ((drag_control(window->control)) && event[2] == 1) {
                window->drag_x = event[0]; window->drag_y = event[1];
                if (polling) {
                    result = poll_drag(window,session,global);
                    if (result < 0 || control_pipe_blocked) goto done;
                    break;
                }
                result = service_drag(window,&state,session,global);
                if (result < 0 || control_pipe_blocked) goto done;
                continue;
            }
            if (repeat_control(window->control)) {
                /* Native arrows send on press and repeat until release,
                 * retaining the original action and selection outside the
                 * glyph. They do not use close/full watch-box semantics. */
                completed = event[2] == 1 && (event[3] & 1);
                selected = event[2] == 1 && wm_arrow_action(window->control) >= 0;
                if (window->control_selected != selected) {
                    window->control_selected = selected;
                    if (!paint_window(window,global)) {
                        window->control = 0; window->control_selected = FALSE;
                        continue;
                    }
                }
                if (completed) {
                    result = arrow_message(window,session);
                    if (result < 0 || control_pipe_blocked) goto done;
                }
                if (event[2] != 1) window->control = 0;
                continue;
            }
            object = window->control && aes_frame_control_inside(&state,window->control,event[0],event[1]) ?
                window->control : 0;
            completed = window->control && !(event[2] & 1) && (event[3] & 1) && object == window->control;
            if (event[2] & ~1L) { window->control = 0; completed = FALSE; }
            selected = window->control && object == window->control && (event[2] & 1);
            if (completed) {
                window->control_message = window->control == W_CLOSER ? WM_CLOSED : WM_FULLED;
                window->control_rectangle = window->current;
            }
            if (!(event[2] & 1)) window->control = 0;
            if (window->control_selected != selected) {
                window->control_selected = selected;
                if (!paint_window(window,global)) {
                    window->control = 0; window->control_selected = FALSE;
                    if (!completed) continue;
                }
            }
            result = flush_control(window,session);
            if (result < 0 || control_pipe_blocked) goto done;
        }
        /* Read cancellation/release before generating any repeat. Only
         * repeat after draining the bounded queue, at most once per native
         * two-tick dispatch interval, never in a tight loop or by waiting
         * with the display. */
        if (n < 16 && repeat_control(window->control)) {
            LONG now = cuis_call6(CUIS_CLOCK,session,0,0,0,0,0);
            if (now < 0) { result = now; goto done; }
            if ((((ULONG)now-(ULONG)window->arrow_tick) & 0x7fffffffUL) >= 2) {
                result = arrow_message(window,session);
                if (result < 0 || control_pipe_blocked) goto done;
            }
        }
    }
    result = E_OK;
done:
    if (cuis_call6(CUIS_DISPLAY_LOCK,session,0,0,0,0,0) != E_OK) return EACCDN;
    return result;
}

static LONG window_messages(LONG session, short *global, BOOL polling)
{
    LONG native, identifier, kind, result;
    short message[8], i, j;
    struct gem_window *window;
    GRECT work;
    if (frame_overlays) return E_OK;
    frame_notice_blocked = frame_display_blocked = FALSE;
    result = aes_desktop_dispatch(global);
    if (result == EAGAIN) { frame_display_blocked = TRUE; return E_OK; }
    if (result < 0) return result;
    result = service_controls(session,global,polling);
    if (result < 0) return result;
    /* Bound pumping even if input continuously invalidates the display. */
    for (i = 0; i < 2 * GEM_WINDOW_LIMIT; i++) {
        kind = cuis_call6(CUIS_NOTICE_PEEK, session, (uintptr_t)&native, (uintptr_t)&identifier, 0, 0, 0);
        if (kind <= 0) return kind;
        for (j = 0; j < GEM_WINDOW_LIMIT; j++)
            if (windows[j].id == identifier && windows[j].native == native) break;
        if (j == GEM_WINDOW_LIMIT) return EACCDN;
        window = &windows[j];
        for (j = 0; j < 8; j++) message[j] = 0;
        message[3] = window->id;
        if (kind == CUIS_NOTICE_REDRAW) {
            message[0] = WM_REDRAW;
            if (!mt_wind_calc(WC_WORK, window->kind, window->current.g_x, window->current.g_y,
                          window->current.g_w, window->current.g_h,
                          &work.g_x, &work.g_y, &work.g_w, &work.g_h, global)) return EIMBA;
            message[4] = work.g_x; message[5] = work.g_y;
            message[6] = work.g_w > 0 ? work.g_w : 0;
            message[7] = work.g_h > 0 ? work.g_h : 0;
        } else if (kind == CUIS_NOTICE_ACTIVATE) message[0] = WM_TOPPED;
        else return EINVFN;
        result = publish_notice(window,session,kind,message,global);
        /* A full pipe already has enough data to satisfy every legal read.
         * Leave the notice pending and let the caller free pipe capacity. */
        if (result == EAGAIN) { frame_notice_blocked = TRUE; return E_OK; }
        if (result < 0) return result;
    }
    return E_OK;
}

LONG aes_window_messages(LONG session, short *global)
{
    return window_messages(session,global,FALSE);
}

LONG aes_window_messages_poll(LONG session, short *global)
{
    LONG result;
    if (frame_overlays) return E_OK;
    if (!aes_frames_registered()) return window_messages(session,global,TRUE);
    /* APR_NOWAIT may paint only after a nonblocking acquisition. Nested
     * frame/renderer locks then borrow our already-owned display scope. */
    result = cuis_call6(CUIS_DISPLAY_LOCK,session,1,0,0,0,0);
    if (result == EAGAIN) { frame_display_blocked = TRUE; return E_OK; }
    if (result != E_OK) return result;
    result = window_messages(session,global,TRUE);
    if (cuis_call6(CUIS_DISPLAY_LOCK,session,0,0,0,0,0) != E_OK) return EACCDN;
    return result;
}

short mt_wind_get(short id, short field, short *a, short *b, short *c, short *d, short *global)
{
    struct gem_window *window;
    GRECT r;
    LONG values[4] = { 0, 0, 0, 0 }, result;
    short i;
    if (field == WF_TOP || field == WF_BOTTOM || field == WF_OWNER) {
        if (aes_application_session(global) < 0) return 0;
        if (field == WF_OWNER && id == 0) {
            values[0] = cuis_call6(CUIS_DISPLAY_INFO,3,0,0,0,0,0);
            if (values[0] < 0 || values[0] > 32767) return 0;
            r.g_x = values[0]; r.g_y = 1; r.g_h = 0;
            result = cuis_call6(CUIS_STACK_QUERY,1,(uintptr_t)&values[0],
                                (uintptr_t)&values[1],(uintptr_t)&values[2],0,0);
            if (result != E_OK || values[0] < 0 || values[0] > 32767) return 0;
            r.g_w = values[0];
            output(&r,a,b,c,d); return 1;
        }
        if (field == WF_OWNER)
            result = cuis_call6(CUIS_WINDOW_INFO, id, (uintptr_t)&values[0], (uintptr_t)&values[1],
                                (uintptr_t)&values[2], (uintptr_t)&values[3], 0);
        else
            result = cuis_call6(CUIS_STACK_QUERY, field == WF_BOTTOM, (uintptr_t)&values[0],
                                (uintptr_t)&values[1], (uintptr_t)&values[2], 0, 0);
        if (result != E_OK) return 0;
        for (i = 0; i < 4; i++) if (values[i] < 0 || values[i] > 32767) return 0;
        if (field == WF_OWNER && values[1] > 1) return 0;
        if (field == WF_BOTTOM) values[1] = values[2] = 0;
        r.g_x = (short)values[0]; r.g_y = (short)values[1];
        r.g_w = (short)values[2]; r.g_h = (short)values[3];
        output(&r, a, b, c, d); return 1;
    }
    if (aes_application_session(global) < 0) return 0;
    if (field == WF_SCREEN || field == WF_NEWDESK) {
        void *pointer;
        OBJECT *tree;
        LONG bytes = 0;
        WORD root = 0;
        if (field == WF_SCREEN) {
            pointer = aes_screen_buffer(&bytes,global);
            if (!pointer) return 0;
        } else {
            if (id || !aes_desktop_get(&tree,&root,global)) return 0;
            pointer = tree;
        }
        if ((UQUAD)(uintptr_t)pointer >> 32) return 0;
        r.g_x = (WORD)((uintptr_t)pointer >> 16); r.g_y = (WORD)(uintptr_t)pointer;
        r.g_w = field == WF_SCREEN ? (WORD)((ULONG)bytes >> 16) : root;
        r.g_h = field == WF_SCREEN ? (WORD)bytes : 0;
        output(&r,a,b,c,d); return 1;
    }
    if (field == WF_COLOR || field == WF_DCOLOR) {
        LONG top, bottom;
        WORD gadget;
        if (!a || *a < 0 || *a >= NUM_ELEM) return 0;
        gadget = *a;
        if (field == WF_COLOR) {
            window = lookup(id,global);
            if (!window) return 0;
            top = (UWORD)window->top_colours[gadget]; bottom = (UWORD)window->bottom_colours[gadget];
        } else {
            top = cuis_call6(CUIS_SETTING,gadget,0,0,0,0,0);
            bottom = cuis_call6(CUIS_SETTING,NUM_ELEM+gadget,0,0,0,0,0);
            if (top == EFILNF) top = (UWORD)aes_top_colours[gadget];
            if (bottom == EFILNF) bottom = (UWORD)aes_bottom_colours[gadget];
            if (top < 0 || top > 65535 || bottom < 0 || bottom > 65535) return 0;
        }
        r.g_x = gadget; r.g_y = (WORD)top; r.g_w = (WORD)bottom; r.g_h = 0x0f0f;
        output(&r,a,b,c,d); return 1;
    }
    if (id == 0) {
        switch (field) {
        case WF_WORKXYWH: case WF_FULLXYWH:
            if (!aes_desktop_geometry(&r,TRUE,global)) return 0;
            break;
        case WF_CURRXYWH: case WF_PREVXYWH:
            if (!aes_desktop_geometry(&r,FALSE,global)) return 0;
            break;
        case WF_FIRSTXYWH: case WF_NEXTXYWH:
            if (!aes_desktop_region(field == WF_FIRSTXYWH,&r,global)) return 0;
            break;
        case WF_KIND: r.g_x = r.g_y = r.g_w = r.g_h = 0; break;
        default: return 0;
        }
        output(&r,a,b,c,d); return 1;
    }
    window = lookup(id, global);
    if (!window) return 0;
    if (field == WF_NAME || field == WF_INFO) {
        const char *text = field == WF_NAME ? window->name : window->info;
        ULONG length;
        if (!(window->kind & (field == WF_NAME ? NAME : INFO)) || !a || !text) return 0;
        for (length = 0; length < 128 && text[length]; length++) ;
        if (length == 128) return 0;
        if (b == a+1) {
            if ((UQUAD)(uintptr_t)text >> 32) return 0;
            *a = (WORD)((uintptr_t)text >> 16); *b = (WORD)(uintptr_t)text;
        } else strcpy((char *)a,text);
        return 1;
    }
    switch (field) {
    case WF_FIRSTXYWH:
    case WF_NEXTXYWH:
        if (!visible_work(window, field == WF_FIRSTXYWH, &r, global)) return 0;
        break;
    case WF_HSLIDE: r.g_x = window->hslide; r.g_y = r.g_w = r.g_h = 0; break;
    case WF_VSLIDE: r.g_x = window->vslide; r.g_y = r.g_w = r.g_h = 0; break;
    case WF_HSLSIZE: r.g_x = window->hslsiz; r.g_y = r.g_w = r.g_h = 0; break;
    case WF_VSLSIZE: r.g_x = window->vslsiz; r.g_y = r.g_w = r.g_h = 0; break;
    case WF_KIND:
        r.g_x = window->kind; r.g_y = r.g_w = r.g_h = 0;
        break;
    case WF_CURRXYWH: r = window->current; break;
    case WF_PREVXYWH: r = window->previous; break;
    case WF_FULLXYWH: r = window->full; break;
    case WF_WORKXYWH:
        /* Match GEM draw_change: even a closed zero border is passed through
         * WC_WORK, rather than clamped to a new empty-work convention. */
        r = window->current;
        if (!mt_wind_calc(WC_WORK, window->kind, r.g_x, r.g_y, r.g_w, r.g_h,
                          &r.g_x, &r.g_y, &r.g_w, &r.g_h, global)) return 0;
        break;
    default: return 0;
    }
    output(&r, a, b, c, d);
    return 1;
}
short wind_get(short id, short field, short *a, short *b, short *c, short *d)
{
    return mt_wind_get(id, field, a, b, c, d, aes_global);
}
static WORD *slider_field(struct gem_window *window, WORD field)
{
    switch (field) {
    case WF_HSLIDE: return &window->hslide;
    case WF_VSLIDE: return &window->vslide;
    case WF_HSLSIZE: return &window->hslsiz;
    case WF_VSLSIZE: return &window->vslsiz;
    }
    return NULL;
}
static short local_wind_set(short id, short field, short a, short b, short c, short d, short *global)
{
    struct gem_window *window = lookup(id, global);
    WORD *slot, old;
    if (field == WF_NEWDESK)
        return id == 0 ? aes_desktop_set((OBJECT *)(uintptr_t)(((ULONG)(UWORD)a<<16)|(UWORD)b),c,global) : 0;
    if (field == WF_DCOLOR) {
        if (a < 0 || a >= NUM_ELEM) return 0;
        if (b == -1 && c == -1) return 1;
        return cuis_call6(CUIS_SETTING,a,(UWORD)b,NUM_ELEM+a,(UWORD)c,
                          (b != -1 ? 1 : 0) | (c != -1 ? 2 : 0),0) == E_OK;
    }
    if (!window) return 0;
    if (field == WF_COLOR) {
        WORD top, bottom;
        if (a < 0 || a >= NUM_ELEM) return 0;
        top = window->top_colours[a]; bottom = window->bottom_colours[a];
        if (b != -1) window->top_colours[a] = b;
        if (c != -1) window->bottom_colours[a] = c;
        if (!paint_window(window,global)) {
            window->top_colours[a] = top; window->bottom_colours[a] = bottom;
            (void)paint_window(window,global); return 0;
        }
        return 1;
    }
    slot = slider_field(window,field);
    if (slot) {
        BOOL size = field == WF_HSLSIZE || field == WF_VSLSIZE;
        BOOL vertical = field == WF_VSLIDE || field == WF_VSLSIZE;
        if (!(size && a == -1)) { if (a < 1) a = 1; if (a > 1000) a = 1000; }
        if (*slot == a) return 1;
        old = *slot; *slot = a;
        /* A deferred thumb drag uses frozen rectangles. A setter replaces
         * that geometry, so discard its proposal; page holds keep repeating. */
        if (thumb_control(window->control)) { window->control = 0; window->control_selected = FALSE; }
        if (window->open && (window->kind & (vertical ? VSLIDE : HSLIDE)) && !paint_window(window,global)) {
            *slot = old; (void)paint_window(window,global); return 0;
        }
        return 1;
    }
    if (field == WF_NAME || field == WF_INFO)
        return mt_wind_set_str(id,field,(const char *)(uintptr_t)(((ULONG)(UWORD)a<<16)|(UWORD)b),global);
    if (field == WF_CURRXYWH) {
        GRECT current = window->current, previous = window->previous;
        if (!configure(window, a, b, c, d, global)) return 0;
        discard_regions(&window->regions);
        window->control = 0; window->control_selected = FALSE;
        window->previous = window->current;
        window->current.g_x = a; window->current.g_y = b;
        window->current.g_w = c; window->current.g_h = d;
        if (!paint_window(window,global)) {
            if (configure(window,current.g_x,current.g_y,current.g_w,current.g_h,global)) {
                window->current = current; window->previous = previous;
                (void)paint_window(window,global);
            }
            return 0;
        }
        return 1;
    }
    if ((field == WF_TOP || field == WF_BOTTOM) && window->open) {
        if (cuis_call6(field == WF_TOP ? CUIS_WINDOW_RAISE : CUIS_WINDOW_LOWER,
                      window->native,0,0,0,0,0) != E_OK) return 0;
        return paint_window(window,global) ? 1 : 0;
    }
    return 0; /* Decorated fields migrate with the frame renderer. */
}
short wind_set(short id, short field, short a, short b, short c, short d)
{
    return mt_wind_set(id, field, a, b, c, d, aes_global);
}

short mt_wind_update(short code, short *global)
{
    LONG session = aes_application_session(global);
    ULONG function = CUIS_DISPLAY_LOCK;
    if (session < 0) return 0;
    if (code == END_MCTRL || code == BEG_MCTRL || code == (BEG_MCTRL | NO_BLOCK)) {
        function = CUIS_MOUSE_LOCK;
        code -= END_MCTRL;
    }
    if (code == END_UPDATE)
        return cuis_call6(function, session, 0, 0, 0, 0, 0) == E_OK ? 1 : 0;
    if (code != BEG_UPDATE && code != (BEG_UPDATE | NO_BLOCK)) return 0;
    return cuis_call6(function, session, 1, !(code & NO_BLOCK), 0, 0, 0) == E_OK ? 1 : 0;
}
short wind_update(short code) { return mt_wind_update(code, aes_global); }

short mt_wind_create(short kind, short x, short y, short w, short h, short *global)
{
    short result;
    if (!mt_wind_update(BEG_UPDATE, global)) return -1;
    result = local_wind_create(kind, x, y, w, h, global);
    if (!mt_wind_update(END_UPDATE, global)) return -1;
    return result;
}

short mt_wind_open(short id, short x, short y, short w, short h, short *global)
{
    short result;
    if (!mt_wind_update(BEG_UPDATE, global)) return 0;
    result = local_wind_open(id, x, y, w, h, global);
    if (!mt_wind_update(END_UPDATE, global)) return 0;
    return result;
}

short mt_wind_close(short id, short *global)
{
    short result;
    if (!mt_wind_update(BEG_UPDATE, global)) return 0;
    result = local_wind_close(id, global);
    if (!mt_wind_update(END_UPDATE, global)) return 0;
    return result;
}

short mt_wind_delete(short id, short *global)
{
    short result;
    if (!mt_wind_update(BEG_UPDATE, global)) return 0;
    result = local_wind_delete(id, global);
    if (!mt_wind_update(END_UPDATE, global)) return 0;
    return result;
}

short mt_wind_set(short id, short field, short a, short b, short c, short d, short *global)
{
    short result;
    if (!mt_wind_update(BEG_UPDATE, global)) return 0;
    result = local_wind_set(id, field, a, b, c, d, global);
    if (!mt_wind_update(END_UPDATE, global)) return 0;
    return result;
}

short mt_wind_set_str(short id, short field, const char *text, short *global)
{
    struct gem_window *window = lookup(id,global);
    char *old;
    BOOL ok;
    if (!window || !text || (field != WF_NAME && field != WF_INFO) ||
        !mt_wind_update(BEG_UPDATE,global)) return 0;
    old = field == WF_NAME ? window->name : window->info;
    if (field == WF_NAME) window->name = (char *)text;
    else window->info = (char *)text;
    ok = paint_window(window,global);
    if (!ok) {
        if (field == WF_NAME) window->name = old;
        else window->info = old;
        (void)paint_window(window,global);
    }
    if (!mt_wind_update(END_UPDATE,global)) ok = FALSE;
    return ok ? 1 : 0;
}
short wind_set_str(short id, short field, const char *text)
{
    return mt_wind_set_str(id,field,text,aes_global);
}

short mt_wind_new(short *global)
{
    LONG session = aes_application_session(global);
    if (session < 0 || cuis_call6(CUIS_WINDOWS_RESET,session,0,0,0,0,0) != E_OK) return 0;
    aes_windows_reset(); aes_graphics_wind_new();
    return 1;
}
short wind_new(void) { return mt_wind_new(aes_global); }
short mt_wind_set_ptr_int(short id, short field, void *pointer, short value, short *global)
{
    if (field == WF_NEWDESK) return id == 0 ? aes_desktop_set(pointer,value,global) : 0;
    if (field == WF_NAME || field == WF_INFO) return mt_wind_set_str(id,field,pointer,global);
    return 0;
}
short wind_set_ptr_int(short id, short field, void *pointer, short value)
{ return mt_wind_set_ptr_int(id,field,pointer,value,aes_global); }
short mt_wind_set_ptr(short id, short field, void *pointer, short *global)
{ return mt_wind_set_ptr_int(id,field,pointer,0,global); }
short wind_set_ptr(short id, short field, void *pointer)
{ return mt_wind_set_ptr(id,field,pointer,aes_global); }
short mt_wind_get_ptr(short id, short field, void **pointer, short *global)
{
    struct gem_window *window;
    void *value;
    OBJECT *tree;
    LONG bytes;
    if (!pointer || aes_application_session(global) < 0) return 0;
    if (field == WF_NEWDESK) {
        if (id || !aes_desktop_get(&tree,NULL,global)) return 0;
        value = tree;
    } else if (field == WF_SCREEN) {
        value = aes_screen_buffer(&bytes,global);
        if (!value) return 0;
    } else if ((field == WF_NAME || field == WF_INFO) && (window = lookup(id,global)) != NULL &&
               (window->kind & (field == WF_NAME ? NAME : INFO))) value = field == WF_NAME ? window->name : window->info;
    else return 0;
    *pointer = value; return 1;
}
short wind_get_ptr(short id, short field, void **pointer)
{ return mt_wind_get_ptr(id,field,pointer,aes_global); }
short mt_wind_get_int(short id, short field, short *value, short *global)
{ return mt_wind_get(id,field,value,NULL,NULL,NULL,global); }
short wind_get_int(short id, short field, short *value)
{ return mt_wind_get_int(id,field,value,aes_global); }
short mt_wind_set_int(short id, short field, short value, short *global)
{ return mt_wind_set(id,field,value,0,0,0,global); }
short wind_set_int(short id, short field, short value)
{ return mt_wind_set_int(id,field,value,aes_global); }
short mt_wind_get_grect(short id, short field, GRECT *rect, short *global)
{
    GRECT result = { 0,0,0,0 };
    if (!rect || !mt_wind_get(id,field,&result.g_x,&result.g_y,&result.g_w,&result.g_h,global)) return 0;
    *rect = result; return 1;
}
short wind_get_grect(short id, short field, GRECT *rect)
{ return mt_wind_get_grect(id,field,rect,aes_global); }
short mt_wind_set_grect(short id, short field, const GRECT *rect, short *global)
{ return rect ? mt_wind_set(id,field,rect->g_x,rect->g_y,rect->g_w,rect->g_h,global) : 0; }
short wind_set_grect(short id, short field, const GRECT *rect)
{ return mt_wind_set_grect(id,field,rect,aes_global); }
short mt_wind_create_grect(short kind, const GRECT *rect, short *global)
{ return rect ? mt_wind_create(kind,rect->g_x,rect->g_y,rect->g_w,rect->g_h,global) : -1; }
short wind_create_grect(short kind, const GRECT *rect)
{ return mt_wind_create_grect(kind,rect,aes_global); }
short mt_wind_open_grect(short id, const GRECT *rect, short *global)
{ return rect ? mt_wind_open(id,rect->g_x,rect->g_y,rect->g_w,rect->g_h,global) : 0; }
short wind_open_grect(short id, const GRECT *rect)
{ return mt_wind_open_grect(id,rect,aes_global); }
short mt_wind_calc_grect(short type, short kind, const GRECT *in, GRECT *out, short *global)
{
    GRECT result;
    if (!in || !out || !mt_wind_calc(type,kind,in->g_x,in->g_y,in->g_w,in->g_h,
        &result.g_x,&result.g_y,&result.g_w,&result.g_h,global)) return 0;
    *out = result; return 1;
}
short wind_calc_grect(short type, short kind, const GRECT *in, GRECT *out)
{ return mt_wind_calc_grect(type,kind,in,out,aes_global); }
