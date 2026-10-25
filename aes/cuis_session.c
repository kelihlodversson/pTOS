/* Process-owned native GUI sessions; never interpret GEM/AES wire formats. */
#include "emutos.h"
#include "cuis.h"
#include "cuis_region.h"
#include "screen_mode.h"
#include "gemerror.h"
#include "kheap.h"
#include "../bdos/kproc.h"

extern PD *run;

typedef struct cuis_window {
    struct cuis_window *next;
    struct cuis_window *znext;
    ULONG handle;
    LONG x, y, width, height;
    BOOL visible, redraw;
} CUIS_WINDOW;

typedef struct cuis_client {
    struct cuis_client *next;
    PD *owner;
    ULONG handle;
    CUIS_WINDOW *windows;
    ULONG window_count;
} CUIS_CLIENT;
static CUIS_CLIENT *clients;
static ULONG next_handle;
static ULONG next_window_handle;
/* Front to back, containing only visible windows from all sessions. */
static CUIS_WINDOW *window_front;
static CUIS_CLIENT *update_owner;
static CUIS_WINDOW *update_window;
static CUIS_RECT *update_clips;
static ULONG update_count;

static void end_update(void)
{
    if (update_clips) kfree(update_clips);
    update_clips = NULL;
    update_count = 0;
    update_owner = NULL;
    update_window = NULL;
}

static LONG begin_update(CUIS_CLIENT *client, CUIS_WINDOW *window)
{
    CUIS_RECT *a, *b, *swap;
    CUIS_WINDOW *cover;
    ULONG count = 1;
    LONG result, right, bottom;
    SCREEN_MODE_DESC mode;

    if (update_owner) return EAGAIN;
    if (!window->visible) return EINVAL;
    screen_get_current_mode_desc(&mode);
    if (!mode.width || !mode.height) return EINVFN;
    a = kalloc(CUIS_CLIP_LIMIT * sizeof(*a));
    if (!a) return ENSMEM;
    b = kalloc(CUIS_CLIP_LIMIT * sizeof(*b));
    if (!b) { kfree(a); return ENSMEM; }
    a[0].x = window->x > 0 ? window->x : 0;
    a[0].y = window->y > 0 ? window->y : 0;
    right = window->x + window->width;
    bottom = window->y + window->height;
    if (right > mode.width) right = mode.width;
    if (bottom > mode.height) bottom = mode.height;
    if (right <= a[0].x || bottom <= a[0].y) count = 0;
    else {
        a[0].width = right - a[0].x;
        a[0].height = bottom - a[0].y;
    }
    for (cover = window_front; cover != window && count; cover = cover->znext) {
        CUIS_RECT occluder;
        occluder.x = cover->x; occluder.y = cover->y;
        occluder.width = cover->width; occluder.height = cover->height;
        result = cuis_region_subtract(a, count, &occluder, b, CUIS_CLIP_LIMIT);
        if (result < 0) { kfree(a); kfree(b); return result; }
        count = (ULONG)result;
        swap = a; a = b; b = swap;
    }
    kfree(b);
    update_owner = client;
    update_window = window;
    update_clips = a;
    update_count = count;
    return E_OK;
}

static void damage_windows(void)
{
    CUIS_WINDOW *window;
    for (window = window_front; window; window = window->znext)
        window->redraw = TRUE;
}

static void unlink_visible(CUIS_WINDOW *window)
{
    CUIS_WINDOW **link;
    for (link = &window_front; *link && *link != window;
         link = &(*link)->znext) ;
    if (*link) *link = window->znext;
    window->znext = NULL;
}

static void hide_window(CUIS_WINDOW *window)
{
    if (!window->visible) return;
    unlink_visible(window);
    window->visible = FALSE;
    window->redraw = FALSE;
    damage_windows();
}

static CUIS_CLIENT **find_owner(PD *owner)
{
    CUIS_CLIENT **link;
    for (link = &clients; *link && (*link)->owner != owner;
         link = &(*link)->next) ;
    return link;
}

void cuis_process_exit(PD *owner)
{
    CUIS_CLIENT **link = find_owner(owner), *client = *link;
    if (client) {
        CUIS_WINDOW *window;
        if (update_owner == client) end_update();
        while ((window = client->windows) != NULL) {
            client->windows = window->next;
            hide_window(window);
            kfree(window);
        }
        *link = client->next;
        kfree(client);
    }
}

#if CONF_WITH_X86_64_MEMTEST
BOOL cuis_update_active(void) { return update_owner != NULL; }
ULONG cuis_visible_window_count(void)
{
    CUIS_WINDOW *window;
    ULONG count = 0;
    for (window = window_front; window; window = window->znext) count++;
    return count;
}
ULONG cuis_window_count(void)
{
    CUIS_CLIENT *client;
    ULONG count = 0;
    for (client = clients; client; client = client->next)
        count += client->window_count;
    return count;
}
ULONG cuis_session_count(void)
{
    CUIS_CLIENT *client;
    ULONG count = 0;
    for (client = clients; client; client = client->next) count++;
    return count;
}
#endif

LONG cuis_service(ULONG function, UQUAD handle)
{
    CUIS_CLIENT *client;

    switch (function) {
    case CUIS_SESSION_START:
        client = *find_owner(run);
        if (client) return (LONG)client->handle;
        if (!kproc_pid(run)) return EACCDN;
        /* Keep every successful result non-negative and never recycle a
         * handle. Stop at the signed 32-bit limit instead of wrapping. */
        if (next_handle > 0x7fffffffUL) return ENSMEM;
        client = kalloc(sizeof(*client));
        if (!client) return ENSMEM;
        client->windows = NULL;
        client->window_count = 0;
        client->owner = run;
        client->handle = next_handle++;
        client->next = clients;
        clients = client;
        return (LONG)client->handle;
    case CUIS_SESSION_QUERY:
    case CUIS_SESSION_END:
        if (handle > 0x7fffffffULL) return EINVAL;
        client = *find_owner(run);
        if (!client || handle != client->handle) return EACCDN;
        if (function == CUIS_SESSION_END) cuis_process_exit(run);
        return E_OK;
    default:
        return EINVFN;
    }
}

/* Geometry slots contain sign-extended LONGs, not truncated machine words.
 * Positive extents and representable exclusive right/bottom edges keep later
 * intersection arithmetic well defined. Negative origins are permitted. */
static BOOL valid_geometry(UQUAD x, UQUAD y, UQUAD width, UQUAD height)
{
    return x == (UQUAD)(QUAD)(LONG)x && y == (UQUAD)(QUAD)(LONG)y &&
           width > 0 && width <= 0x7fffffffULL &&
           height > 0 && height <= 0x7fffffffULL &&
           (QUAD)x + (QUAD)width <= 0x7fffffffLL &&
           (QUAD)y + (QUAD)height <= 0x7fffffffLL;
}

LONG cuis_window_service(ULONG function, UQUAD a, UQUAD b, UQUAD c,
                         UQUAD d, UQUAD e)
{
    CUIS_CLIENT *client;
    CUIS_WINDOW **link, **back, *window;

    if (function < CUIS_WINDOW_CREATE || function > CUIS_UPDATE_END)
        return EINVFN;
    client = *find_owner(run);
    if (!client) return EACCDN;
    if (function == CUIS_WINDOW_CREATE) {
        if (update_owner) return EAGAIN;
        if (!valid_geometry(a, b, c, d)) return EINVAL;
        if (client->window_count >= CUIS_WINDOWS_PER_SESSION ||
            next_window_handle > 0x7fffffffUL) return ENSMEM;
        window = kalloc(sizeof(*window));
        if (!window) return ENSMEM;
        window->handle = next_window_handle++;
        window->znext = NULL;
        window->visible = FALSE;
        window->redraw = FALSE;
        window->x = (LONG)a; window->y = (LONG)b;
        window->width = (LONG)c; window->height = (LONG)d;
        window->next = client->windows;
        client->windows = window;
        client->window_count++;
        return (LONG)window->handle;
    }
    if (a > 0x7fffffffULL) return EINVAL;
    for (link = &client->windows; *link && (*link)->handle != a;
         link = &(*link)->next) ;
    window = *link;
    if (!window) return EACCDN;
    if (function == CUIS_UPDATE_BEGIN) return begin_update(client, window);
    if (function == CUIS_UPDATE_END) {
        if (update_owner != client || update_window != window) return EACCDN;
        end_update();
        return E_OK;
    }
    if (update_owner && function != CUIS_WINDOW_REDRAW) return EAGAIN;
    switch (function) {
    case CUIS_WINDOW_DESTROY:
        hide_window(window);
        *link = window->next;
        client->window_count--;
        kfree(window);
        break;
    case CUIS_WINDOW_CONFIGURE:
        if (!valid_geometry(b, c, d, e)) return EINVAL;
        if (window->x == (LONG)b && window->y == (LONG)c &&
            window->width == (LONG)d && window->height == (LONG)e) break;
        window->x = (LONG)b; window->y = (LONG)c;
        window->width = (LONG)d; window->height = (LONG)e;
        if (window->visible) damage_windows();
        break;
    case CUIS_WINDOW_SHOW:
        if (window->visible) break;
        window->visible = TRUE;
        window->znext = window_front;
        window_front = window;
        damage_windows();
        break;
    case CUIS_WINDOW_HIDE:
        hide_window(window);
        break;
    case CUIS_WINDOW_RAISE:
        if (!window->visible) return EINVAL;
        if (window == window_front) break;
        unlink_visible(window);
        window->znext = window_front;
        window_front = window;
        damage_windows();
        break;
    case CUIS_WINDOW_LOWER:
        if (!window->visible) return EINVAL;
        if (!window->znext) break;
        unlink_visible(window);
        for (back = &window_front; *back; back = &(*back)->znext) ;
        *back = window;
        damage_windows();
        break;
    case CUIS_WINDOW_REDRAW:
        /* Polling is nonblocking and does not grant permission to draw.
         * Later clipping/display coordination must guard actual rendering. */
        if (!window->redraw) return 0;
        window->redraw = FALSE;
        return 1;
    }
    return E_OK;
}

/* Indexed copyout has no mutable cursor: failed copyout can be retried without
 * dropping a region. Validate every destination before writing any output. */
LONG cuis_clip_service(UQUAD handle, UQUAD index, UQUAD x, UQUAD y,
                       UQUAD width, UQUAD height)
{
    CUIS_RECT *rect;
    if (handle > 0x7fffffffULL || index > 0xffffffffULL) return EINVAL;
    if (!update_owner || update_owner->owner != run ||
        update_window->handle != handle) return EACCDN;
    if (index >= update_count) return 0;
    if (!kproc_validate_user_write(x, sizeof(LONG)) ||
        !kproc_validate_user_write(y, sizeof(LONG)) ||
        !kproc_validate_user_write(width, sizeof(LONG)) ||
        !kproc_validate_user_write(height, sizeof(LONG))) return EIMBA;
    rect = &update_clips[(ULONG)index];
    if (!kproc_copy_to_user(x, &rect->x, sizeof(LONG)) ||
        !kproc_copy_to_user(y, &rect->y, sizeof(LONG)) ||
        !kproc_copy_to_user(width, &rect->width, sizeof(LONG)) ||
        !kproc_copy_to_user(height, &rect->height, sizeof(LONG))) return EIMBA;
    return 1;
}

/* 0 retains legacy native VDI behavior; 1 requires the immutable owned
 * snapshot. A different update owner blocks native screen writes. */
LONG cuis_vdi_policy(void)
{
    if (update_owner) return update_owner->owner == run ? 1 : EAGAIN;
    return *find_owner(run) ? EACCDN : 0;
}
BOOL cuis_vdi_clip(ULONG index, CUIS_RECT *rect)
{
    if (!update_owner || update_owner->owner != run || index >= update_count)
        return FALSE;
    *rect = update_clips[index];
    return TRUE;
}
