/* Native window metadata; GEM decoration and compatibility stay in libaes. */
#include "libaes.h"
#include "transport.h"

LONG aes_window_create(LONG x, LONG y, LONG width, LONG height)
{
    return cuis_call5(CUIS_WINDOW_CREATE, x, y, width, height, 0);
}
LONG aes_window_configure(LONG handle, LONG x, LONG y, LONG width, LONG height)
{
    return cuis_call5(CUIS_WINDOW_CONFIGURE, handle, x, y, width, height);
}
LONG aes_window_destroy(LONG handle)
{
    return cuis_call5(CUIS_WINDOW_DESTROY, handle, 0, 0, 0, 0);
}

LONG aes_window_show(LONG handle)
{
    return cuis_call(CUIS_WINDOW_SHOW, handle);
}

LONG aes_window_hide(LONG handle)
{
    return cuis_call(CUIS_WINDOW_HIDE, handle);
}

LONG aes_window_raise(LONG handle)
{
    return cuis_call(CUIS_WINDOW_RAISE, handle);
}

LONG aes_window_lower(LONG handle)
{
    return cuis_call(CUIS_WINDOW_LOWER, handle);
}

LONG aes_window_redraw(LONG handle)
{
    return cuis_call(CUIS_WINDOW_REDRAW, handle);
}

LONG aes_update_begin(LONG window)
{
    return cuis_call(CUIS_UPDATE_BEGIN, window);
}
LONG aes_update_end(LONG window)
{
    return cuis_call(CUIS_UPDATE_END, window);
}
LONG aes_window_clip(LONG window, ULONG index, LONG *x, LONG *y, LONG *width, LONG *height)
{
    return cuis_call6(CUIS_WINDOW_CLIP, (QUAD)window, index,
                      (uintptr_t)x, (uintptr_t)y, (uintptr_t)width, (uintptr_t)height);
}

LONG aes_window_focus(LONG window)
{
    return cuis_call(CUIS_FOCUS_REQUEST, window);
}
LONG aes_window_focused(LONG window)
{
    return cuis_call(CUIS_FOCUS_QUERY, window);
}
LONG aes_window_unfocus(LONG window)
{
    return cuis_call(CUIS_FOCUS_RELEASE, window);
}

LONG aes_key_read(LONG window, ULONG *key)
{
    return cuis_call6(CUIS_KEY_READ, (QUAD)window, (uintptr_t)key, 0, 0, 0, 0);
}
LONG aes_key_wait(LONG window, LONG timeout_ms)
{
    return cuis_call5(CUIS_KEY_WAIT, window, timeout_ms, 0, 0, 0);
}

LONG aes_pointer_read(LONG window, LONG *x, LONG *y, ULONG *buttons, ULONG *state)
{
    return cuis_call6(CUIS_POINTER_READ, (QUAD)window, (uintptr_t)x,
                      (uintptr_t)y, (uintptr_t)buttons, (uintptr_t)state, 0);
}
LONG aes_event_wait(LONG window, ULONG events, LONG timeout_ms)
{
    return cuis_call6(CUIS_EVENT_WAIT, (QUAD)window, events, (QUAD)timeout_ms, 0, 0, 0);
}

LONG aes_message_send(LONG destination, ULONG a, ULONG b, ULONG c, ULONG d)
{
    return cuis_call6(CUIS_MESSAGE_SEND, (QUAD)destination, a, b, c, d, 0);
}
LONG aes_message_read(LONG session, LONG *sender, ULONG *a, ULONG *b, ULONG *c, ULONG *d)
{
    return cuis_call6(CUIS_MESSAGE_READ, (QUAD)session, (uintptr_t)sender,
                      (uintptr_t)a, (uintptr_t)b, (uintptr_t)c, (uintptr_t)d);
}
LONG aes_message_wait(LONG session, LONG timeout_ms)
{
    return cuis_call5(CUIS_MESSAGE_WAIT, session, timeout_ms, 0, 0, 0);
}
