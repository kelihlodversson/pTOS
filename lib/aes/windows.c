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
