/* Process-owned native GUI sessions; never interpret GEM/AES wire formats. */
#include "emutos.h"
#include "cuis.h"
#include "cuis_region.h"
#include "cuis_input.h"
#include "ksched.h"
#include "screen_mode.h"
#include "gemerror.h"
#include "kheap.h"
#include "string.h"
#include "../bdos/kproc.h"

extern PD *run;

#define CUIS_MESSAGE_CAPACITY 16U
#define CUIS_KEY_CAPACITY 16U
#define CUIS_POINTER_CAPACITY 16U
#define CUIS_WINDOW_LIMIT 256UL
#define CUIS_STREAM_BYTES 65536UL
typedef struct {
    LONG sender_pid;
    UBYTE data[1];
} CUIS_PACKET;
typedef struct {
    LONG x, y;
    ULONG buttons, state;
} CUIS_POINTER;
typedef struct cuis_window {
    struct cuis_client *client;
    struct cuis_window *next;
    struct cuis_window *znext;
    ULONG handle;
    UWORD identifier; /* Globally unique among live windows; reusable after destruction. */
    LONG x, y, width, height;
    BOOL visible, redraw, managed_activation;
    ULONG notices;
    UWORD outset_right, outset_bottom;
    CUIS_RECT *display_regions, *pending_regions;
    ULONG display_region_count;
    BOOL display_regions_valid;
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

typedef struct {
    LONG x, y, width, height;
    BOOL leave;
} CUIS_INPUT_RECTANGLE;
typedef struct {
    CUIS_INPUT_RECTANGLE rect[2];
    ULONG mask, state, clicks;
    BOOL invert;
} CUIS_INPUT_FILTER;
typedef struct {
    ULONG events, ready, count, start, duration;
    LONG x, y;
    ULONG buttons, modifiers;
    BOOL matched, tracking;
} CUIS_INPUT_WAIT;
typedef struct cuis_client {
    struct cuis_client *next;
    PD *owner;
    ULONG handle;
    CUIS_WINDOW *windows;
    ULONG window_count;
    CUIS_MESSAGE messages[CUIS_MESSAGE_CAPACITY];
    UWORD message_head, message_count;
    UBYTE label[CUIS_LABEL_MAX];
    ULONG label_length;
    LONG pid;
    CUIS_PACKET *pending_write;
    UBYTE *stream_data; /* Byte ring followed by a WORD sender-ID ring. */
    ULONG stream_head, stream_bytes;
    CUIS_POINTER button_edges[CUIS_POINTER_CAPACITY];
    ULONG edge_head, edge_count, recent_count, recent_buttons;
    CUIS_POINTER saved_edges[CUIS_POINTER_CAPACITY];
    ULONG saved_head, saved_count, saved_recent_count, saved_recent_buttons;
    BOOL isolated_mouse;
    CUIS_INPUT_FILTER input_filter;
    CUIS_INPUT_WAIT *input_wait; /* Resident syscall stack, only while blocked. */
    ULONG keys[CUIS_KEY_CAPACITY];
    UWORD key_head, key_count;
    UWORD surface_height, surface_events;
    struct cuis_client *surface_next;
    LONG input_result[5]; /* key, count, x, y, packed buttons/modifiers */
} CUIS_CLIENT;
static CUIS_CLIENT *clients;
static ULONG input_interval = 450;
static void sample_input_waits(void);
static ULONG next_handle;
static ULONG next_window_handle;
/* Front to back, containing only visible windows from all sessions. */
static CUIS_WINDOW *window_front;
static CUIS_WINDOW *focus_window;
static ULONG window_total;
static CUIS_WINDOW *pointer_hover, *pointer_capture;
static LONG pointer_x, pointer_y, pointer_width, pointer_height;
static ULONG pointer_buttons;
static BOOL pointer_activation_drag;
static BOOL pointer_initialized, pointer_seen, pointer_suppressed, pointer_legacy_drag;
static CUIS_CLIENT *update_owner;
static CUIS_WINDOW *update_window;
static CUIS_RECT *update_clips;
static ULONG update_count;
static CUIS_CLIENT *mouse_owner;
static CUIS_CLIENT *keyboard_owner;
static ULONG keyboard_depth;
static void end_keyboard(void);
static ULONG mouse_depth;
static CUIS_CLIENT *display_owner;
static ULONG display_depth;
static CUIS_RECT display_clip;

/* A process-owned top strip is generic display coordination, not a GEM
 * tree/message protocol. Focus chooses among registered surfaces. */
static CUIS_CLIENT *surface_owner, *surface_capture, *surface_front;
static BOOL surface_focus_pending;
static UWORD surface_height, surface_paint_height;
static CUIS_CLIENT *surface_painter;
static void damage_windows(void);
static void surface_remove(CUIS_CLIENT *client)
{
    CUIS_CLIENT **link;
    for (link = &surface_front; *link && *link != client; link = &(*link)->surface_next) ;
    if (*link) *link = client->surface_next;
    client->surface_next = NULL;
}
static void surface_choose(CUIS_CLIENT *client)
{
    UWORD height = surface_height;
    BOOL changed = surface_owner != client;
    if (surface_owner) {
        surface_owner->surface_events = 0;
        if (changed && !keyboard_owner) surface_owner->key_head = surface_owner->key_count = 0;
    }
    surface_owner = client;
    surface_height = client ? client->surface_height : 0;
    if (client) {
        client->surface_events = CUIS_SURFACE_DIRTY;
        if (changed && !keyboard_owner) client->key_head = client->key_count = 0;
        if (pointer_initialized && pointer_y < client->surface_height && !pointer_buttons)
            client->surface_events |= CUIS_SURFACE_POINTER;
    }
    if (height != (client ? client->surface_height : 0)) damage_windows();
}
static void surface_fallback(CUIS_CLIENT *exclude);
static void surface_register(CUIS_CLIENT *client, UWORD height)
{
    surface_remove(client);
    client->surface_height = height;
    if (height) {
        client->surface_next = surface_front; surface_front = client;
        surface_focus_pending = FALSE; surface_choose(client);
    } else if (surface_owner == client) surface_fallback(client);
}
static void surface_focus(void)
{
    if (!surface_focus_pending || display_owner || update_owner || surface_capture) return;
    surface_focus_pending = FALSE;
    if (focus_window && focus_window->client->surface_height && surface_owner != focus_window->client)
        surface_choose(focus_window->client);
}
static void surface_fallback(CUIS_CLIENT *exclude)
{
    CUIS_CLIENT *client;
    if (focus_window && focus_window->client != exclude && focus_window->client->surface_height) {
        surface_choose(focus_window->client); return;
    }
    for (client = surface_front; client; client = client->surface_next)
        if (client != exclude) break;
    surface_choose(client);
}

static void end_update(void)
{
    if (update_clips) kfree(update_clips);
    update_clips = NULL;
    update_count = 0;
    update_owner = NULL;
    surface_focus();
    update_window = NULL;
}

static LONG visible_regions(CUIS_WINDOW *window, CUIS_RECT **rects, ULONG *total)
{
    CUIS_RECT *a, *b, *swap;
    CUIS_WINDOW *cover;
    ULONG count = 1;
    LONG result, right, bottom;
    SCREEN_MODE_DESC mode;

    *rects = NULL; *total = 0;
    if (!window->visible) return E_OK;
    screen_get_current_mode_desc(&mode);
    if (!mode.width || !mode.height) return EINVFN;
    a = kalloc(CUIS_CLIP_LIMIT * sizeof(*a));
    if (!a) return ENSMEM;
    b = kalloc(CUIS_CLIP_LIMIT * sizeof(*b));
    if (!b) { kfree(a); return ENSMEM; }
    a[0].x = window->x > 0 ? window->x : 0;
    a[0].y = window->y > 0 ? window->y : 0;
    if (surface_owner && a[0].y < surface_owner->surface_height) a[0].y = surface_owner->surface_height;
    right = window->x + window->width + window->outset_right;
    bottom = window->y + window->height + window->outset_bottom;
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
        occluder.width = cover->width + cover->outset_right;
        occluder.height = cover->height + cover->outset_bottom;
        result = cuis_region_subtract(a, count, &occluder, b, CUIS_CLIP_LIMIT);
        if (result < 0) { kfree(a); kfree(b); return result; }
        count = (ULONG)result;
        swap = a; a = b; b = swap;
    }
    kfree(b);
    if (!count) { kfree(a); a = NULL; }
    *rects = a;
    *total = count;
    return E_OK;
}

static LONG begin_update(CUIS_CLIENT *client, CUIS_WINDOW *window)
{
    LONG result;
    if (update_owner || display_owner) return EAGAIN;
    if (!window->visible) return EINVAL;
    result = visible_regions(window, &update_clips, &update_count);
    if (result < 0) return result;
    update_owner = client;
    update_window = window;
    return E_OK;
}

static void damage_windows(void)
{
    CUIS_WINDOW *window;
    UQUAD flags = cuis_input_lock();
    for (window = window_front; window; window = window->znext) {
        window->redraw = TRUE;
        if (window->managed_activation) window->notices |= CUIS_NOTICE_REDRAW;
    }
    cuis_input_unlock(flags);
}

/* IRQ keyboard delivery uses only the published focus pointer and its
 * resident bounded queue. Remove that pointer before a window can be freed. */
static BOOL set_focus(CUIS_WINDOW *window)
{
    UQUAD flags = cuis_input_lock();
    BOOL changed = focus_window != window;
    if (changed) {
        if (focus_window) {
            focus_window->key_head = focus_window->key_count = 0;
            if ((!window || window->client != focus_window->client) && focus_window->client != mouse_owner) {
                focus_window->client->edge_count = 0;
                focus_window->client->recent_count = 0;
            }
        }
        if (window) window->key_head = window->key_count = 0;
        focus_window = window;
        surface_focus_pending = TRUE;
        surface_focus();
    }
    cuis_input_unlock(flags);
    return changed;
}

BOOL cuis_keyboard_event(ULONG key)
{
    UQUAD flags = cuis_input_lock();
    CUIS_WINDOW *window = focus_window;
    CUIS_CLIENT *client = keyboard_owner ? keyboard_owner : window ? NULL : surface_owner;
    if (client) {
        if (client->key_count < CUIS_KEY_CAPACITY) {
            client->keys[(client->key_head + client->key_count) % CUIS_KEY_CAPACITY] = key;
            client->key_count++;
        }
    } else if (window && window->key_count < CUIS_KEY_CAPACITY) {
        window->keys[(window->key_head + window->key_count) % CUIS_KEY_CAPACITY] = key;
        window->key_count++;
    }
    cuis_input_unlock(flags);
    /* A full focused queue drops the newest key, never leaks it to BIOS. */
    return client != NULL || window != NULL;
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
/* Expiry must precede admission of a packet: a delayed waiter must not
 * count a new press as part of an already completed click window. */
static void expire_button_wait(CUIS_INPUT_WAIT *wait)
{
    if (wait && wait->tracking &&
        (ULONG)(cuis_input_ticks() - wait->start) >= wait->duration)
        wait->ready |= CUIS_READY_BUTTON;
}

/* Keep session history even for windowless modal dialogs. Only trusted
 * routing calls this producer; opaque application messages never enter it. */
static void button_enqueue(CUIS_CLIENT *client, ULONG changed)
{
    CUIS_POINTER *event;
    LONG x, y;
    ULONG buttons, modifiers;
    if (changed) { client->recent_count = 1; client->recent_buttons = pointer_buttons; }
    expire_button_wait(client->input_wait);
    if (changed && (!client->input_wait || !(client->input_wait->events & CUIS_READY_BUTTON) ||
                    (client->input_wait->ready & CUIS_READY_BUTTON))) {
        if (client->edge_count == CUIS_POINTER_CAPACITY) {
            client->edge_head = (client->edge_head + 1) % CUIS_POINTER_CAPACITY;
            client->edge_count--;
        }
        cuis_input_snapshot(&x, &y, &buttons, &modifiers);
        event = &client->button_edges[(client->edge_head + client->edge_count++) % CUIS_POINTER_CAPACITY];
        event->x = pointer_x; event->y = pointer_y;
        event->buttons = pointer_buttons; event->state = modifiers;
    }
}
static void pointer_enqueue(CUIS_WINDOW *window, ULONG changed, BOOL inside)
{
    CUIS_POINTER *event;
    if (!window) return;
    button_enqueue(window->client, changed);
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
    if (mouse_owner) {
        if (mouse_owner->isolated_mouse) {
            /* A private gesture must not append menu presses/releases to
             * ordinary window queues. Only its session predicates see them. */
            pointer_hover = NULL;
            button_enqueue(mouse_owner,changed);
            if (!pointer_buttons) pointer_capture = NULL;
            return TRUE;
        }
        /* A global mouse grab never raises/focuses a hit window. Native
         * window queues can follow an owned visible window, but the session
         * predicates/history also work when the dialog has no windows. */
        for (target = mouse_owner->windows; target && !target->visible; target = target->next) ;
        if (focus_window && focus_window->client == mouse_owner) target = focus_window;
        if (pointer_hover && pointer_hover != target) pointer_enqueue(pointer_hover, 0, FALSE);
        pointer_hover = target;
        if (target) pointer_enqueue(target, changed, target == hit);
        else button_enqueue(mouse_owner, changed);
        if (!pointer_buttons) pointer_capture = NULL;
        return TRUE;
    }
    if (surface_capture || (surface_owner && !pointer_capture && pointer_y < surface_owner->surface_height)) {
        CUIS_CLIENT *client = surface_capture ? surface_capture : surface_owner;
        if (pointer_hover) pointer_enqueue(pointer_hover,0,FALSE);
        pointer_hover = NULL;
        client->surface_events |= CUIS_SURFACE_POINTER;
        if (pointer_buttons) surface_capture = client;
        /* Surface gestures are consumed by its owner-side handler, not
         * ordinary application button history. A subsequent mouse grab
         * samples the current held state without replaying a bar press. */
        if (!pointer_buttons && surface_capture) {
            sample_input_waits(); surface_capture = NULL; surface_focus();
        }
        return TRUE;
    }
    if (!pointer_capture && (changed & pointer_buttons)) {
        if (hit) {
            pointer_capture = hit;
            if (hit->managed_activation && hit != window_front) {
                hit->notices |= CUIS_NOTICE_ACTIVATE;
                pointer_activation_drag = TRUE;
            } else if (set_focus(hit)) damage_windows();
        } else {
            if (pointer_hover) pointer_enqueue(pointer_hover, 0, FALSE);
            pointer_hover = NULL;
            if (set_focus(NULL)) damage_windows();
            pointer_legacy_drag = TRUE;
            return FALSE;
        }
    }
    if (pointer_activation_drag) {
        if (!pointer_buttons) { pointer_capture = NULL; pointer_activation_drag = FALSE; }
        return TRUE; /* A click-to-top is not also application button input. */
    }
    target = pointer_capture ? pointer_capture : hit;
    consumed = target != NULL;
    if (pointer_hover && pointer_hover != target) pointer_enqueue(pointer_hover, 0, pointer_hover == hit);
    pointer_hover = target;
    pointer_enqueue(target, changed, target == hit);
    if (pointer_capture && !pointer_buttons) {
        /* A focus change during the drag must not steal its release. The
         * activation-gesture branch above deliberately bypasses sampling. */
        sample_input_waits();
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
    sample_input_waits();
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
    window->notices = 0;
    window->pointer_head = window->pointer_count = 0;
    window->pointer_overflow = FALSE;
    if (window->client != mouse_owner) window->client->edge_count = window->client->recent_count = 0;
    if (pointer_capture == window) {
        pointer_capture = NULL;
        pointer_activation_drag = FALSE;
        pointer_suppressed = !mouse_owner && pointer_buttons != 0;
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

static void clear_display_regions(CUIS_CLIENT *client)
{
    CUIS_WINDOW *window;
    for (window = client->windows; window; window = window->next) {
        if (window->display_regions) kfree(window->display_regions);
        if (window->pending_regions) kfree(window->pending_regions);
        window->pending_regions = NULL;
        window->display_regions = NULL;
        window->display_regions_valid = FALSE;
    }
}

static BOOL display_available(void *argument)
{
    (void)argument;
    return !display_owner && !update_owner;
}

LONG cuis_surface_service(ULONG function, UQUAD handle, UQUAD argument)
{
    CUIS_CLIENT *client;
    SCREEN_MODE_DESC mode;
    UQUAD flags;
    LONG result;
    if (handle > 0x7fffffffULL) return EINVAL;
    flags = cuis_input_lock();
    client = *find_owner(run);
    if (!client || client->handle != handle) { cuis_input_unlock(flags); return EACCDN; }
    if (function == CUIS_CLOCK) {
        result = argument ? EINVAL : (LONG)(cuis_input_ticks() & 0x7fffffffUL);
    } else if (function == CUIS_SURFACE_STATUS) {
        if (argument > 2) result = EINVAL;
        else if (argument == 2) {
            if (surface_owner != client) result = EACCDN;
            else { client->surface_events |= CUIS_SURFACE_DIRTY; result = E_OK; }
        } else {
            result = surface_owner == client ? CUIS_SURFACE_OWNED | (mouse_owner ? 0 : client->surface_events) : 0;
            if (argument && !mouse_owner) client->surface_events = 0;
        }
    } else if (function == CUIS_SURFACE_BEGIN) {
        screen_get_current_mode_desc(&mode);
        if (!argument || argument > 32767 || argument >= mode.height || !mode.width) result = EINVAL;
        else if (display_owner != client || surface_painter) result = EACCDN;
        else if ((surface_capture && surface_capture != client) || (mouse_owner && mouse_owner != client)) result = EAGAIN;
        else { surface_painter = client; surface_paint_height = argument; result = E_OK; }
    } else if (function == CUIS_SURFACE_END) {
        if (argument > 1) result = EINVAL;
        else if (display_owner != client || surface_painter != client) result = EACCDN;
        else if (argument && surface_capture && surface_capture != client) result = EAGAIN;
        else {
            if (argument) surface_register(client,surface_paint_height);
            surface_painter = NULL; surface_paint_height = 0; result = E_OK;
        }
    } else if (function == CUIS_SURFACE_REGISTER) {
        screen_get_current_mode_desc(&mode);
        if (argument && (argument > 32767 || argument >= mode.height || !mode.width)) result = EINVAL;
        else if ((display_owner && display_owner != client) || (update_owner && update_owner != client) ||
                 (surface_capture && surface_capture != client)) result = EAGAIN;
        else {
            if (surface_painter) result = EACCDN;
            else { surface_register(client,argument); result = E_OK; }
        }
    } else result = EINVFN;
    cuis_input_unlock(flags);
    return result;
}

LONG cuis_display_lock(UQUAD handle, UQUAD acquire, UQUAD block)
{
    CUIS_CLIENT *client;
    SCREEN_MODE_DESC mode;
    UQUAD flags;
    if (handle > 0x7fffffffULL || acquire > 1 || block > 1 || (!acquire && block)) return EINVAL;
    flags = cuis_input_lock();
    for (;;) {
        client = *find_owner(run);
        if (!client || client->handle != handle) { cuis_input_unlock(flags); return EACCDN; }
        if (!acquire) {
            if (display_owner != client) { cuis_input_unlock(flags); return EACCDN; }
            if (!--display_depth) {
                if (surface_painter == client) { surface_painter = NULL; surface_paint_height = 0; }
                end_keyboard(); clear_display_regions(client); display_owner = NULL; surface_focus();
            }
            cuis_input_unlock(flags); return E_OK;
        }
        if (display_owner == client) {
            if (display_depth == 0xffffffffUL) { cuis_input_unlock(flags); return ERANGE; }
            display_depth++;
            cuis_input_unlock(flags); return E_OK;
        }
        if (!display_owner && !update_owner) {
            screen_get_current_mode_desc(&mode);
            if (!mode.width || !mode.height) { cuis_input_unlock(flags); return EINVFN; }
            display_clip.x = display_clip.y = 0;
            display_clip.width = mode.width; display_clip.height = mode.height;
            display_depth = 1; display_owner = client;
            cuis_input_unlock(flags); return E_OK;
        }
        /* A mouse owner must never sleep behind a foreign drawing owner:
         * that owner can be waiting for this gesture's mouse control.
         * EACCDN distinguishes contention from the EAGAIN borrow case below;
         * ordinary nonblocking callers retain their existing EAGAIN result. */
        if (block && mouse_owner == client && update_owner != client) {
            cuis_input_unlock(flags); return EACCDN;
        }
        /* Waiting for a per-window section owned by this caller deadlocks. */
        if (!block || update_owner == client) { cuis_input_unlock(flags); return EAGAIN; }
        /* This is an IRQ mask, not a held mutex. ksched_block requires
         * masked IRQs and restores the selected task's saved context; idle
         * uses STI/HLT/CLI. Unlocking here would race the availability check
         * against waiter publication (or HLT) and lose a release wakeup. */
        if (!ksched_block(display_available, NULL)) cuis_input_idle();
    }
}

/* Call with IRQs excluded; a foreign held drag cannot be taken over. */
static BOOL mouse_available(void *argument)
{
    CUIS_CLIENT *client = argument;
    return !mouse_owner && (!pointer_buttons ||
        surface_capture == client ||
        (pointer_capture && pointer_capture->client == client && !pointer_activation_drag));
}

static void end_keyboard(void)
{
    if (keyboard_owner) keyboard_owner->key_head = keyboard_owner->key_count = 0;
    keyboard_owner = NULL; keyboard_depth = 0;
}

LONG cuis_keyboard_lock(UQUAD handle, UQUAD acquire)
{
    CUIS_CLIENT *client;
    UQUAD flags;
    if (handle > 0x7fffffffULL || acquire > 1) return EINVAL;
    flags = cuis_input_lock();
    client = *find_owner(run);
    if (!client || client->handle != handle || display_owner != client || mouse_owner != client) {
        cuis_input_unlock(flags); return EACCDN;
    }
    if (acquire) {
        if (keyboard_depth == 0xffffffffUL) { cuis_input_unlock(flags); return ERANGE; }
        if (!keyboard_owner) {
            keyboard_owner = client;
            client->key_head = client->key_count = 0;
            if (focus_window && focus_window->client == client)
                focus_window->key_head = focus_window->key_count = 0;
        }
        keyboard_depth++;
    } else {
        if (keyboard_owner != client) { cuis_input_unlock(flags); return EACCDN; }
        if (!--keyboard_depth) end_keyboard();
    }
    cuis_input_unlock(flags); return E_OK;
}

LONG cuis_display_redraw(UQUAD handle, UQUAD x, UQUAD y, UQUAD width, UQUAD height)
{
    CUIS_CLIENT *client;
    CUIS_WINDOW *window;
    UQUAD flags;
    if (handle > 0x7fffffffULL || x != (UQUAD)(QUAD)(LONG)x || y != (UQUAD)(QUAD)(LONG)y ||
        width > 32767 || height > 32767) return EINVAL;
    flags = cuis_input_lock();
    client = *find_owner(run);
    if (!client || client->handle != handle || display_owner != client) {
        cuis_input_unlock(flags); return EACCDN;
    }
    if (width && height) for (window = window_front; window; window = window->znext) {
        if ((QUAD)window->x < (QUAD)(LONG)x + (LONG)width && (QUAD)window->x + window->width + window->outset_right > (LONG)x &&
            (QUAD)window->y < (QUAD)(LONG)y + (LONG)height && (QUAD)window->y + window->height + window->outset_bottom > (LONG)y) {
            window->redraw = TRUE;
            if (window->managed_activation) window->notices |= CUIS_NOTICE_REDRAW;
        }
    }
    cuis_input_unlock(flags); return E_OK;
}

static void end_mouse(void)
{
    CUIS_CLIENT *client = mouse_owner;
    end_keyboard();
    mouse_owner->edge_count = mouse_owner->recent_count = 0;
    mouse_owner = NULL; mouse_depth = 0;
    pointer_capture = pointer_hover = NULL; surface_capture = NULL;
    pointer_activation_drag = FALSE;
    /* Never deliver a release without the press that belonged to the grab. */
    pointer_suppressed = pointer_buttons != 0;
    route_pointer(0);
    if (client->isolated_mouse) {
        memcpy(client->button_edges,client->saved_edges,sizeof(client->button_edges));
        client->edge_head = client->saved_head; client->edge_count = client->saved_count;
        client->recent_count = client->saved_recent_count; client->recent_buttons = client->saved_recent_buttons;
        client->isolated_mouse = FALSE;
    }
    surface_focus();
    sample_input_waits();
}

LONG cuis_mouse_lock(UQUAD handle, UQUAD acquire, UQUAD block)
{
    CUIS_CLIENT *client;
    UQUAD flags;
    if (handle > 0x7fffffffULL || acquire > CUIS_MOUSE_FRESH || block > 1 || (!acquire && block)) return EINVAL;
    refresh_pointer_bounds();
    flags = cuis_input_lock();
    for (;;) {
        client = *find_owner(run);
        if (!client || client->handle != handle) { cuis_input_unlock(flags); return EACCDN; }
        if (!acquire) {
            if (mouse_owner != client) { cuis_input_unlock(flags); return EACCDN; }
            if (!--mouse_depth) end_mouse();
            cuis_input_unlock(flags); return E_OK;
        }
        if (mouse_owner == client) {
            if (acquire == CUIS_MOUSE_FRESH) { cuis_input_unlock(flags); return EACCDN; }
            if (mouse_depth == 0xffffffffUL) { cuis_input_unlock(flags); return ERANGE; }
            mouse_depth++;
            cuis_input_unlock(flags); return E_OK;
        }
        if (mouse_available(client)) {
            if (acquire == CUIS_MOUSE_FRESH) {
                memcpy(client->saved_edges,client->button_edges,sizeof(client->button_edges));
                client->saved_head = client->edge_head; client->saved_count = client->edge_count;
                client->saved_recent_count = client->recent_count; client->saved_recent_buttons = client->recent_buttons;
                client->edge_head = client->edge_count = client->recent_count = 0;
                client->recent_buttons = 0; client->isolated_mouse = TRUE;
            }
            mouse_owner = client; mouse_depth = 1;
            pointer_capture = NULL;
            route_pointer(0);
            sample_input_waits();
            cuis_input_unlock(flags); return E_OK;
        }
        if (!block || update_owner == client) { cuis_input_unlock(flags); return EAGAIN; }
        /* Keep the availability test and waiter publication under the same
         * IRQ mask, just like DISPLAY_LOCK; no lock spans task execution. */
        if (!ksched_block(mouse_available, client)) cuis_input_idle();
    }
}

void cuis_process_exit(PD *owner)
{
    CUIS_CLIENT **link = find_owner(owner), *client = *link;
    if (client) {
        CUIS_WINDOW *window;
        UQUAD flags = cuis_input_lock();
        client->input_wait = NULL;
        if (surface_painter == client) { surface_painter = NULL; surface_paint_height = 0; }
        client->surface_height = 0; surface_remove(client);
        if (surface_capture == client) { surface_capture = NULL; pointer_suppressed = pointer_buttons != 0; }
        if (surface_owner == client) surface_fallback(client);
        if (mouse_owner == client) end_mouse();
        cuis_input_unlock(flags);
        if (update_owner == client) end_update();
        if (display_owner == client) { clear_display_regions(client); display_owner = NULL; display_depth = 0; }
        while ((window = client->windows) != NULL) {
            client->windows = window->next;
            hide_window(window);
            window_total--;
            kfree(window);
        }
        flags = cuis_input_lock();
        surface_focus();
        cuis_input_unlock(flags);
        *link = client->next;
        if (client->stream_data) kfree(client->stream_data);
        if (client->pending_write) kfree(client->pending_write);
        kfree(client);
    }
}

#if CONF_WITH_X86_64_MEMTEST
BOOL cuis_update_active(void) { return update_owner != NULL || display_owner != NULL; }
BOOL cuis_mouse_active(void) { return mouse_owner != NULL; }
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
    UQUAD flags;

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
        client->message_head = client->message_count = 0;
        client->label_length = 0;
        client->pid = kproc_pid(run);
        client->pending_write = NULL; client->stream_data = NULL;
        client->stream_head = client->stream_bytes = 0;
        client->edge_head = client->edge_count = client->recent_count = client->recent_buttons = 0;
        client->input_wait = NULL;
        client->isolated_mouse = FALSE;
        client->surface_height = client->surface_events = 0; client->surface_next = NULL;
        client->key_head = client->key_count = 0;
        memset(&client->input_filter, 0, sizeof(client->input_filter));
        memset(client->input_result, 0, sizeof(client->input_result));
        client->owner = run;
        client->handle = next_handle++;
        flags = cuis_input_lock();
        client->next = clients;
        clients = client;
        cuis_input_unlock(flags);
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

static CUIS_CLIENT *find_handle(UQUAD handle)
{
    CUIS_CLIENT *client;
    for (client = clients; client && client->handle != handle; client = client->next) ;
    return client;
}

/* Labels are opaque bytes. Filename/GEM naming policy belongs to libaes. */
LONG cuis_directory_service(ULONG function, UQUAD a, UQUAD b, UQUAD c)
{
    CUIS_CLIENT *client = *find_owner(run), *peer, *found = NULL;
    UBYTE label[CUIS_LABEL_MAX];
    ULONG n;
    const char *name;
    if (!client) return EACCDN;
    if (function == CUIS_LABEL_FIND) {
        if (!b || b > CUIS_LABEL_MAX) return EINVAL;
        if (!kproc_copy_from_user(label, a, (ULONG)b)) return EIMBA;
        for (peer = clients; peer; peer = peer->next)
            if (peer->label_length == b && !memcmp(peer->label, label, (ULONG)b) &&
                (!found || peer->handle < found->handle)) found = peer;
        return found ? (LONG)found->handle : EFILNF;
    }
    if (function == CUIS_PEER_BY_PID) {
        if (!a || a > 32767) return EINVAL;
        for (peer = clients; peer && peer->pid != (LONG)a; peer = peer->next) ;
        return peer ? (LONG)peer->handle : EFILNF;
    }
    if (a > 0x7fffffffULL) return EINVAL;
    if (function == CUIS_PEER_PID) {
        peer = find_handle(a);
        return peer ? peer->pid : EFILNF;
    }
    if (a != client->handle) return EACCDN;
    if (function == CUIS_SESSION_NAME) {
        name = kproc_executable_name(run);
        for (n = 0; name[n]; n++) ;
        if (c > 0xffffffffULL || c < n + 1) return ERANGE;
        return kproc_copy_to_user(b, name, n + 1) ? (LONG)n : EIMBA;
    }
    if (function == CUIS_SESSION_LABEL) {
        if (c > CUIS_LABEL_MAX) return EINVAL;
        if (c && !kproc_copy_from_user(label, b, (ULONG)c)) return EIMBA;
        if (c) memcpy(client->label, label, (ULONG)c);
        client->label_length = (ULONG)c;
        return E_OK;
    }
    return EINVFN;
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

static UWORD window_identifier(void)
{
    UBYTE used[CUIS_WINDOW_LIMIT + 1] = { 0 };
    CUIS_CLIENT *client;
    CUIS_WINDOW *window;
    UWORD id;
    for (client = clients; client; client = client->next)
        for (window = client->windows; window; window = window->next) {
            if (!window->identifier || window->identifier > CUIS_WINDOW_LIMIT) return 0;
            used[window->identifier] = 1;
        }
    for (id = 1; id <= CUIS_WINDOW_LIMIT; id++)
        if (!used[id]) return id;
    return 0; /* The global quota is checked before allocating a window. */
}

LONG cuis_window_service(ULONG function, UQUAD a, UQUAD b, UQUAD c,
                         UQUAD d, UQUAD e)
{
    CUIS_CLIENT *client;
    CUIS_WINDOW **link, **back, *window;
    UQUAD flags;
    BOOL changed;

    if ((function < CUIS_WINDOW_CREATE || function > CUIS_FOCUS_RELEASE ||
         function == CUIS_WINDOW_CLIP) && function != CUIS_WINDOW_IDENTIFY &&
        function != CUIS_WINDOW_OUTSET && function != CUIS_WINDOW_POLICY)
        return EINVFN;
    client = *find_owner(run);
    if (!client) return EACCDN;
    if (function == CUIS_WINDOW_CREATE) {
        if (update_owner || (display_owner && display_owner != client)) return EAGAIN;
        if (!valid_geometry(a, b, c, d)) return EINVAL;
        if (window_total >= CUIS_WINDOW_LIMIT || client->window_count >= CUIS_WINDOWS_PER_SESSION ||
            next_window_handle > 0x7fffffffUL) return ENSMEM;
        window = kalloc(sizeof(*window));
        if (!window) return ENSMEM;
        window->identifier = window_identifier();
        if (!window->identifier) { kfree(window); return ENSMEM; }
        window->client = client;
        window->handle = next_window_handle++;
        window->znext = NULL;
        window->display_regions = window->pending_regions = NULL; window->display_regions_valid = FALSE;
        window->display_region_count = 0;
        window->outset_right = window->outset_bottom = 0;
        window->visible = FALSE;
        window->managed_activation = FALSE; window->notices = 0;
        window->redraw = FALSE;
        window->key_head = window->key_count = 0;
        window->pointer_head = window->pointer_count = 0;
        window->pointer_overflow = FALSE;
        window->x = (LONG)a; window->y = (LONG)b;
        window->width = (LONG)c; window->height = (LONG)d;
        /* Mouse-grab IRQ routing traverses this list, including hidden
         * entries. Publish only a fully initialized node under its IRQ mask. */
        flags = cuis_input_lock();
        window->next = client->windows;
        client->windows = window;
        client->window_count++;
        window_total++;
        cuis_input_unlock(flags);
        return (LONG)window->handle;
    }
    if (a > 0x7fffffffULL) return EINVAL;
    for (link = &client->windows; *link && (*link)->handle != a;
         link = &(*link)->next) ;
    window = *link;
    if (!window) return EACCDN;
    if (function == CUIS_WINDOW_IDENTIFY) return window->identifier;
    if (function == CUIS_UPDATE_BEGIN) return begin_update(client, window);
    if (function == CUIS_UPDATE_END) {
        if (update_owner != client || update_window != window) return EACCDN;
        end_update();
        return E_OK;
    }
    if (function == CUIS_FOCUS_QUERY) return focus_window == window ? 1 : 0;
    if ((update_owner || (display_owner && display_owner != client)) &&
        function != CUIS_WINDOW_REDRAW) return EAGAIN;
    switch (function) {
    case CUIS_WINDOW_DESTROY:
        if (window->pending_regions) kfree(window->pending_regions);
        if (window->display_regions) kfree(window->display_regions);
        hide_window(window);
        *link = window->next;
        client->window_count--;
        window_total--;
        kfree(window);
        break;
    case CUIS_WINDOW_CONFIGURE:
        if (!valid_geometry(b, c, d, e) ||
            d + window->outset_right > 0x7fffffffULL || e + window->outset_bottom > 0x7fffffffULL ||
            (QUAD)b + (QUAD)d + window->outset_right > 0x7fffffffLL ||
            (QUAD)c + (QUAD)e + window->outset_bottom > 0x7fffffffLL) return EINVAL;
        if (window->x == (LONG)b && window->y == (LONG)c &&
            window->width == (LONG)d && window->height == (LONG)e) break;
        flags = cuis_input_lock();
        window->x = (LONG)b; window->y = (LONG)c;
        window->width = (LONG)d; window->height = (LONG)e;
        route_pointer(0);
        cuis_input_unlock(flags);
        if (window->visible) damage_windows();
        break;
    case CUIS_WINDOW_POLICY:
        if (b > 1) return EINVAL;
        flags = cuis_input_lock();
        window->managed_activation = (BOOL)b;
        window->notices = b && window->visible ? CUIS_NOTICE_REDRAW : 0;
        cuis_input_unlock(flags);
        break;
    case CUIS_WINDOW_OUTSET:
        if (b > 0xffffULL || c > 0xffffULL ||
            (QUAD)window->width + (QUAD)b > 0x7fffffffLL || (QUAD)window->height + (QUAD)c > 0x7fffffffLL ||
            (QUAD)window->x + window->width + (QUAD)b > 0x7fffffffLL ||
            (QUAD)window->y + window->height + (QUAD)c > 0x7fffffffLL) return EINVAL;
        window->outset_right = (UWORD)b; window->outset_bottom = (UWORD)c;
        if (window->visible) damage_windows();
        break;
    case CUIS_WINDOW_SHOW:
        if (window->visible) break;
        flags = cuis_input_lock();
        window->visible = TRUE;
        window->znext = window_front;
        window_front = window;
        if (window->managed_activation) set_focus(window);
        route_pointer(0);
        cuis_input_unlock(flags);
        damage_windows();
        break;
    case CUIS_WINDOW_HIDE:
        hide_window(window);
        break;
    case CUIS_WINDOW_RAISE:
        if (!window->visible) return EINVAL;
        flags = cuis_input_lock();
        window->notices &= ~CUIS_NOTICE_ACTIVATE;
        if (window == window_front) {
            changed = window->managed_activation && set_focus(window);
            cuis_input_unlock(flags);
            if (changed) damage_windows();
            break;
        }
        unlink_visible(window);
        window->znext = window_front;
        window_front = window;
        if (window->managed_activation) set_focus(window);
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
        if (window->managed_activation && focus_window == window) set_focus(window_front);
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

/* User-output access can tear down the process/window. Resolve identities
 * again before committing or discarding the fault-cleanup-owned candidate. */
static LONG finish_region_refresh(PD *owner, ULONG session, ULONG handle,
                                   CUIS_RECT *candidate, ULONG count, BOOL commit)
{
    CUIS_CLIENT *client = *find_owner(owner);
    CUIS_WINDOW *window;
    if (!client || client->handle != session) return EACCDN;
    for (window = client->windows; window && window->handle != handle; window = window->next) ;
    if (!window || window->pending_regions != candidate) return EACCDN;
    if (commit && display_owner == client) {
        if (window->display_regions) kfree(window->display_regions);
        window->display_regions = window->pending_regions;
        window->display_region_count = count;
        window->display_regions_valid = TRUE;
        window->pending_regions = NULL;
        return E_OK;
    }
    if (window->pending_regions) kfree(window->pending_regions);
    window->pending_regions = NULL;
    return commit ? EACCDN : E_OK;
}

LONG cuis_region_service(UQUAD handle, UQUAD index, UQUAD x, UQUAD y,
                         UQUAD width, UQUAD height)
{
    CUIS_CLIENT *client;
    CUIS_WINDOW *window;
    CUIS_RECT *rects = NULL, rect;
    ULONG count = 0, session;
    LONG result;
    PD *owner = run;
    BOOL refresh = FALSE;
    if (handle > 0x7fffffffULL || index > 0xffffffffULL) return EINVAL;
    client = *find_owner(run);
    if (!client) return EACCDN;
    for (window = client->windows; window && window->handle != handle; window = window->next) ;
    if (!window) return EACCDN;
    session = client->handle;
    if (window->pending_regions) return EAGAIN;
    if (display_owner == client) {
        /* FIRST refreshes, NEXT retains its snapshot even if an occluding
         * peer exits. Per-window caches also allow interleaved GEM loops. */
        if (!index || !window->display_regions_valid) {
            result = visible_regions(window, &rects, &count);
            if (result < 0) return result;
            window->pending_regions = rects;
            refresh = TRUE;
            if (index >= count) {
                result = finish_region_refresh(owner, session, (ULONG)handle, rects, count, TRUE);
                return result < 0 ? result : 0;
            }
            rect = rects[(ULONG)index];
        } else {
            if (index >= window->display_region_count) return 0;
            rect = window->display_regions[(ULONG)index];
        }
    } else {
        result = visible_regions(window, &rects, &count);
        if (result < 0) return result;
        if (index >= count) { if (rects) kfree(rects); return 0; }
        rect = rects[(ULONG)index];
        kfree(rects);
    }
    if (!kproc_validate_user_write(x, sizeof(LONG)) || !kproc_validate_user_write(y, sizeof(LONG)) ||
        !kproc_validate_user_write(width, sizeof(LONG)) || !kproc_validate_user_write(height, sizeof(LONG)) ||
        !kproc_copy_to_user(x, &rect.x, sizeof(LONG)) || !kproc_copy_to_user(y, &rect.y, sizeof(LONG)) ||
        !kproc_copy_to_user(width, &rect.width, sizeof(LONG)) || !kproc_copy_to_user(height, &rect.height, sizeof(LONG))) {
        if (refresh) (void)finish_region_refresh(owner, session, (ULONG)handle, rects, count, FALSE);
        return EIMBA;
    }
    if (refresh) {
        result = finish_region_refresh(owner, session, (ULONG)handle, rects, count, TRUE);
        if (result < 0) return result;
    }
    return 1;
}

/* 0 retains legacy native VDI behavior; 1 requires the immutable owned
 * snapshot. A different update owner blocks native screen writes. */
LONG cuis_vdi_policy(void)
{
    if (display_owner) return display_owner->owner == run ? 1 : EAGAIN;
    if (update_owner) return update_owner->owner == run ? 1 : EAGAIN;
    return *find_owner(run) ? EACCDN : 0;
}
BOOL cuis_vdi_clip(ULONG index, CUIS_RECT *rect)
{
    if (display_owner) {
        if (display_owner->owner != run || index) return FALSE;
        *rect = display_clip;
        if (surface_owner && surface_owner != display_owner && surface_painter != display_owner) {
            LONG cut = surface_owner->surface_height;
            if (cut > rect->height) cut = rect->height;
            rect->height -= cut; rect->y += cut;
        }
        return TRUE;
    }
    if (!update_owner || update_owner->owner != run || index >= update_count)
        return FALSE;
    *rect = update_clips[index];
    if (surface_owner && rect->y < surface_owner->surface_height) {
        LONG cut = surface_owner->surface_height-rect->y;
        if (cut > rect->height) cut = rect->height;
        rect->y += cut; rect->height -= cut;
    }
    return TRUE;
}

typedef struct {
    CUIS_CLIENT *client;
    CUIS_WINDOW *window;
    ULONG events, start, duration;
    LONG timeout;
} CUIS_WAIT;

/* A scheduler predicate sees kernel-owned state only, with IRQs masked. */
static BOOL wait_ready(void *argument)
{
    CUIS_WAIT *wait = argument;
    CUIS_WINDOW *window = wait->window;
    ULONG events = wait->events;
    if (window && (!window->visible || (events == CUIS_EVENT_KEY && focus_window != window))) return TRUE;
    if (((events & CUIS_EVENT_KEY) && focus_window == window && window->key_count) ||
        ((events & CUIS_EVENT_POINTER) && (window->pointer_count || window->pointer_overflow)) ||
        ((events & CUIS_EVENT_REDRAW) && window->redraw) ||
        ((events & CUIS_EVENT_MESSAGE) && wait->client->message_count)) return TRUE;
    return wait->timeout >= 0 && (ULONG)(cuis_input_ticks() - wait->start) >= wait->duration;
}

static LONG wait_events(CUIS_CLIENT *client, CUIS_WINDOW *window, ULONG events, UQUAD argument)
{
    LONG timeout;
    ULONG start, duration = 0;
    UQUAD flags;
    CUIS_WAIT wait;
    timeout = (LONG)argument;
    if (argument != (UQUAD)(QUAD)timeout || timeout < -1) return EINVAL;
    /* Include the current partial tick so a positive timeout cannot expire
     * early just because the next 200 Hz tick was already imminent. */
    if (timeout > 0) duration = (ULONG)timeout / 5 + ((ULONG)timeout % 5 != 0) + 1;
    flags = cuis_input_lock();
    start = cuis_input_ticks();
    wait.client = client; wait.window = window; wait.events = events;
    wait.start = start; wait.duration = duration; wait.timeout = timeout;
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
        if (!ksched_block(wait_ready, &wait)) cuis_input_idle();
    }
}

/* A timer has no input/message predicate, and never consumes queued events.
 * All unsigned 32-bit millisecond intervals fit the wrap-safe tick span. */
LONG cuis_delay_service(UQUAD handle, UQUAD milliseconds)
{
    CUIS_CLIENT *client = *find_owner(run);
    CUIS_WAIT wait;
    UQUAD flags;
    if (handle > 0x7fffffffULL || milliseconds > 0xffffffffULL) return EINVAL;
    if (!client || client->handle != handle) return EACCDN;
    if (!milliseconds) return ksched_yield();
    if (update_owner && update_owner->owner == run) return EACCDN;
    flags = cuis_input_lock();
    wait.client = client; wait.window = NULL; wait.events = 0;
    wait.start = cuis_input_ticks(); wait.timeout = 0;
    wait.duration = (ULONG)(milliseconds / 5 + (milliseconds % 5 != 0) + 1);
    while (!wait_ready(&wait))
        if (!ksched_block(wait_ready, &wait)) cuis_input_idle();
    cuis_input_unlock(flags);
    return E_OK;
}

typedef struct {
    ULONG handle, length;
    BOOL write;
} CUIS_STREAM_WAIT;

typedef struct {
    CUIS_CLIENT *client;
    ULONG events, minimum, start, duration;
    CUIS_INPUT_WAIT input;
} CUIS_SESSION_WAIT_STATE;

static BOOL client_window(CUIS_CLIENT *client, CUIS_WINDOW *window)
{
    CUIS_WINDOW *scan;
    for (scan = client->windows; scan; scan = scan->next)
        if (scan == window) return TRUE;
    return FALSE;
}

/* Called with IRQs excluded, both at registration and from the trusted mouse
 * producer. Latch crossings/presses before a later IRQ can reverse them. */
static void sample_input(CUIS_CLIENT *client, CUIS_INPUT_WAIT *wait, BOOL initial)
{
    CUIS_INPUT_FILTER *filter = &client->input_filter;
    CUIS_INPUT_RECTANGLE *rect;
    CUIS_POINTER *edge;
    ULONG i, bit;
    BOOL owned, match, inside;
    LONG x, y;
    ULONG buttons, modifiers;
    cuis_input_snapshot(&x, &y, &buttons, &modifiers);
    if (pointer_initialized) { x = pointer_x; y = pointer_y; buttons = pointer_buttons; }
    owned = !pointer_suppressed && !pointer_legacy_drag && !pointer_activation_drag &&
        (mouse_owner ? mouse_owner == client :
         surface_capture ? FALSE :
         pointer_capture ? client_window(client, pointer_capture) :
         surface_owner && y < surface_owner->surface_height ? FALSE : client_window(client, focus_window));
    if (owned) {
        for (i = 0; i < 2; i++) {
            bit = i ? CUIS_READY_RECT2 : CUIS_READY_RECT1;
            rect = &filter->rect[i];
            inside = rect->width > 0 && rect->height > 0 && x >= rect->x && y >= rect->y &&
                (QUAD)x < (QUAD)rect->x + rect->width && (QUAD)y < (QUAD)rect->y + rect->height;
            if ((wait->events & bit) && (inside != rect->leave)) wait->ready |= bit;
        }
    }
    if (initial && (wait->events & CUIS_READY_BUTTON)) {
        /* GEM's quick check retains a press/release pair that arrived before
         * the wait was registered. A bounded private history is independent
         * of the transitional per-window native pointer queues. Stop after
         * the first matching edge so subsequent waits retain FIFO history.
         * These edges were already routed to this owner by trusted input;
         * current focus only governs live predicates, not queued history. */
        while (client->edge_count && !(wait->ready & CUIS_READY_BUTTON)) {
            edge = &client->button_edges[client->edge_head];
            if (((edge->buttons & filter->mask) == (filter->state & filter->mask)) != filter->invert) {
                wait->ready |= CUIS_READY_BUTTON;
                wait->count = filter->clicks ? 1 : 0;
                wait->x = edge->x; wait->y = edge->y;
                wait->buttons = edge->buttons; wait->modifiers = edge->state;
            }
            client->edge_head = (client->edge_head + 1) % CUIS_POINTER_CAPACITY;
            client->edge_count--;
        }
    }
    expire_button_wait(wait);
    if ((wait->events & CUIS_READY_BUTTON) && !(wait->ready & CUIS_READY_BUTTON)) {
        match = owned && (((buttons & filter->mask) == (filter->state & filter->mask)) != filter->invert);
        if (match && !wait->matched) {
            if (!wait->tracking) {
                wait->count = initial && client->recent_buttons == buttons && client->recent_count ? client->recent_count : 1;
                if (wait->count > filter->clicks) wait->count = filter->clicks;
                wait->start = cuis_input_ticks();
                wait->duration = input_interval / 5 + (input_interval % 5 != 0) + 1;
                wait->tracking = !initial && filter->clicks > 1;
            } else {
                /* GEM extends the original click window by three ticks. */
                if (wait->count < filter->clicks) {
                    wait->count++;
                    wait->duration += 3;
                }
            }
            wait->x = x; wait->y = y; wait->buttons = buttons; wait->modifiers = modifiers;
            if (!wait->tracking) wait->ready |= CUIS_READY_BUTTON;
        }
        wait->matched = match;
    }
    if (!(wait->ready & CUIS_READY_BUTTON) && !wait->tracking) {
        wait->x = x; wait->y = y; wait->buttons = buttons; wait->modifiers = modifiers;
    }
}

static void sample_input_waits(void)
{
    CUIS_CLIENT *client;
    for (client = clients; client; client = client->next)
        if (client->input_wait) sample_input(client, client->input_wait, FALSE);
}

static BOOL notices_ready(CUIS_CLIENT *client)
{
    CUIS_WINDOW *window;
    for (window = client->windows; window; window = window->next)
        if (window->notices) return TRUE;
    return FALSE;
}

static LONG session_ready(CUIS_SESSION_WAIT_STATE *wait)
{
    LONG result = 0;
    CUIS_WINDOW *window = focus_window;
    if (wait->events & CUIS_READY_INPUT) {
        sample_input(wait->client, &wait->input, FALSE);
        result |= wait->input.ready;
        if ((wait->events & CUIS_READY_KEY) &&
            (keyboard_owner ? (keyboard_owner == wait->client && wait->client->key_count) :
             (window ? client_window(wait->client, window) && window->key_count :
              surface_owner == wait->client && wait->client->key_count)))
            result |= CUIS_READY_KEY;
    }
    if ((wait->events & CUIS_READY_STREAM) && wait->client->stream_bytes >= wait->minimum)
        result |= CUIS_READY_STREAM;
    if ((wait->events & CUIS_READY_NOTICE) && notices_ready(wait->client)) result |= CUIS_READY_NOTICE;
    if ((wait->events & CUIS_READY_TIMER) && (ULONG)(cuis_input_ticks() - wait->start) >= wait->duration)
        result |= CUIS_READY_TIMER;
    /* Internal work must not end an unfinished multi-click wait. Keep its
     * count, snapshot and original deadline resident until a user event is
     * ready; report the pending surface alongside that event. */
    if ((wait->events & CUIS_READY_SURFACE) && !mouse_owner && surface_owner == wait->client &&
        wait->client->surface_events && (!wait->input.tracking || result))
        result |= CUIS_READY_SURFACE;
    return result;
}

static BOOL session_wait_ready(void *argument) { return session_ready(argument) != 0; }

/* Stream/notice checks do not consume data. Input completion captures a
 * key and predicate snapshot; one timer start survives all wakeups. No GEM
 * event masks or header formats cross this native scalar interface. */
LONG cuis_session_wait(UQUAD handle, UQUAD minimum, UQUAD events, UQUAD milliseconds)
{
    CUIS_CLIENT *client = *find_owner(run);
    CUIS_SESSION_WAIT_STATE wait;
    UQUAD flags;
    LONG ready;
    if (handle > 0x7fffffffULL || !events || events > CUIS_READY_ALL ||
        ((events & CUIS_READY_STREAM) && (!minimum || minimum > CUIS_STREAM_MAX)) ||
        ((events & CUIS_READY_TIMER) && milliseconds > 0xffffffffULL)) return EINVAL;
    if (!client || handle != client->handle) return EACCDN;
    if (events & CUIS_READY_INPUT) refresh_pointer_bounds();
    flags = cuis_input_lock();
    memset(&wait.input, 0, sizeof(wait.input));
    wait.input.events = (ULONG)events;
    if (events & CUIS_READY_INPUT) {
        sample_input(client, &wait.input, TRUE);
        client->input_wait = &wait.input;
    }
    wait.client = client; wait.events = (ULONG)events; wait.minimum = (ULONG)minimum;
    wait.start = cuis_input_ticks();
    wait.duration = (events & CUIS_READY_TIMER) && milliseconds ?
        (ULONG)(milliseconds / 5 + (milliseconds % 5 != 0) + 1) : 0;
    if ((events & CUIS_READY_TIMER) && !milliseconds && !(session_ready(&wait) & ~CUIS_READY_TIMER)) {
        /* A zero timer poll also gives cooperative peers one opportunity,
         * unless a requested source was already ready at the initial check. */
        cuis_input_unlock(flags);
        ksched_yield();
        flags = cuis_input_lock();
    }
    for (;;) {
        ready = session_ready(&wait);
        if (ready) {
            if (events & CUIS_READY_INPUT) {
                if (!(ready & CUIS_READY_BUTTON)) {
                    wait.input.tracking = FALSE;
                    sample_input(client, &wait.input, FALSE);
                }
                client->input_result[0] = 0;
                if (ready & CUIS_READY_KEY) {
                    if (keyboard_owner == client || (!focus_window && surface_owner == client)) {
                        client->input_result[0] = client->keys[client->key_head];
                        client->key_head = (client->key_head + 1) % CUIS_KEY_CAPACITY;
                        client->key_count--;
                    } else {
                        client->input_result[0] = focus_window->keys[focus_window->key_head];
                        focus_window->key_head = (focus_window->key_head + 1) % CUIS_KEY_CAPACITY;
                        focus_window->key_count--;
                    }
                }
                /* A per-call cap must not reduce a completed click count.
                 * Queued edges belong to newer input, not this snapshot. */
                if ((ready & CUIS_READY_BUTTON) && wait.input.buttons == pointer_buttons &&
                    !client->edge_count && wait.input.count > client->recent_count) {
                    client->recent_buttons = pointer_buttons; client->recent_count = wait.input.count;
                }
                client->input_result[1] = ready & CUIS_READY_BUTTON ? wait.input.count : 0;
                client->input_result[2] = wait.input.x; client->input_result[3] = wait.input.y;
                client->input_result[4] = (wait.input.buttons & 0xffff) | ((wait.input.modifiers & 0xffff) << 16);
            }
            client->input_wait = NULL;
            cuis_input_unlock(flags); return ready;
        }
        if (update_owner == client) {
            client->input_wait = NULL;
            cuis_input_unlock(flags); return EACCDN;
        }
        if (!ksched_block(session_wait_ready, &wait)) cuis_input_idle();
    }
}

/* Rectangle slot in bit zero, leave predicate in bit one. All geometry
 * arguments are signed LONG scalars; no userspace request structures. */
LONG cuis_input_config(ULONG function, UQUAD session, UQUAD a, UQUAD b, UQUAD c, UQUAD d, UQUAD e)
{
    CUIS_CLIENT *client;
    CUIS_INPUT_RECTANGLE rect;
    UQUAD flags, outputs[5];
    LONG values[5];
    ULONG i;
    if (session > 0x7fffffffULL) return EINVAL;
    if (function == CUIS_INPUT_RECT) {
        if (a > 3 || b != (UQUAD)(QUAD)(LONG)b || c != (UQUAD)(QUAD)(LONG)c ||
            d != (UQUAD)(QUAD)(LONG)d || e != (UQUAD)(QUAD)(LONG)e) return EINVAL;
        rect.x = (LONG)b; rect.y = (LONG)c; rect.width = (LONG)d; rect.height = (LONG)e;
        rect.leave = (a & 2) != 0;
    } else if (function == CUIS_INPUT_BUTTON) {
        if (a > 0xffff || b > 0xffff || c > 255 || d > 1 || e) return EINVAL;
    } else if (function == CUIS_INPUT_INTERVAL) {
        if (b > 1 || (b && (!a || a > 5000)) || c || d || e) return EINVAL;
    } else if (function != CUIS_INPUT_RESULT) return EINVFN;
    flags = cuis_input_lock();
    client = *find_owner(run);
    if (!client || client->handle != session) { cuis_input_unlock(flags); return EACCDN; }
    if (function == CUIS_INPUT_RECT) client->input_filter.rect[a & 1] = rect;
    else if (function == CUIS_INPUT_BUTTON) {
        client->input_filter.mask = (ULONG)a; client->input_filter.state = (ULONG)b;
        client->input_filter.clicks = (ULONG)c; client->input_filter.invert = (BOOL)d;
    } else if (function == CUIS_INPUT_INTERVAL) {
        if (b) input_interval = (ULONG)a;
        i = input_interval;
        cuis_input_unlock(flags); return (LONG)i;
    } else {
        memcpy(values, client->input_result, sizeof(values));
        cuis_input_unlock(flags);
        outputs[0] = a; outputs[1] = b; outputs[2] = c; outputs[3] = d; outputs[4] = e;
        for (i = 0; i < 5; i++)
            if (!kproc_validate_user_write(outputs[i], sizeof(LONG))) return EIMBA;
        for (i = 0; i < 5; i++)
            if (!kproc_copy_to_user(outputs[i], &values[i], sizeof(LONG))) return EIMBA;
        return E_OK;
    }
    cuis_input_unlock(flags); return E_OK;
}

LONG cuis_state_service(UQUAD handle, UQUAD x, UQUAD y, UQUAD buttons, UQUAD modifiers)
{
    CUIS_CLIENT *client = *find_owner(run);
    LONG sx, sy;
    ULONG sb, sm;
    UQUAD flags;
    BOOL copied;
    if (handle > 0x7fffffffULL) return EINVAL;
    if (!client || handle != client->handle) return EACCDN;
    if (!kproc_validate_user_write(x, sizeof(LONG)) || !kproc_validate_user_write(y, sizeof(LONG)) ||
        !kproc_validate_user_write(buttons, sizeof(ULONG)) || !kproc_validate_user_write(modifiers, sizeof(ULONG))) return EIMBA;
    flags = cuis_input_lock();
    cuis_input_snapshot(&sx, &sy, &sb, &sm);
    /* The standard vector synchronizes Line-A, including native VDI cursor
     * changes. Custom-vector packets consumed by CUIS never reach it. */
    if (!cuis_input_standard_pointer() && pointer_initialized) {
        sx = pointer_x; sy = pointer_y; sb = pointer_buttons;
    }
    copied = kproc_copy_to_user(x, &sx, sizeof(sx)) && kproc_copy_to_user(y, &sy, sizeof(sy)) &&
             kproc_copy_to_user(buttons, &sb, sizeof(sb)) && kproc_copy_to_user(modifiers, &sm, sizeof(sm));
    cuis_input_unlock(flags);
    return copied ? E_OK : EIMBA;
}

static BOOL stream_ready(void *argument)
{
    CUIS_STREAM_WAIT *wait = argument;
    CUIS_CLIENT *peer = find_handle(wait->handle);
    /* A waiting writer retains only the non-reused handle, never a pointer
     * into a receiver which can exit while the writer is suspended. */
    return !peer || (wait->write ?
        CUIS_STREAM_BYTES - peer->stream_bytes >= wait->length :
        peer->stream_bytes >= wait->length);
}

/* Exact byte-stream transfers. A parallel sender-ID ring preserves trusted
 * metadata for partial reads without imposing packet boundaries on payloads. */
LONG cuis_stream_service(ULONG function, UQUAD handle, UQUAD buffer, UQUAD length,
                         UQUAD block, UQUAD sender)
{
    CUIS_CLIENT *client = *find_owner(run), *peer;
    CUIS_PACKET *packet;
    CUIS_STREAM_WAIT wait;
    UQUAD flags;
    ULONG bytes, at, i;
    UWORD *senders;
    LONG result, pid;
    if (function != CUIS_STREAM_READ && function != CUIS_STREAM_WRITE) return EINVFN;
    if (handle > 0x7fffffffULL || length > CUIS_STREAM_MAX || block > 1) return EINVAL;
    if (!client) return EACCDN;
    if (function == CUIS_STREAM_READ && handle != client->handle) return EACCDN;
    peer = find_handle(handle);
    if (!peer) return EACCDN;
    if (!length) return 0;
    wait.handle = (ULONG)handle; wait.length = (ULONG)length;
    wait.write = function == CUIS_STREAM_WRITE;
    if (wait.write) {
        if (client->pending_write) return EACCDN;
        packet = kalloc(sizeof(*packet) + (ULONG)length - 1);
        if (!packet) return ENSMEM;
        packet->sender_pid = client->pid;
        /* Teardown also owns the snapshot if backing its input terminates
         * the writer, or a future cancellation abandons this continuation. */
        client->pending_write = packet;
        if (!kproc_copy_from_user(packet->data, buffer, (ULONG)length)) {
            client->pending_write = NULL; kfree(packet); return EIMBA;
        }
    }
    if (wait.write) {
        peer = find_handle(handle);
        if (peer && !peer->stream_data)
            peer->stream_data = kalloc(CUIS_STREAM_BYTES * (1 + sizeof(UWORD)));
        if (!peer || !peer->stream_data) {
            packet = client->pending_write; client->pending_write = NULL;
            kfree(packet); return peer ? ENSMEM : EACCDN;
        }
    }
    flags = cuis_input_lock();
    for (;;) {
        peer = find_handle(handle);
        if (!peer) { result = EACCDN; break; }
        if (stream_ready(&wait)) {
            if (wait.write) {
                packet = client->pending_write;
                at = (peer->stream_head + peer->stream_bytes) % CUIS_STREAM_BYTES;
                bytes = CUIS_STREAM_BYTES - at;
                if (bytes > length) bytes = (ULONG)length;
                memcpy(peer->stream_data + at, packet->data, bytes);
                if (bytes < length) memcpy(peer->stream_data, packet->data + bytes, (ULONG)length - bytes);
                senders = (UWORD *)(peer->stream_data + CUIS_STREAM_BYTES);
                for (i = 0; i < length; i++) senders[(at + i) % CUIS_STREAM_BYTES] = (UWORD)packet->sender_pid;
                client->pending_write = NULL;
                peer->stream_bytes += (ULONG)length;
                cuis_input_unlock(flags);
                kfree(packet);
                return (LONG)length;
            }
            /* Back the whole output before the non-yielding copy/commit.
             * Failed copyout leaves all bytes and sender metadata unread. */
            cuis_input_unlock(flags);
            if (!kproc_validate_user_write(buffer, (ULONG)length) ||
                (sender && !kproc_validate_user_write(sender, sizeof(LONG)))) return EIMBA;
            flags = cuis_input_lock();
            if (!stream_ready(&wait)) continue;
            senders = (UWORD *)(client->stream_data + CUIS_STREAM_BYTES);
            pid = senders[client->stream_head];
            bytes = CUIS_STREAM_BYTES - client->stream_head;
            if (bytes > length) bytes = (ULONG)length;
            if (!kproc_copy_to_user(buffer, client->stream_data + client->stream_head, bytes) ||
                (bytes < length && !kproc_copy_to_user(buffer + bytes, client->stream_data, (ULONG)length - bytes)) ||
                (sender && !kproc_copy_to_user(sender, &pid, sizeof(pid)))) {
                cuis_input_unlock(flags); return EIMBA;
            }
            client->stream_head = (client->stream_head + (ULONG)length) % CUIS_STREAM_BYTES;
            client->stream_bytes -= (ULONG)length;
            cuis_input_unlock(flags);
            return (LONG)length;
        }
        if (!block) { result = EAGAIN; break; }
        if (update_owner == client) { result = EACCDN; break; }
        if (!ksched_block(stream_ready, &wait)) cuis_input_idle();
    }
    cuis_input_unlock(flags);
    if (wait.write) {
        packet = client->pending_write;
        client->pending_write = NULL;
        kfree(packet);
    }
    return result;
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

/* Generic notices are converted by libaes, then atomically appended as opaque
 * bytes to the caller's own application pipe. Kernel code never builds GEM. */
LONG cuis_notice_service(ULONG function, UQUAD session, UQUAD a, UQUAD b, UQUAD c, UQUAD d)
{
    CUIS_CLIENT *client;
    CUIS_WINDOW *window;
    UBYTE data[32], *storage = NULL;
    BOOL need_storage;
    LONG result;
    ULONG kind, native, identifier, at, i;
    UWORD *senders;
    UQUAD flags;
    if (session > 0x7fffffffULL) return EINVAL;
    if (function == CUIS_NOTICE_PEEK) {
        flags = cuis_input_lock();
        client = *find_owner(run);
        if (!client || client->handle != session) { cuis_input_unlock(flags); return EACCDN; }
        if (!notices_ready(client)) { cuis_input_unlock(flags); return 0; }
        cuis_input_unlock(flags);
        if (!kproc_validate_user_write(a, sizeof(LONG)) || !kproc_validate_user_write(b, sizeof(LONG))) return EIMBA;
        flags = cuis_input_lock();
        client = *find_owner(run);
        if (!client || client->handle != session) { cuis_input_unlock(flags); return EACCDN; }
        for (window = client->windows; window && !window->notices; window = window->next) ;
        if (!window) { cuis_input_unlock(flags); return 0; }
        kind = window->notices & CUIS_NOTICE_ACTIVATE ? CUIS_NOTICE_ACTIVATE : CUIS_NOTICE_REDRAW;
        native = window->handle; identifier = window->identifier;
        if (!kproc_copy_to_user(a, &native, sizeof(LONG)) || !kproc_copy_to_user(b, &identifier, sizeof(LONG))) {
            cuis_input_unlock(flags); return EIMBA;
        }
        cuis_input_unlock(flags); return (LONG)kind;
    }
    if (function != CUIS_NOTICE_PUBLISH) return EINVFN;
    if (a > 0x7fffffffULL || (b != CUIS_NOTICE_REDRAW && b != CUIS_NOTICE_ACTIVATE) || !d || d > sizeof(data)) return EINVAL;
    flags = cuis_input_lock();
    client = *find_owner(run);
    if (!client || client->handle != session) { cuis_input_unlock(flags); return EACCDN; }
    for (window = client->windows; window && window->handle != a; window = window->next) ;
    if (!window || !(window->notices & b)) { cuis_input_unlock(flags); return EACCDN; }
    need_storage = client->stream_data == NULL;
    cuis_input_unlock(flags);
    if (!kproc_copy_from_user(data, c, (ULONG)d)) return EIMBA;
    if (need_storage) {
        storage = kalloc(CUIS_STREAM_BYTES * (1 + sizeof(UWORD)));
        if (!storage) return ENSMEM;
    }
    flags = cuis_input_lock();
    client = *find_owner(run);
    if (!client || client->handle != session) { result = EACCDN; goto publish_done; }
    for (window = client->windows; window && window->handle != a; window = window->next) ;
    if (!window || !(window->notices & b)) { result = EACCDN; goto publish_done; }
    if (CUIS_STREAM_BYTES - client->stream_bytes < d) { result = EAGAIN; goto publish_done; }
    if (!client->stream_data) {
        if (!storage) { result = EACCDN; goto publish_done; }
        client->stream_data = storage; storage = NULL;
    }
    at = (client->stream_head + client->stream_bytes) % CUIS_STREAM_BYTES;
    senders = (UWORD *)(client->stream_data + CUIS_STREAM_BYTES);
    for (i = 0; i < d; i++) {
        client->stream_data[(at + i) % CUIS_STREAM_BYTES] = data[i];
        senders[(at + i) % CUIS_STREAM_BYTES] = (UWORD)client->pid;
    }
    client->stream_bytes += (ULONG)d;
    window->notices &= ~(ULONG)b;
    if (b == CUIS_NOTICE_REDRAW) window->redraw = FALSE;
    result = E_OK;
publish_done:
    cuis_input_unlock(flags);
    if (storage) kfree(storage);
    return result;
}

LONG cuis_stack_service(ULONG function, UQUAD a, UQUAD b, UQUAD c, UQUAD d, UQUAD e)
{
    CUIS_CLIENT *client, *owner = NULL;
    CUIS_WINDOW *window = NULL, *scan, *above = NULL;
    LONG values[4] = { 0, 0, 0, 0 };
    UQUAD outputs[4], flags;
    ULONG i, count;
    if (!*find_owner(run)) return EACCDN;
    if (function == CUIS_STACK_QUERY) {
        if (a > 1) return EINVAL;
        outputs[0] = b; outputs[1] = c; outputs[2] = d; count = 3;
    } else if (function == CUIS_WINDOW_INFO) {
        if (!a || a > CUIS_WINDOW_LIMIT) return EINVAL;
        outputs[0] = b; outputs[1] = c; outputs[2] = d; outputs[3] = e; count = 4;
    } else return EINVFN;
    for (i = 0; i < count; i++) if (!kproc_validate_user_write(outputs[i], sizeof(LONG))) return EIMBA;
    if (!*find_owner(run)) return EACCDN;
    flags = cuis_input_lock();
    if (function == CUIS_STACK_QUERY) {
        window = window_front;
        if (a && window) while (window->znext) window = window->znext;
        if (window) {
            for (client = clients; client && !owner; client = client->next)
                for (scan = client->windows; scan; scan = scan->next) if (scan == window) { owner = client; break; }
            values[0] = window->identifier; values[1] = owner ? owner->pid : 0;
            values[2] = window->znext ? window->znext->identifier : 0;
        }
    } else {
        for (client = clients; client && !owner; client = client->next)
            for (scan = client->windows; scan; scan = scan->next) if (scan->identifier == a) { owner = client; window = scan; break; }
        if (!window) { cuis_input_unlock(flags); return EACCDN; }
        if (window->visible) for (scan = window_front; scan && scan != window; scan = scan->znext) above = scan;
        values[0] = owner->pid; values[1] = window->visible;
        values[2] = window->visible && above ? above->identifier : 0;
        values[3] = window->visible && window->znext ? window->znext->identifier : 0;
    }
    for (i = 0; i < count; i++) if (!kproc_copy_to_user(outputs[i], &values[i], sizeof(LONG))) {
        cuis_input_unlock(flags); return EIMBA;
    }
    cuis_input_unlock(flags); return E_OK;
}

/* Opaque peer data cannot manufacture a trusted input or redraw event. */
LONG cuis_message_service(ULONG function, UQUAD a, UQUAD b, UQUAD c,
                          UQUAD d, UQUAD e, UQUAD f)
{
    CUIS_CLIENT *client, *peer;
    CUIS_MESSAGE message;
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
    return EINVFN;
}
