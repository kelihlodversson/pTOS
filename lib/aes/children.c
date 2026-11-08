/* GEM child launch/notification adapter; shell policy stays in shell.c. */
#include "emutos.h"
#include "gem.h"
#include "application.h"
#include "transport.h"
#include "gemerror.h"
#include "bdosdefs.h"
LONG aes_child_messages(LONG session)
{
    ULONG i;
    for (i = 0; i < CUIS_CHILDREN_PER_SESSION; i++) {
        LONG status = cuis_call6(CUIS_CHILD_RESULT,session,0,0,0,0,0), result;
        short message[8] = { CH_EXIT,0,0,0,0,0,0,0 };
        if (status <= 0) return status;
        message[3] = (short)((ULONG)status >> 16);
        message[4] = (short)status;
        result = cuis_call6(CUIS_CHILD_PUBLISH,session,(ULONG)status,(uintptr_t)message,sizeof(message),0,0);
        if (result == EAGAIN) return E_OK;
        if (result != E_OK) return result;
    }
    return E_OK;
}

/* Inputs are prepared by shel_write; the caller keeps ownership of its
 * environment until the synchronous transport call has copied it. */
LONG aes_child_launch(LONG session, const char *path, const char *tail,
                      const char *environment, const char *directory, BOOL console, BOOL observed)
{
    LONG watch = 0;
    if (observed) {
        LONG result = aes_child_messages(session);
        if (result < 0) return result;
        watch = cuis_call6(CUIS_CHILD_WATCH,session,CUIS_CHILD_RESERVE,0,0,0,0);
        if (watch <= 0) return watch;
    }
    if (directory)
        return cuis_call6(console ? CUIS_CONSOLE_LAUNCH_DIRECTORY : CUIS_CHILD_LAUNCH_DIRECTORY,
            session,watch,(uintptr_t)path,(uintptr_t)tail,(uintptr_t)environment,(uintptr_t)directory);
    if (watch || console)
        return cuis_call6(CUIS_CHILD_LAUNCH,session,watch,(uintptr_t)path,
            (uintptr_t)tail,(uintptr_t)environment,console);
    return (LONG)aes_gemdos_call4(0x4b,PE_ASYNC_LOADGO,(uintptr_t)path,(uintptr_t)tail,(uintptr_t)environment);
}
LONG aes_child_defer(LONG session, const char *path, const char *tail,
                     const char *environment, const char *directory)
{
    return cuis_call6(directory ? CUIS_PROCESS_DEFER_DIRECTORY : CUIS_PROCESS_DEFER,
        session,(uintptr_t)path,(uintptr_t)tail,(uintptr_t)environment,(uintptr_t)directory,0);
}
