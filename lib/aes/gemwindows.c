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

short mt_wind_create(short kind, short x, short y, short w, short h, short *global)
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
    if (identifier <= 0 || identifier > 32767) {
        (void)cuis_call6(CUIS_WINDOW_DESTROY, native, 0, 0, 0, 0, 0);
        return -1;
    }
    window = &windows[i];
    window->native = native;
    window->open = FALSE;
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
short mt_wind_open(short id, short x, short y, short w, short h, short *global)
{
    struct gem_window *window = lookup(id, global);
    if (!window || window->open || !configure(window, x, y, w, h)) return 0;
    if (cuis_call6(CUIS_WINDOW_SHOW, window->native, 0, 0, 0, 0, 0) != E_OK) return 0;
    window->current.g_x = x; window->current.g_y = y;
    window->current.g_w = w; window->current.g_h = h;
    window->previous = window->current;
    window->open = TRUE;
    return 1;
}
short wind_open(short id, short x, short y, short w, short h)
{
    return mt_wind_open(id, x, y, w, h, aes_global);
}
short mt_wind_close(short id, short *global)
{
    struct gem_window *window = lookup(id, global);
    if (!window || !window->open ||
        cuis_call6(CUIS_WINDOW_HIDE, window->native, 0, 0, 0, 0, 0) != E_OK) return 0;
    window->previous = window->current;
    window->current.g_x = window->current.g_y = 0;
    window->current.g_w = window->current.g_h = 0;
    window->open = FALSE;
    return 1;
}
short wind_close(short id) { return mt_wind_close(id, aes_global); }
short mt_wind_delete(short id, short *global)
{
    struct gem_window *window = lookup(id, global);
    /* Native destruction hides an open window and reclaims its input queues. */
    if (!window || cuis_call6(CUIS_WINDOW_DESTROY, window->native, 0, 0, 0, 0, 0) != E_OK) return 0;
    window->id = 0;
    return 1;
}
short wind_delete(short id) { return mt_wind_delete(id, aes_global); }

short mt_wind_get(short id, short field, short *a, short *b, short *c, short *d, short *global)
{
    struct gem_window *window = lookup(id, global);
    GRECT r;
    if (!window) return 0;
    switch (field) {
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
short mt_wind_set(short id, short field, short a, short b, short c, short d, short *global)
{
    struct gem_window *window = lookup(id, global);
    if (!window) return 0;
    if (field == WF_CURRXYWH) {
        if (!configure(window, a, b, c, d)) return 0;
        window->previous = window->current;
        window->current.g_x = a; window->current.g_y = b;
        window->current.g_w = c; window->current.g_h = d;
        return 1;
    }
    return 0; /* Focus/stacking and decorated fields migrate together later. */
}
short wind_set(short id, short field, short a, short b, short c, short d)
{
    return mt_wind_set(id, field, a, b, c, d, aes_global);
}
