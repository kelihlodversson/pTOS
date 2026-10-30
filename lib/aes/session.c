/* Per-process writable state in ordinary PIC data; kernel exit is the
 * authoritative cleanup for faults and _Exit, independent of destructors. */
#include "libaes.h"
#include "gemerror.h"
#include "transport.h"
static LONG session = -1;

LONG aes_session_open(void)
{
    LONG handle = cuis_call(CUIS_SESSION_START, 0);
    if (handle >= 0) session = handle;
    return handle;
}
LONG aes_session_query(LONG handle)
{
    return cuis_call(CUIS_SESSION_QUERY, handle);
}
LONG aes_session_close(LONG handle)
{
    LONG rc = cuis_call(CUIS_SESSION_END, handle);
    if (rc == E_OK && session == handle) session = -1;
    return rc;
}
__attribute__((destructor)) static void session_fini(void)
{
    if (session >= 0) (void)aes_session_close(session);
}
