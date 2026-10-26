/* GEM IDs and plain-window geometry are private to each application.
 * CUIS retains authoritative ownership, visibility and global stacking. */
#include "gem.h"
#include "gemerror.h"
#include "transport.h"
#include "application.h"

#define GEM_WINDOW_LIMIT 64
static struct gem_window {
    short id;
    LONG native;
    BOOL open;
    ULONG region_index;
    GRECT current, previous, full;
} windows[GEM_WINDOW_LIMIT];

void aes_windows_reset(void)
{
    short i;
    for (i = 0; i < GEM_WINDOW_LIMIT; i++) windows[i].id = 0;
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

short mt_wind_calc(short type, short kind, short x, short y, short w, short h,
                   short *ox, short *oy, short *ow, short *oh, short *global)
{
    GRECT r;
    short inset;
    (void)global;
    if (kind || (type != WC_BORDER && type != WC_WORK)) return 0;
    inset = type == WC_WORK ? 1 : -1;
    r.g_x = x + inset; r.g_y = y + inset;
    r.g_w = w - 2 * inset; r.g_h = h - 2 * inset;
    output(&r, ox, oy, ow, oh);
    return 1;
}
short wind_calc(short type, short kind, short x, short y, short w, short h,
                short *ox, short *oy, short *ow, short *oh)
{
    return mt_wind_calc(type, kind, x, y, w, h, ox, oy, ow, oh, aes_global);
}

static short local_wind_create(short kind, short x, short y, short w, short h, short *global)
{
    struct gem_window *window;
    short i;
    LONG native, identifier;
    if (kind || aes_application_session(global) < 0) return -1;
    for (i = 0; i < GEM_WINDOW_LIMIT && windows[i].id; i++) ;
    if (i == GEM_WINDOW_LIMIT) return -1;
    /* GEM starts closed with zero current/previous geometry. CUIS requires
     * positive extents even for hidden metadata, so use an invisible placeholder. */
    native = cuis_call6(CUIS_WINDOW_CREATE, 0, 0, 1, 1, 0, 0);
    if (native < 0) return -1;
    identifier = cuis_call6(CUIS_WINDOW_IDENTIFY, native, 0, 0, 0, 0, 0);
    if (identifier <= 0 || identifier > 32767 ||
        cuis_call6(CUIS_WINDOW_OUTSET, native, 2, 2, 0, 0, 0) != E_OK ||
        cuis_call6(CUIS_WINDOW_POLICY, native, 1, 0, 0, 0, 0) != E_OK) {
        (void)cuis_call6(CUIS_WINDOW_DESTROY, native, 0, 0, 0, 0, 0);
        return -1;
    }
    window = &windows[i];
    window->native = native;
    window->open = FALSE;
    window->region_index = 0xffffffffUL;
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

static BOOL configure(struct gem_window *window, short x, short y, short w, short h)
{
    return w > 0 && h > 0 && cuis_call6(CUIS_WINDOW_CONFIGURE, window->native,
                     (QUAD)x, (QUAD)y, w, h, 0) == E_OK;
}
static short local_wind_open(short id, short x, short y, short w, short h, short *global)
{
    struct gem_window *window = lookup(id, global);
    if (!window || window->open || !configure(window, x, y, w, h)) return 0;
    if (cuis_call6(CUIS_WINDOW_SHOW, window->native, 0, 0, 0, 0, 0) != E_OK) return 0;
    window->current.g_x = x; window->current.g_y = y;
    window->current.g_w = w; window->current.g_h = h;
    window->previous = window->current;
    window->open = TRUE;
    window->region_index = 0xffffffffUL;
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
    window->region_index = 0xffffffffUL;
    return 1;
}
short wind_close(short id) { return mt_wind_close(id, aes_global); }
static short local_wind_delete(short id, short *global)
{
    struct gem_window *window = lookup(id, global);
    /* Native destruction hides an open window and reclaims its input queues. */
    if (!window || cuis_call6(CUIS_WINDOW_DESTROY, window->native, 0, 0, 0, 0, 0) != E_OK) return 0;
    window->id = 0;
    return 1;
}
short wind_delete(short id) { return mt_wind_delete(id, aes_global); }

static short visible_work(struct gem_window *window, BOOL first, GRECT *out, short *global)
{
    LONG state[4], result, left, top, right, bottom, edge;
    ULONG index = first ? 0 : window->region_index;
    GRECT work;
    if (!mt_wind_calc(WC_WORK, 0, window->current.g_x, window->current.g_y,
                  window->current.g_w, window->current.g_h,
                  &work.g_x, &work.g_y, &work.g_w, &work.g_h, global)) return 0;
    for (;;) {
        if (index == 0xffffffffUL) result = 0;
        else result = cuis_call6(CUIS_WINDOW_REGION, window->native, index,
                                 (uintptr_t)&state[0], (uintptr_t)&state[1],
                                 (uintptr_t)&state[2], (uintptr_t)&state[3]);
        if (result < 0) return 0;
        if (!result) {
            out->g_x = out->g_y = out->g_w = out->g_h = 0;
            window->region_index = 0xffffffffUL;
            return 1;
        }
        index++;
        left = state[0] > work.g_x ? state[0] : work.g_x;
        top = state[1] > work.g_y ? state[1] : work.g_y;
        right = state[0] + state[2]; edge = (LONG)work.g_x + work.g_w;
        if (right > edge) right = edge;
        bottom = state[1] + state[3]; edge = (LONG)work.g_y + work.g_h;
        if (bottom > edge) bottom = edge;
        if (work.g_w > 0 && work.g_h > 0 && right > left && bottom > top) {
            out->g_x = (short)left; out->g_y = (short)top;
            out->g_w = (short)(right - left); out->g_h = (short)(bottom - top);
            window->region_index = index;
            return 1;
        }
    }
}

LONG aes_window_messages(LONG session, short *global)
{
    LONG native, identifier, kind, result;
    short message[8], i, j;
    struct gem_window *window;
    GRECT work;
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
            mt_wind_calc(WC_WORK, 0, window->current.g_x, window->current.g_y,
                          window->current.g_w, window->current.g_h,
                          &work.g_x, &work.g_y, &work.g_w, &work.g_h, global);
            message[4] = work.g_x; message[5] = work.g_y;
            message[6] = work.g_w > 0 ? work.g_w : 0;
            message[7] = work.g_h > 0 ? work.g_h : 0;
        } else if (kind == CUIS_NOTICE_ACTIVATE) message[0] = WM_TOPPED;
        else return EINVFN;
        result = cuis_call6(CUIS_NOTICE_PUBLISH, session, native, kind, (uintptr_t)message, sizeof(message), 0);
        /* A full pipe already has enough data to satisfy every legal read.
         * Leave the notice pending and let the caller free pipe capacity. */
        if (result == EAGAIN) return E_OK;
        if (result < 0) return result;
    }
    return E_OK;
}

short mt_wind_get(short id, short field, short *a, short *b, short *c, short *d, short *global)
{
    struct gem_window *window;
    GRECT r;
    LONG values[4] = { 0, 0, 0, 0 }, result;
    short i;
    if (field == WF_TOP || field == WF_BOTTOM || field == WF_OWNER) {
        if (aes_application_session(global) < 0) return 0;
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
    window = lookup(id, global);
    if (!window) return 0;
    switch (field) {
    case WF_FIRSTXYWH:
    case WF_NEXTXYWH:
        if (!visible_work(window, field == WF_FIRSTXYWH, &r, global)) return 0;
        break;
    case WF_KIND:
        r.g_x = r.g_y = r.g_w = r.g_h = 0;
        break;
    case WF_CURRXYWH: r = window->current; break;
    case WF_PREVXYWH: r = window->previous; break;
    case WF_FULLXYWH: r = window->full; break;
    case WF_WORKXYWH:
        /* Match GEM draw_change: even a closed zero border is passed through
         * WC_WORK, rather than clamped to a new empty-work convention. */
        r = window->current;
        if (!mt_wind_calc(WC_WORK, 0, r.g_x, r.g_y, r.g_w, r.g_h,
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
static short local_wind_set(short id, short field, short a, short b, short c, short d, short *global)
{
    struct gem_window *window = lookup(id, global);
    if (!window) return 0;
    if (field == WF_CURRXYWH) {
        if (!configure(window, a, b, c, d)) return 0;
        window->region_index = 0xffffffffUL;
        window->previous = window->current;
        window->current.g_x = a; window->current.g_y = b;
        window->current.g_w = c; window->current.g_h = d;
        return 1;
    }
    if ((field == WF_TOP || field == WF_BOTTOM) && window->open)
        return cuis_call6(field == WF_TOP ? CUIS_WINDOW_RAISE : CUIS_WINDOW_LOWER,
                          window->native, 0, 0, 0, 0, 0) == E_OK ? 1 : 0;
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
