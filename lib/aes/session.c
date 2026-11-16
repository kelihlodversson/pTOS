/* Stateless transport. Explicit contexts close their sessions; kernel exit
 * remains authoritative for faults, _Exit and omitted appl_exit calls. */
#include "native_bindings.h"
#include "gemerror.h"
#include "transport.h"

LONG aes_session_open(void)
{
    return cuis_call(CUIS_SESSION_START, 0);
}
LONG aes_session_query(LONG handle)
{
    return cuis_call(CUIS_SESSION_QUERY, handle);
}
LONG aes_session_close(LONG handle)
{
    return cuis_call(CUIS_SESSION_END, handle);
}
