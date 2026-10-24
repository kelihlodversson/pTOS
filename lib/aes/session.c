/* Per-process writable state in ordinary PIC data; kernel exit is the
 * authoritative cleanup for faults and _Exit, independent of destructors. */
#include "libaes.h"
#include "gemerror.h"
#include "transport.h"
static ULONG session;

static LONG request(ULONG operation, CUIS_SESSION_INFO *info)
{
    CUIS_SESSION_REQUEST r = { sizeof(r), CUIS_VERSION, operation, 0, 0, { 0 } };
    CUIS_SESSION_INFO result;
    LONG rc;
    r.handle = operation == CUIS_SESSION_ATTACH ? 0 : session;
    rc = cuis_call(&r, operation == CUIS_SESSION_DETACH ? NULL : &result);
    if (rc == E_OK) {
        if (operation == CUIS_SESSION_DETACH) session = 0;
        else {
            session = result.handle;
            if (info) *info = result;
        }
    }
    return rc;
}
LONG aes_session_open(CUIS_SESSION_INFO *info)
{
    return request(CUIS_SESSION_ATTACH, info);
}
LONG aes_session_query(CUIS_SESSION_INFO *info)
{
    return request(CUIS_SESSION_QUERY, info);
}
LONG aes_session_close(void)
{
    if (!session) return E_OK;
    return request(CUIS_SESSION_DETACH, NULL);
}
__attribute__((destructor)) static void session_fini(void)
{
    (void)aes_session_close();
}
