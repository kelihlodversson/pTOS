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
static ULONG next_handle = 1;

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

LONG cuis_service(ULONG function, UQUAD address, UQUAD size,
                    UQUAD output, UQUAD output_size)
{
    CUIS_SESSION_REQUEST request;
    CUIS_SESSION_INFO info = { sizeof(info), CUIS_VERSION, 0,
                               CUIS_FEATURE_SESSION, { 0 } };
    CUIS_CLIENT *client;
    BOOL fresh = FALSE;
    ULONG i;

    if (function != CUIS_SESSION) return EINVFN;
    if (size != sizeof(request)) return EINVAL;
    if ((address & 3) || !kproc_copy_from_user(&request, address, sizeof(request)))
        return EIMBA;
    if (request.version != CUIS_VERSION) return EOPNOTSUPP;
    if (request.size != sizeof(request) || request.flags) return EINVAL;
    for (i = 0; i < ARRAY_SIZE(request.reserved); i++)
        if (request.reserved[i]) return EINVAL;
    if (request.operation > CUIS_SESSION_DETACH) return EINVFN;
    client = *find_owner(run);
    if (request.operation != CUIS_SESSION_ATTACH) {
        if (!client || !request.handle || request.handle != client->handle)
            return EACCDN;
        if (request.operation == CUIS_SESSION_DETACH) {
            cuis_process_exit(run);
            return E_OK;
        }
    } else if (request.handle) return EINVAL;

    if (output_size != sizeof(info)) return EINVAL;
    if ((output & 3) || !kproc_validate_user_write(output, sizeof(info)))
        return EIMBA;
    if (!client) {
        /* Handles never recycle during a boot, so a stale handle cannot
         * become valid for a later registration of the same process. */
        if (!next_handle || !kproc_pid(run)) return ENSMEM;
        client = kalloc(sizeof(*client));
        if (!client) return ENSMEM;
        client->owner = run;
        client->handle = next_handle++;
        fresh = TRUE;
    }
    info.handle = client->handle;
    if (!kproc_copy_to_user(output, &info, sizeof(info))) {
        if (fresh) kfree(client);
        return EIMBA;
    }
    /* Publish only after copyout succeeds. No operation above can yield. */
    if (fresh) {
        client->next = clients;
        clients = client;
    }
    return E_OK;
}
