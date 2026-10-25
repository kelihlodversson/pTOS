/* Process-owned native GUI sessions; never interpret GEM/AES wire formats. */
#include "emutos.h"
#include "cuis.h"
#include "cuis_region.h"
#include "cuis_input.h"
#include "screen_mode.h"
#include "gemerror.h"
#include "kheap.h"
#include "../bdos/kproc.h"

extern PD *run;

#define CUIS_MESSAGE_CAPACITY 16U
#define CUIS_MESSAGE_GRANTS 16U
#define CUIS_KEY_CAPACITY 16U
#define CUIS_POINTER_CAPACITY 16U
#define CUIS_WINDOW_LIMIT 256UL
typedef struct {
    LONG x, y;
    ULONG buttons, state;
} CUIS_POINTER;
typedef struct cuis_window {
    struct cuis_window *next;
    struct cuis_window *znext;
    ULONG handle;
    LONG x, y, width, height;
    BOOL visible, redraw;
    ULONG keys[CUIS_KEY_CAPACITY];
    UWORD key_head, key_count;
    CUIS_POINTER pointer[CUIS_POINTER_CAPACITY];
    UWORD pointer_head, pointer_count;
    BOOL pointer_overflow;
} CUIS_WINDOW;

typedef struct {
    ULONG sender;
    ULONG words[4];
} CUIS_MESSAGE;

typedef struct cuis_client {
    struct cuis_client *next;
    PD *owner;
    ULONG handle;
    CUIS_WINDOW *windows;
    ULONG window_count;
    CUIS_MESSAGE messages[CUIS_MESSAGE_CAPACITY];
    UWORD message_head, message_count;
    ULONG grants[CUIS_MESSAGE_GRANTS];
    UWORD grant_count;
} CUIS_CLIENT;
static CUIS_CLIENT *clients;
static ULONG next_handle;
static ULONG next_window_handle;
/* Front to back, containing only visible windows from all sessions. */
static CUIS_WINDOW *window_front;
static CUIS_WINDOW *focus_window;
static ULONG window_total;
static CUIS_WINDOW *pointer_hover, *pointer_capture;
static LONG pointer_x, pointer_y, pointer_width, pointer_height;
static ULONG pointer_buttons;
static BOOL pointer_initialized, pointer_seen, pointer_suppressed, pointer_legacy_drag;
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

/* IRQ keyboard delivery uses only the published focus pointer and its
 * resident bounded queue. Remove that pointer before a window can be freed. */
static BOOL set_focus(CUIS_WINDOW *window)
{
    UQUAD flags = cuis_input_lock();
    BOOL changed = focus_window != window;
    if (changed) {
        if (focus_window) focus_window->key_head = focus_window->key_count = 0;
        if (window) window->key_head = window->key_count = 0;
        focus_window = window;
    }
    cuis_input_unlock(flags);
    return changed;
}

BOOL cuis_keyboard_event(ULONG key)
{
    UQUAD flags = cuis_input_lock();
    CUIS_WINDOW *window = focus_window;
    if (window && window->key_count < CUIS_KEY_CAPACITY) {
        window->keys[(window->key_head + window->key_count) % CUIS_KEY_CAPACITY] = key;
        window->key_count++;
    }
    cuis_input_unlock(flags);
    /* A full focused queue drops the newest key, never leaks it to BIOS. */
    return window != NULL;
}

/* All routing and topology commits use the same short IRQ exclusion.
 * The global window bound also bounds hit testing and focus redraw work. */
static CUIS_WINDOW *pointer_hit(void)
{
    CUIS_WINDOW *window;
    for (window = window_front; window; window = window->znext)
        if (pointer_x >= window->x && pointer_x < window->x + window->width &&
            pointer_y >= window->y && pointer_y < window->y + window->height)
            return window;
    return NULL;
}
static void pointer_enqueue(CUIS_WINDOW *window, ULONG changed, BOOL inside)
{
    CUIS_POINTER *event;
    if (!window) return;
    if (window->pointer_count) {
        event = &window->pointer[(window->pointer_head + window->pointer_count - 1) % CUIS_POINTER_CAPACITY];
        if (!changed && !(event->state & CUIS_POINTER_CHANGED) &&
            ((event->state & CUIS_POINTER_INSIDE) != 0) == inside &&
            event->buttons == pointer_buttons) {
            event->x = pointer_x; event->y = pointer_y;
            return;
        }
    }
    if (window->pointer_count == CUIS_POINTER_CAPACITY) {
        window->pointer_head = window->pointer_count = 0;
        window->pointer_overflow = TRUE;
        changed = 0; /* Retain a resynchronization snapshot after lost edges. */
    }
    event = &window->pointer[(window->pointer_head + window->pointer_count) % CUIS_POINTER_CAPACITY];
    event->x = pointer_x; event->y = pointer_y;
    event->buttons = pointer_buttons;
    event->state = changed | (inside ? CUIS_POINTER_INSIDE : 0);
    window->pointer_count++;
}
static BOOL route_pointer(ULONG changed)
{
    CUIS_WINDOW *hit, *target;
    BOOL consumed;
    if (!pointer_initialized || pointer_suppressed || pointer_legacy_drag) return FALSE;
    hit = pointer_hit();
    if (!pointer_capture && (changed & pointer_buttons)) {
        if (hit) {
            pointer_capture = hit;
            if (set_focus(hit)) damage_windows();
        } else {
            if (pointer_hover) pointer_enqueue(pointer_hover, 0, FALSE);
            pointer_hover = NULL;
            if (set_focus(NULL)) damage_windows();
            pointer_legacy_drag = TRUE;
            return FALSE;
        }
    }
    target = pointer_capture ? pointer_capture : hit;
    consumed = target != NULL;
    if (pointer_hover && pointer_hover != target) pointer_enqueue(pointer_hover, 0, pointer_hover == hit);
    pointer_hover = target;
    pointer_enqueue(target, changed, target == hit);
    if (pointer_capture && !pointer_buttons) {
        pointer_capture = NULL;
        if (pointer_hover != hit) {
            pointer_enqueue(pointer_hover, 0, pointer_hover == hit);
            pointer_hover = hit;
            pointer_enqueue(hit, 0, TRUE);
        }
    }
    return consumed;
}
static void refresh_pointer_bounds(void)
{
    SCREEN_MODE_DESC mode;
    LONG x, y, old_x, old_y;
    ULONG buttons;
    BOOL legacy;
    UQUAD flags;
    screen_get_current_mode_desc(&mode);
    if (!mode.width || !mode.height) return;
    flags = cuis_input_lock();
    pointer_width = mode.width > 32768U ? 32768L : mode.width;
    pointer_height = mode.height > 32768U ? 32768L : mode.height;
    old_x = pointer_x; old_y = pointer_y;
    legacy = cuis_input_pointer_state(&x, &y, &buttons);
    if (legacy) {
        cuis_input_clip_pointer(&x, &y);
        pointer_x = x; pointer_y = y;
    }
    if (!pointer_initialized) {
        if (legacy) {
            if (!pointer_seen) pointer_buttons = buttons & 3;
        } else {
            pointer_x = pointer_width / 2; pointer_y = pointer_height / 2;
        }
        pointer_legacy_drag = pointer_buttons != 0;
        pointer_initialized = TRUE;
    }
    if (pointer_x >= pointer_width) pointer_x = pointer_width - 1;
    if (pointer_y >= pointer_height) pointer_y = pointer_height - 1;
    if (pointer_x < 0) pointer_x = 0;
    if (pointer_y < 0) pointer_y = 0;
    if (pointer_x != old_x || pointer_y != old_y) route_pointer(0);
    cuis_input_unlock(flags);
}
BOOL cuis_pointer_packet(WORD dx, WORD dy, ULONG buttons, LONG *out_x, LONG *out_y)
{
    UQUAD flags = cuis_input_lock();
    LONG x, y;
    ULONG old = pointer_buttons, ignored;
    BOOL consumed, legacy;
    pointer_seen = TRUE;
    pointer_buttons = buttons & 3;
    if (!pointer_initialized) {
        cuis_input_unlock(flags);
        return FALSE;
    }
    legacy = cuis_input_pointer_state(&x, &y, &ignored);
    if (!legacy) { x = pointer_x; y = pointer_y; }
    x += dx; y += dy;
    if (legacy) cuis_input_clip_pointer(&x, &y);
    pointer_x = x < 0 ? 0 : x >= pointer_width ? pointer_width - 1 : x;
    pointer_y = y < 0 ? 0 : y >= pointer_height ? pointer_height - 1 : y;
    if (out_x) *out_x = pointer_x;
    if (out_y) *out_y = pointer_y;
    if (pointer_suppressed || pointer_legacy_drag) {
        consumed = pointer_suppressed;
        if (!pointer_buttons) {
            pointer_suppressed = pointer_legacy_drag = FALSE;
            route_pointer(0); /* No foreign release without its matching press. */
        }
    } else consumed = route_pointer(old ^ pointer_buttons);
    cuis_input_unlock(flags);
    return consumed;
}
void cuis_mouse_packet(SBYTE *packet)
{
    if (!cuis_input_standard_pointer() && ((UBYTE)packet[0] & 0xf8) == 0xf8 &&
        cuis_pointer_packet(packet[1], packet[2],
            ((packet[0] & 2) >> 1) | ((packet[0] & 1) << 1), NULL, NULL)) return;
    cuis_input_call_pointer(packet);
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
    UQUAD flags = cuis_input_lock();
    if (!window->visible) { cuis_input_unlock(flags); return; }
    unlink_visible(window);
    window->visible = FALSE;
    window->redraw = FALSE;
    window->pointer_head = window->pointer_count = 0;
    window->pointer_overflow = FALSE;
    if (pointer_capture == window) {
        pointer_capture = NULL;
        pointer_suppressed = pointer_buttons != 0;
    }
    if (pointer_hover == window) pointer_hover = NULL;
    if (focus_window == window) set_focus(window_front);
    route_pointer(0);
    cuis_input_unlock(flags);
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
        CUIS_CLIENT *peer;
        UWORD i;
        /* Handles are never recycled. Remove expired grants without removing
         * already accepted messages, whose sender remains historical identity. */
        for (peer = clients; peer; peer = peer->next)
            for (i = 0; i < peer->grant_count; i++)
                if (peer->grants[i] == client->handle) {
                    peer->grants[i] = peer->grants[--peer->grant_count];
                    break;
                }
        if (update_owner == client) end_update();
        while ((window = client->windows) != NULL) {
            client->windows = window->next;
            hide_window(window);
            window_total--;
            kfree(window);
        }
        *link = client->next;
        kfree(client);
    }
}

#if CONF_WITH_X86_64_MEMTEST
BOOL cuis_update_active(void) { return update_owner != NULL; }
BOOL cuis_focus_active(void) { return focus_window != NULL; }
BOOL cuis_pointer_attached(void) { return pointer_hover != NULL || pointer_capture != NULL; }
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
        refresh_pointer_bounds();
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
        client->message_head = client->message_count = client->grant_count = 0;
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
    UQUAD flags;
    BOOL changed;

    if (function < CUIS_WINDOW_CREATE || function > CUIS_FOCUS_RELEASE ||
        function == CUIS_WINDOW_CLIP)
        return EINVFN;
    client = *find_owner(run);
    if (!client) return EACCDN;
    if (function == CUIS_WINDOW_CREATE) {
        if (update_owner) return EAGAIN;
        if (!valid_geometry(a, b, c, d)) return EINVAL;
        if (window_total >= CUIS_WINDOW_LIMIT || client->window_count >= CUIS_WINDOWS_PER_SESSION ||
            next_window_handle > 0x7fffffffUL) return ENSMEM;
        window = kalloc(sizeof(*window));
        if (!window) return ENSMEM;
        window->handle = next_window_handle++;
        window->znext = NULL;
        window->visible = FALSE;
        window->redraw = FALSE;
        window->key_head = window->key_count = 0;
        window->pointer_head = window->pointer_count = 0;
        window->pointer_overflow = FALSE;
        window->x = (LONG)a; window->y = (LONG)b;
        window->width = (LONG)c; window->height = (LONG)d;
        window->next = client->windows;
        client->windows = window;
        client->window_count++;
        window_total++;
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
    if (function == CUIS_FOCUS_QUERY) return focus_window == window ? 1 : 0;
    if (update_owner && function != CUIS_WINDOW_REDRAW) return EAGAIN;
    switch (function) {
    case CUIS_WINDOW_DESTROY:
        hide_window(window);
        *link = window->next;
        client->window_count--;
        window_total--;
        kfree(window);
        break;
    case CUIS_WINDOW_CONFIGURE:
        if (!valid_geometry(b, c, d, e)) return EINVAL;
        if (window->x == (LONG)b && window->y == (LONG)c &&
            window->width == (LONG)d && window->height == (LONG)e) break;
        flags = cuis_input_lock();
        window->x = (LONG)b; window->y = (LONG)c;
        window->width = (LONG)d; window->height = (LONG)e;
        route_pointer(0);
        cuis_input_unlock(flags);
        if (window->visible) damage_windows();
        break;
    case CUIS_WINDOW_SHOW:
        if (window->visible) break;
        flags = cuis_input_lock();
        window->visible = TRUE;
        window->znext = window_front;
        window_front = window;
        route_pointer(0);
        cuis_input_unlock(flags);
        damage_windows();
        break;
    case CUIS_WINDOW_HIDE:
        hide_window(window);
        break;
    case CUIS_WINDOW_RAISE:
        if (!window->visible) return EINVAL;
        if (window == window_front) break;
        flags = cuis_input_lock();
        unlink_visible(window);
        window->znext = window_front;
        window_front = window;
        route_pointer(0);
        cuis_input_unlock(flags);
        damage_windows();
        break;
    case CUIS_WINDOW_LOWER:
        if (!window->visible) return EINVAL;
        if (!window->znext) break;
        flags = cuis_input_lock();
        unlink_visible(window);
        for (back = &window_front; *back; back = &(*back)->znext) ;
        *back = window;
        route_pointer(0);
        cuis_input_unlock(flags);
        damage_windows();
        break;
    case CUIS_FOCUS_REQUEST:
        if (!window->visible) return EINVAL;
        if (window != window_front) return EACCDN;
        if (set_focus(window)) damage_windows();
        break;
    case CUIS_FOCUS_RELEASE:
        flags = cuis_input_lock();
        changed = focus_window == window && set_focus(NULL);
        cuis_input_unlock(flags);
        if (changed) damage_windows();
        break;
    case CUIS_WINDOW_REDRAW:
        /* Polling is nonblocking and does not grant permission to draw.
         * Later clipping/display coordination must guard actual rendering. */
        flags = cuis_input_lock();
        changed = window->redraw;
        window->redraw = FALSE;
        cuis_input_unlock(flags);
        return changed ? 1 : 0;
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

static LONG wait_events(CUIS_CLIENT *client, CUIS_WINDOW *window, ULONG events, UQUAD argument)
{
    LONG timeout;
    ULONG start, duration = 0;
    UQUAD flags;
    timeout = (LONG)argument;
    if (argument != (UQUAD)(QUAD)timeout || timeout < -1) return EINVAL;
    /* Include the current partial tick so a positive timeout cannot expire
     * early just because the next 200 Hz tick was already imminent. */
    if (timeout > 0) duration = (ULONG)timeout / 5 + ((ULONG)timeout % 5 != 0) + 1;
    flags = cuis_input_lock();
    start = cuis_input_ticks();
    for (;;) {
        if (window && (!window->visible || (events == CUIS_EVENT_KEY && focus_window != window))) {
            cuis_input_unlock(flags); return EACCDN;
        }
        if (((events & CUIS_EVENT_KEY) && focus_window == window && window->key_count) ||
            ((events & CUIS_EVENT_POINTER) && (window->pointer_count || window->pointer_overflow)) ||
            ((events & CUIS_EVENT_REDRAW) && window->redraw) ||
            ((events & CUIS_EVENT_MESSAGE) && client->message_count)) {
            cuis_input_unlock(flags); return 1;
        }
        if (timeout >= 0 && (ULONG)(cuis_input_ticks() - start) >= duration) {
            cuis_input_unlock(flags);
            return 0;
        }
        if (update_owner && update_owner->owner == run) {
            cuis_input_unlock(flags);
            return EACCDN;
        }
#if CONF_WITH_X86_64_MEMTEST
        if (timeout == -1) cuis_input_test_wait(events);
#endif
        cuis_input_idle();
    }
}

/* Flat calls; only the trusted producer can submit keyboard events. */
LONG cuis_input_service(ULONG function, UQUAD handle, UQUAD argument)
{
    CUIS_CLIENT *client;
    CUIS_WINDOW *window;
    UQUAD flags;
    ULONG key;

    if (function != CUIS_KEY_READ && function != CUIS_KEY_WAIT) return EINVFN;
    if (handle > 0x7fffffffULL) return EINVAL;
    client = *find_owner(run);
    if (!client) return EACCDN;
    for (window = client->windows; window && window->handle != handle;
         window = window->next) ;
    if (!window) return EACCDN;
    if (function == CUIS_KEY_READ) {
        flags = cuis_input_lock();
        if (focus_window != window) { cuis_input_unlock(flags); return EACCDN; }
        if (!window->key_count) { cuis_input_unlock(flags); return 0; }
        cuis_input_unlock(flags);
        /* Back lazy/COW output memory before the short queue transaction. */
        if (!kproc_validate_user_write(argument, sizeof(key))) return EIMBA;
        flags = cuis_input_lock();
        if (focus_window != window) { cuis_input_unlock(flags); return EACCDN; }
        if (!window->key_count) { cuis_input_unlock(flags); return 0; }
        key = window->keys[window->key_head];
        if (!kproc_copy_to_user(argument, &key, sizeof(key))) {
            cuis_input_unlock(flags);
            return EIMBA;
        }
        window->key_head = (window->key_head + 1) % CUIS_KEY_CAPACITY;
        window->key_count--;
        cuis_input_unlock(flags);
        return 1;
    }
    return wait_events(client, window, CUIS_EVENT_KEY, argument);
}

static CUIS_WINDOW *input_window(UQUAD handle)
{
    CUIS_CLIENT *client = *find_owner(run);
    CUIS_WINDOW *window;
    if (!client) return NULL;
    for (window = client->windows; window && window->handle != handle; window = window->next) ;
    return window;
}
LONG cuis_event_service(UQUAD handle, UQUAD events, UQUAD timeout)
{
    CUIS_WINDOW *window;
    if (handle > 0x7fffffffULL || !events ||
        (events & ~(UQUAD)(CUIS_EVENT_KEY | CUIS_EVENT_POINTER | CUIS_EVENT_REDRAW | CUIS_EVENT_MESSAGE))) return EINVAL;
    window = input_window(handle);
    if (!window) return EACCDN;
    refresh_pointer_bounds();
    return wait_events(*find_owner(run), window, (ULONG)events, timeout);
}
LONG cuis_pointer_service(UQUAD handle, UQUAD x, UQUAD y, UQUAD buttons, UQUAD changed)
{
    CUIS_WINDOW *window;
    CUIS_POINTER event;
    UQUAD flags;
    if (handle > 0x7fffffffULL) return EINVAL;
    window = input_window(handle);
    if (!window) return EACCDN;
    flags = cuis_input_lock();
    if (window->pointer_overflow) {
        window->pointer_overflow = FALSE;
        cuis_input_unlock(flags); return ERANGE;
    }
    if (!window->pointer_count) { cuis_input_unlock(flags); return 0; }
    cuis_input_unlock(flags);
    if (!kproc_validate_user_write(x, sizeof(LONG)) ||
        !kproc_validate_user_write(y, sizeof(LONG)) ||
        !kproc_validate_user_write(buttons, sizeof(ULONG)) ||
        !kproc_validate_user_write(changed, sizeof(ULONG))) return EIMBA;
    flags = cuis_input_lock();
    if (window->pointer_overflow) {
        window->pointer_overflow = FALSE;
        cuis_input_unlock(flags); return ERANGE;
    }
    if (!window->pointer_count) { cuis_input_unlock(flags); return 0; }
    event = window->pointer[window->pointer_head];
    if (!kproc_copy_to_user(x, &event.x, sizeof(LONG)) ||
        !kproc_copy_to_user(y, &event.y, sizeof(LONG)) ||
        !kproc_copy_to_user(buttons, &event.buttons, sizeof(ULONG)) ||
        !kproc_copy_to_user(changed, &event.state, sizeof(ULONG))) {
        cuis_input_unlock(flags); return EIMBA;
    }
    window->pointer_head = (window->pointer_head + 1) % CUIS_POINTER_CAPACITY;
    window->pointer_count--;
    cuis_input_unlock(flags);
    return 1;
}

/* Opaque peer data cannot manufacture a trusted input or redraw event. */
LONG cuis_message_service(ULONG function, UQUAD a, UQUAD b, UQUAD c,
                          UQUAD d, UQUAD e, UQUAD f)
{
    CUIS_CLIENT *client, *peer;
    CUIS_MESSAGE message;
    UWORD i;
    UQUAD flags;

    if (function < CUIS_MESSAGE_SEND || function > CUIS_MESSAGE_WAIT) return EINVFN;
    if (a > 0x7fffffffULL) return EINVAL;
    client = *find_owner(run);
    if (!client) return EACCDN;
    if (function == CUIS_MESSAGE_SEND) {
        if (b > 0xffffffffULL || c > 0xffffffffULL ||
            d > 0xffffffffULL || e > 0xffffffffULL) return EINVAL;
        for (peer = clients; peer && peer->handle != a; peer = peer->next) ;
        if (!peer) return EACCDN;
        for (i = 0; i < peer->grant_count && peer->grants[i] != client->handle; i++) ;
        if (peer != client && i == peer->grant_count) return EACCDN;
        flags = cuis_input_lock();
        if (peer->message_count == CUIS_MESSAGE_CAPACITY) {
            cuis_input_unlock(flags); return EAGAIN;
        }
        message.sender = client->handle;
        message.words[0] = (ULONG)b; message.words[1] = (ULONG)c;
        message.words[2] = (ULONG)d; message.words[3] = (ULONG)e;
        peer->messages[(peer->message_head + peer->message_count) % CUIS_MESSAGE_CAPACITY] = message;
        peer->message_count++;
        cuis_input_unlock(flags);
        return E_OK;
    }
    if (a != client->handle) return EACCDN;
    if (function == CUIS_MESSAGE_WAIT) return wait_events(client, NULL, CUIS_EVENT_MESSAGE, b);
    if (function == CUIS_MESSAGE_READ) {
        if (!client->message_count) return 0;
        /* Back all five outputs before the short IRQ-masked copy/pop. */
        if (!kproc_validate_user_write(b, sizeof(ULONG)) ||
            !kproc_validate_user_write(c, sizeof(ULONG)) ||
            !kproc_validate_user_write(d, sizeof(ULONG)) ||
            !kproc_validate_user_write(e, sizeof(ULONG)) ||
            !kproc_validate_user_write(f, sizeof(ULONG))) return EIMBA;
        flags = cuis_input_lock();
        if (!client->message_count) { cuis_input_unlock(flags); return 0; }
        message = client->messages[client->message_head];
        if (!kproc_copy_to_user(b, &message.sender, sizeof(ULONG)) ||
            !kproc_copy_to_user(c, &message.words[0], sizeof(ULONG)) ||
            !kproc_copy_to_user(d, &message.words[1], sizeof(ULONG)) ||
            !kproc_copy_to_user(e, &message.words[2], sizeof(ULONG)) ||
            !kproc_copy_to_user(f, &message.words[3], sizeof(ULONG))) {
            cuis_input_unlock(flags); return EIMBA;
        }
        client->message_head = (client->message_head + 1) % CUIS_MESSAGE_CAPACITY;
        client->message_count--;
        cuis_input_unlock(flags);
        return 1;
    }
    if (b > 0x7fffffffULL) return EINVAL;
    for (i = 0; i < client->grant_count && client->grants[i] != b; i++) ;
    if (function == CUIS_MESSAGE_DENY) {
        if (i < client->grant_count) client->grants[i] = client->grants[--client->grant_count];
        return E_OK;
    }
    for (peer = clients; peer && peer->handle != b; peer = peer->next) ;
    if (!peer) return EACCDN;
    if (peer == client || i < client->grant_count) return E_OK;
    if (client->grant_count == CUIS_MESSAGE_GRANTS) return ENSMEM;
    client->grants[client->grant_count++] = (ULONG)b;
    return E_OK;
}
