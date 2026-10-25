/* Process-owned native GUI sessions; never interpret GEM/AES wire formats. */
#include "emutos.h"
#include "cuis.h"
#include "gemerror.h"
#include "kheap.h"
#include "../bdos/kproc.h"

extern PD *run;

typedef struct cuis_client {
    struct cuis_client *next;
    PD *owner;
    ULONG handle;
} CUIS_CLIENT;
static CUIS_CLIENT *clients;
static ULONG next_handle;

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
        *link = client->next;
        kfree(client);
    }
}

#if CONF_WITH_X86_64_MEMTEST
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
