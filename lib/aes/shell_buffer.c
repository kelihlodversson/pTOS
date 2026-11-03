/* Standard shared desktop shell context, with classic and AES4 size semantics.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "gem.h"
#include "application.h"
#include "transport.h"
#include "resource_context.h"
#include "gemerror.h"
#include "string.h"
#define SHELL_CONTEXT_SLOT 0
#define SHELL_INITIAL_BYTES 4192

static BOOL release_context(LONG session)
{
    if (cuis_call6(CUIS_DISPLAY_LOCK,session,0,0,0,0,0) == E_OK) return TRUE;
    /* A failed release has not decremented our nesting level. */
    (void)cuis_call6(CUIS_DISPLAY_LOCK,session,0,0,0,0,0);
    return FALSE;
}
short mt_shel_get(char *buffer, short length, short *global)
{
    LONG session = aes_application_session(global), size, bytes, result;
    void *staged = NULL;
    BOOL ok;
    if (session < 0 || length < SHEL_BUFSIZE || (length > 0 && !buffer)) return 0;
    if (!length) return 1;
    if (length > 0) {
        staged = aes_temporary_alloc(length);
        if (!staged) return 0;
    }
    if (cuis_call6(CUIS_DISPLAY_LOCK,session,1,1,0,0,0) != E_OK) {
        if (staged) (void)aes_resource_discard(staged);
        return 0;
    }
    size = cuis_call6(CUIS_CONTEXT,session,SHELL_CONTEXT_SLOT,CUIS_CONTEXT_SIZE,0,0,0);
    ok = size >= 0 && (ULONG)size <= CUIS_CONTEXT_BYTES;
    if (ok && size < SHELL_INITIAL_BYTES) size = SHELL_INITIAL_BYTES;
    bytes = ok && length > 0 ? (length < size ? length : size) : 0;
    if (ok && length > 0)
        ok = cuis_call6(CUIS_CONTEXT,session,SHELL_CONTEXT_SLOT,CUIS_CONTEXT_READ,(uintptr_t)staged,bytes,0) == E_OK;
    if (!release_context(session)) ok = FALSE;
    result = ok ? (length == SHEL_BUFSIZE ? size : 1) : 0;
    if (ok && length > 0) memcpy(buffer,staged,bytes);
    if (staged) (void)aes_resource_discard(staged);
    return result;
}
short shel_get(char *buffer, short length) { return mt_shel_get(buffer,length,aes_global); }
short mt_shel_put(const char *buffer, short length, short *global)
{
    LONG session = aes_application_session(global);
    BOOL ok;
    if (session < 0 || length < 0 || (length && !buffer)) return 0;
    if (!length) return 1;
    if (cuis_call6(CUIS_DISPLAY_LOCK,session,1,1,0,0,0) != E_OK) return 0;
    ok = cuis_call6(CUIS_CONTEXT,session,SHELL_CONTEXT_SLOT,CUIS_CONTEXT_WRITE,(uintptr_t)buffer,length,0) == E_OK;
    if (!release_context(session)) ok = FALSE;
    return ok ? 1 : 0;
}
short shel_put(const char *buffer, short length) { return mt_shel_put(buffer,length,aes_global); }
