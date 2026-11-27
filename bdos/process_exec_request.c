/* Fixed-width PROCESS request decoding. */
#include "emutos.h"
#include "gemerror.h"
#include "kheap.h"
#include "ksched.h"
#include "procmem.h"
#include "string.h"
#include "launch_args.h"
#include "process_exec_internal.h"
extern PD *run;
#if CONF_WITH_USER_ASPACE && CONF_WITH_MULTITHREADING
#define EXEC_PATH_MAX 1024UL
#define EXEC_ENV_MAX 32766UL /* Existing loader envsize() counts in a WORD. */

static LONG copy_argument(ULONG address, ULONG capacity, BOOL environment, char **out)
{
    char *buffer;
    if (!address) return EIMBA;
    buffer = kalloc(capacity);
    if (!buffer) return ENSMEM;
    *out = buffer;
    return launch_copy_user_string(address,buffer,capacity,environment,NULL);
}
LONG proc_exec_prepare_user_args(KPROC *owner, const PROCESS_EXEC_REQUEST *request)
{
    char *path=NULL, *tail=NULL, *environment=NULL, *directory=NULL;
    PEXECVE_ARGS *vectors = NULL;
    UBYTE classic_tail[PDCLSIZE];
    ULONG env_address;
    LONG result;
    if (!owner || kproc_live_pd(owner) != run) return EACCDN;
    if (request->handle || request->policy > PROCESS_EXEC_DEFER_REPLACEMENT) return EINVAL;
    if (request->flags & PROCESS_EXEC_VECTOR_ARGS) {
        ULONG i;

        result = pexecve_args_capture(PE_ASYNC_LOADGO,request->path,request->tail,
                                      request->environment,&vectors);
        if (result != E_OK) goto done;
        if (request->flags & PROCESS_EXEC_ACCESSORY)
            vectors->entry_type = DL_ENTRY_ACCESSORY;
        if (!vectors->argc) { result=EINVAL; goto done; }
        for (i = 0; i < vectors->envc; i++)
            if (!strncmp(vectors->envp[i],"ARGV=",5)) { result=EINVAL; goto done; }
        if (request->classic_tail) {
            result=kproc_copy_from_user_checked(classic_tail,request->classic_tail,sizeof(classic_tail));
            if (result != E_OK) goto done;
            if (classic_tail[0] > 126) { result=EINVAL; goto done; }
            memset(classic_tail+classic_tail[0]+1,0,sizeof(classic_tail)-classic_tail[0]-1);
            vectors->classic_tail=classic_tail;
        }
    } else {
        result=copy_argument(request->path,EXEC_PATH_MAX,FALSE,&path);
        if (result != E_OK) goto done;
        result=copy_argument(request->tail,PDCLSIZE,FALSE,&tail);
        if (result != E_OK) goto done;
        env_address=request->environment ? request->environment :
#if CONF_WITH_PRIVATE_CLONE
            (ULONG)kproc_private_environment(run);
#else
            (ULONG)(uintptr_t)USERPTR_TO_PTR(run->p_env);
#endif
        result=copy_argument(env_address,EXEC_ENV_MAX,TRUE,&environment);
        if (result != E_OK) goto done;
    }
    if (request->directory) {
        result=copy_argument(request->directory,EXEC_PATH_MAX,FALSE,&directory);
        if (result != E_OK) goto done;
        if (!directory[0]) { result=EPTHNF; goto done; }
    }
    result=vectors ? proc_exec_handle_prepare_vectors(owner,vectors,directory,request->policy) :
        proc_exec_handle_prepare(owner,path,tail,environment,directory,request->policy);
 done:
    if (vectors) pexecve_args_release(&vectors);
    if (directory) kfree(directory);
    if (environment) kfree(environment);
    if (tail) kfree(tail);
    if (path) kfree(path);
    return result;
}
LONG proc_exec_copy_request(UQUAD address, UQUAD size, PROCESS_EXEC_REQUEST *request)
{
    LONG result;

    if (size != sizeof(*request)) return EINVAL;
    if (!address || address > 0xffffffffULL - (sizeof(*request)-1)) return EIMBA;
    if (address & 3) return EINVAL;
    result=kproc_copy_from_user_checked(request,address,sizeof(*request));
    if (result!=E_OK) return result;
    if (request->reserved ||
        (request->flags & ~(PROCESS_EXEC_VECTOR_ARGS | PROCESS_EXEC_ACCESSORY)) ||
        (!(request->flags & PROCESS_EXEC_VECTOR_ARGS) &&
         (request->classic_tail || (request->flags & PROCESS_EXEC_ACCESSORY))))
        return EINVAL;
    if ((request->flags & PROCESS_EXEC_ACCESSORY) &&
        (request->policy || (request->operation != PROCESS_EXEC_PREPARE &&
                             request->operation != PROCESS_EXEC_START)))
        return EINVAL;
    return E_OK;
}
static LONG exec_request_owned(KPROC *owner, UQUAD address, UQUAD size)
{
    PROCESS_EXEC_REQUEST request;
    PROCESS_COMPLETION completion;
    LONG result;
    LONG handle;
    BOOL direct, deferred;
    if (!owner || kproc_live_pd(owner) != run) return EACCDN;
    result=proc_exec_copy_request(address,size,&request);
    if (result!=E_OK) return result;
    if (request.operation > PROCESS_EXEC_START) return EINVFN;
    direct=request.operation == PROCESS_EXEC_DEFER || request.operation == PROCESS_EXEC_START;
    if (request.operation != PROCESS_EXEC_PREPARE && !direct) {
        if (request.policy || request.flags || request.path || request.tail || request.environment || request.directory) return EINVAL;
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
    if (direct && request.policy) return EINVAL;
    deferred=request.operation == PROCESS_EXEC_DEFER;
    if (deferred) request.policy=PROCESS_EXEC_DEFER_REPLACEMENT;
    handle=proc_exec_prepare_user_args(owner,&request);
    if (!direct || handle <= 0) return handle;
    result=proc_exec_handle_commit(owner,(ULONG)handle);
    (void)proc_exec_handle_close(owner,(ULONG)handle);
    if (deferred && result > 0) result=E_OK;
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
