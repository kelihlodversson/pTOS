/* Process-owned native GUI sessions; never interpret GEM/AES wire formats. */
#include "emutos.h"
#include "cuis.h"
#include "gemerror.h"
#include "kheap.h"
#include "../bdos/kproc.h"

extern PD *run;

typedef struct cuis_window {
    struct cuis_window *next;
    ULONG handle;
    LONG x, y, width, height;
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
        while ((window = client->windows) != NULL) {
            client->windows = window->next;
            kfree(window);
        }
        *link = client->next;
        kfree(client);
    }
}

#if CONF_WITH_X86_64_MEMTEST
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
    CUIS_WINDOW **link, *window;

    if (function != CUIS_WINDOW_CREATE && function != CUIS_WINDOW_CONFIGURE &&
        function != CUIS_WINDOW_DESTROY) return EINVFN;
    client = *find_owner(run);
    if (!client) return EACCDN;
    if (function == CUIS_WINDOW_CREATE) {
        if (!valid_geometry(a, b, c, d)) return EINVAL;
        if (client->window_count >= CUIS_WINDOWS_PER_SESSION ||
            next_window_handle > 0x7fffffffUL) return ENSMEM;
        window = kalloc(sizeof(*window));
        if (!window) return ENSMEM;
        window->handle = next_window_handle++;
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
    if (function == CUIS_WINDOW_DESTROY) {
        *link = window->next;
        client->window_count--;
        kfree(window);
    } else {
        if (!valid_geometry(b, c, d, e)) return EINVAL;
        window->x = (LONG)b; window->y = (LONG)c;
        window->width = (LONG)d; window->height = (LONG)e;
    }
    return E_OK;
}
