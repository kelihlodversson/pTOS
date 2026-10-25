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
