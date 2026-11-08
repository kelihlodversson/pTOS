/* Test-only native CUIS probes. Standard GEM clients include gem.h.
 * Raw ownership/fault tests intentionally bypass the GEM adapters. */
#ifndef CUIS_NATIVE_PROBE_H
#define CUIS_NATIVE_PROBE_H
#include "gem.h"
#include "rectfunc.h"
#include "cuis.h"
static inline LONG probe_call6(ULONG function, UQUAD a, UQUAD b, UQUAD c, UQUAD d, UQUAD e, UQUAD f)
{
    register UQUAD ax __asm__("rax") = ((UQUAD)CUIS_SERVICE_CLASS << 32) | function;
    register UQUAD fourth __asm__("r10") = d, fifth __asm__("r8") = e, sixth __asm__("r9") = f;
    __asm__ volatile ("syscall" : "+a"(ax) : "D"(a), "S"(b), "d"(c), "r"(fourth), "r"(fifth), "r"(sixth)
                      : "rcx", "r11", "memory", "cc");
    return (LONG)ax;
}
static inline LONG probe_call(ULONG function, LONG handle)
{
    return probe_call6(function,(QUAD)handle,0,0,0,0,0);
}
static inline LONG probe_call5(ULONG function, LONG a, LONG b, LONG c, LONG d, LONG e)
{
    return probe_call6(function,(QUAD)a,(QUAD)b,(QUAD)c,(QUAD)d,(QUAD)e,0);
}
static inline LONG probe_session_open(void) { return probe_call(CUIS_SESSION_START,0); }
static inline LONG probe_session_query(LONG session) { return probe_call(CUIS_SESSION_QUERY,session); }
static inline LONG probe_session_close(LONG session) { return probe_call(CUIS_SESSION_END,session); }
static inline LONG probe_window_create(LONG x, LONG y, LONG width, LONG height)
{
    return probe_call5(CUIS_WINDOW_CREATE, x, y, width, height, 0);
}
static inline LONG probe_window_configure(LONG handle, LONG x, LONG y, LONG width, LONG height)
{
    return probe_call5(CUIS_WINDOW_CONFIGURE, handle, x, y, width, height);
}
static inline LONG probe_window_destroy(LONG handle)
{
    return probe_call5(CUIS_WINDOW_DESTROY, handle, 0, 0, 0, 0);
}

static inline LONG probe_window_show(LONG handle)
{
    return probe_call(CUIS_WINDOW_SHOW, handle);
}

static inline LONG probe_window_hide(LONG handle)
{
    return probe_call(CUIS_WINDOW_HIDE, handle);
}

static inline LONG probe_window_raise(LONG handle)
{
    return probe_call(CUIS_WINDOW_RAISE, handle);
}

static inline LONG probe_window_lower(LONG handle)
{
    return probe_call(CUIS_WINDOW_LOWER, handle);
}

static inline LONG probe_update_begin(LONG window)
{
    return probe_call(CUIS_UPDATE_BEGIN, window);
}
static inline LONG probe_update_end(LONG window)
{
    return probe_call(CUIS_UPDATE_END, window);
}
static inline LONG probe_window_clip(LONG window, ULONG index, LONG *x, LONG *y, LONG *width, LONG *height)
{
    return probe_call6(CUIS_WINDOW_CLIP, (QUAD)window, index,
                      (uintptr_t)x, (uintptr_t)y, (uintptr_t)width, (uintptr_t)height);
}

static inline LONG probe_window_focus(LONG window)
{
    return probe_call(CUIS_FOCUS_REQUEST, window);
}
static inline LONG probe_window_focused(LONG window)
{
    return probe_call(CUIS_FOCUS_QUERY, window);
}
static inline LONG probe_window_unfocus(LONG window)
{
    return probe_call(CUIS_FOCUS_RELEASE, window);
}


static inline LONG probe_pointer_read(LONG window, LONG *x, LONG *y, ULONG *buttons, ULONG *state)
{
    return probe_call6(CUIS_POINTER_READ, (QUAD)window, (uintptr_t)x,
                      (uintptr_t)y, (uintptr_t)buttons, (uintptr_t)state, 0);
}
static inline LONG probe_event_wait(LONG window, ULONG events, LONG timeout_ms)
{
    return probe_call6(CUIS_EVENT_WAIT, (QUAD)window, events, (QUAD)timeout_ms, 0, 0, 0);
}

#endif
