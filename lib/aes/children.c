/* GEM child launch/notification adapter; shell policy stays in shell.c. */
#include "emutos.h"
#include "libaes.h"
#include "application.h"
#include "transport.h"
#include "gemerror.h"
#include "bdosdefs.h"
#include "process_exec.h"

static LONG direct_launch(ULONG operation, const char *path, const char *tail,
                          const char *environment, const char *directory)
{
    PROCESS_EXEC_REQUEST request = {0};
    request.operation = operation;
    request.path = (ULONG)(uintptr_t)path;
    request.tail = (ULONG)(uintptr_t)tail;
    request.environment = (ULONG)(uintptr_t)environment;
    request.directory = (ULONG)(uintptr_t)directory;
    return (LONG)aes_gemdos_call(0x154,(UQUAD)(QUAD)PROCESS_EXEC_SSYSTEM,
                                  (uintptr_t)&request,sizeof(request));
}
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
    if (!observed && !console) {
        if (directory) return direct_launch(PROCESS_EXEC_START,path,tail,environment,directory);
        return (LONG)aes_gemdos_call4(0x4b,PE_ASYNC_LOADGO,(uintptr_t)path,(uintptr_t)tail,(uintptr_t)environment);
    }
    if (observed) {
        LONG result = aes_child_messages(session);
        if (result < 0) return result;
        watch = cuis_call6(CUIS_CHILD_WATCH,session,CUIS_CHILD_RESERVE,0,0,0,0);
        if (watch <= 0) return watch;
    }
    return cuis_call6(CUIS_LAUNCH_ATTACH,session,(ULONG)watch | (console ? CUIS_LAUNCH_CONSOLE : 0),
                      (uintptr_t)path,(uintptr_t)tail,(uintptr_t)environment,(uintptr_t)directory);
}
LONG aes_child_defer(const char *path, const char *tail,
                     const char *environment, const char *directory)
{
    return direct_launch(PROCESS_EXEC_DEFER,path,tail,environment,directory);
}
