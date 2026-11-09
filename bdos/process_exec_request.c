/* Fixed-width PROCESS request decoding. Public Ssystem dispatch stays gated
 * until deferred policy and the full transport contract are validated. */
#include "emutos.h"
#include "gemerror.h"
#include "kheap.h"
#include "ksched.h"
#include "string.h"
#include "process_exec_internal.h"
extern PD *run;
#if CONF_WITH_USER_ASPACE && CONF_WITH_NATIVE_SCHED
#define EXEC_PATH_MAX 1024UL
#define EXEC_ENV_MAX 32766UL /* Existing loader envsize() counts in a WORD. */

static LONG copy_argument(ULONG address, ULONG capacity, BOOL environment, char **out)
{
    ULONG i;
    char *buffer;
    if (!address) return EIMBA;
    buffer = kalloc(capacity);
    if (!buffer) return ENSMEM;
    *out = buffer;
    for (i = 0; i < capacity; i++) {
        LONG error=kproc_copy_from_user_checked(buffer+i,(UQUAD)address+i,1);
        if (error!=E_OK) return error;
        if (!buffer[i] && (!environment || (i && !buffer[i-1]))) return E_OK;
    }
    return ERANGE;
}
static LONG exec_request_owned(KPROC *owner, UQUAD address, UQUAD size)
{
    PROCESS_EXEC_REQUEST request;
    PROCESS_COMPLETION completion;
    LONG result;
    char *path=NULL, *tail=NULL, *environment=NULL, *directory=NULL;
    ULONG env_address;
    if (!owner || kproc_live_pd(owner) != run) return EACCDN;
    if (size != sizeof(request)) return EINVAL;
    if (!address || address > 0xffffffffULL - (sizeof(request)-1)) return EIMBA;
    if (address & 3) return EINVAL;
    result=kproc_copy_from_user_checked(&request,address,sizeof(request));
    if (result!=E_OK) return result;
    if (request.reserved || request.reserved_tail[0] || request.reserved_tail[1]) return EINVAL;
    if (request.operation > PROCESS_EXEC_CANCEL_DEFERRED) return EINVFN;
    if (request.operation != PROCESS_EXEC_PREPARE) {
        if (request.policy || request.path || request.tail || request.environment || request.directory) return EINVAL;
        switch (request.operation) {
        case PROCESS_EXEC_COMMIT:
            return proc_exec_handle_commit(owner,request.handle);
        case PROCESS_EXEC_SNAPSHOT:
            result=proc_exec_handle_snapshot(owner,request.handle,&completion);
            if (result != E_OK) return result;
            /* Only this non-consuming operation copies out. A partial fault
             * cannot release a handle, consume Pwait, or erase completion. */
            return kproc_copy_to_user_checked(address+offsetof(PROCESS_EXEC_REQUEST,completion),
                                              &completion,sizeof(completion));
        case PROCESS_EXEC_CLOSE:
            return proc_exec_handle_close(owner,request.handle);
        case PROCESS_EXEC_CANCEL_DEFERRED:
            if (request.handle) return EINVAL;
            return ksched_defer_cancel();
        }
        return EINVFN;
    }
    if (request.handle || request.policy > PROCESS_EXEC_DEFER_REPLACEMENT) return EINVAL;
    result=copy_argument(request.path,EXEC_PATH_MAX,FALSE,&path);
    if (result != E_OK) goto done;
    result=copy_argument(request.tail,PDCLSIZE,FALSE,&tail);
    if (result != E_OK) goto done;
    env_address=request.environment ? request.environment :
        (ULONG)(uintptr_t)USERPTR_TO_PTR(run->p_env);
    result=copy_argument(env_address,EXEC_ENV_MAX,TRUE,&environment);
    if (result != E_OK) goto done;
    if (request.directory) {
        result=copy_argument(request.directory,EXEC_PATH_MAX,FALSE,&directory);
        if (result != E_OK) goto done;
    }
    result=proc_exec_handle_prepare(owner,path,tail,environment,directory,request.policy);
 done:
    if (directory) kfree(directory);
    if (environment) kfree(environment);
    if (tail) kfree(tail);
    if (path) kfree(path);
    return result;
}

LONG proc_exec_request(KPROC *owner, UQUAD address, UQUAD size)
{
    KSCHED_CLEANUP_SCOPE scope={0};
    LONG result;
    ksched_cleanup_begin(&scope);
    result=exec_request_owned(owner,address,size);
    ksched_cleanup_end(&scope);
    return result;
}
#endif
