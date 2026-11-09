/* Test-gated Ssystem adapter. No public activation or discovery support. */
#include "emutos.h"
#include "gemerror.h"
#include "ksched.h"
#include "process_exec_internal.h"
#if CONF_WITH_PROCESS_EXEC_TEST
extern PD *run;
LONG proc_exec_dispatch(long address, long size)
{
    KSCHED_CLEANUP_SCOPE scope={0};
    KPROC *owner;
    LONG result;
    ksched_cleanup_begin(&scope);
    owner=kproc_retain(run);
    result=owner ? proc_exec_request(owner,(UQUAD)(uintptr_t)address,(UQUAD)size) : EACCDN;
    if (owner) kproc_release(owner);
    ksched_cleanup_end(&scope);
    /* osif's existing post-operation checkpoint is the public boundary.
     * Do not deliver here: callers may still own outer operation resources. */
    return result;
}
#endif
