/* Shared-VM executable policy. Construction/dispatch/reaping stay in their
 * authoritative owners. All fallible preparation is covered by one execution
 * cleanup envelope; a synchronous wait owns no launch-local references. */
#include "emutos.h"
#include "fs.h"
#include "proc.h"
#include "kproc.h"
#include "proc_clone.h"
#include "ksched.h"
#include "gemerror.h"
#include "procmem.h"
#include "process_completion.h"
#include "biosext.h"
extern PD *run;
LONG proc_shared_vm_exec(PD *staged,BOOL asynchronous)
{
    KSCHED_CLEANUP_SCOPE scope={0};
    KPROC *source=NULL,*target=NULL;
    PROC_CLONE_TRANSACTION *tx=NULL;
    PROC_CLONE_OBSERVER *observer=NULL;
    PROC_CLONE_IDENTITIES ids={0,0};
    THREAD_STARTUP_ARGS startup={0};
    KASPACE_PERMISSION_TRANSACTION *permissions=NULL;
    PROCESS_COMPLETION completed;
    PD *controller;
    LONG error;UWORD stage=0;BOOL prepared=FALSE;
    ksched_cleanup_begin(&scope);
    source=kproc_retain(run);target=kproc_retain(staged);
    if(!source || !ksched_loaded_clone_context(source,&controller)) {error=EINVFN;goto done;}
    if(!target) {error=EIMBA;goto done;}
    if(ksched_cancellation_pending()) {error=EACCDN;goto done;}
    stage=1;error=kproc_shared_exec_prepare(source,target,&startup,&permissions);
    if(error) goto done;
    prepared=TRUE;
    stage=2;error=proc_clone_loaded_shared(source,target,&tx);
    if(!error) {
        stage=3;
#if CONF_WITH_SHARED_VM_PEXEC_TEST
        if(kproc_shared_exec_initial_sp(target))
            error=proc_clone_prepare_startup_elf(tx,&startup,kproc_shared_exec_initial_sp(target));
        else
#endif
        error=proc_clone_prepare_startup(tx,&startup);
    }
    if(!error) {stage=4;error=proc_clone_retain_startup_stack(tx); }
    if(!error) {stage=5;error=proc_clone_observe_async(tx,&observer); }
    if(!error && ksched_cancellation_pending()) error=EACCDN;
    if(!error) {stage=6;error=proc_clone_spawn(&tx,&ids); }
    if(!error) kproc_shared_exec_finish(target);
done:
#if CONF_WITH_SHARED_VM_PEXEC_TEST
    if(error) kcprintf("shared VM preparation stage=%u error=%ld\n",stage,(long)error);
#else
    UNUSED(stage);
#endif
    proc_clone_abort(&tx);
    /* A rejected live/foreign/self target may already own a committed set.
     * Only successful preparation by this invocation grants abort authority.
     * The constructor abort is idempotent with this scope's own cleanup. */
    if(error && prepared) kproc_shared_exec_abort(target);
    proc_clone_observer_release(&observer);
    kproc_release(target);kproc_release(source);
    ksched_cleanup_end(&scope); /* no checkpoint with references still owned */
    if(error) return error;
    if(asynchronous) return ids.pid;
    error=ksched_wait_completion(ids.pid,&completed); /* #909's one reap */
    if(error<0) return error;
    return completed.kind==PROCESS_COMPLETION_SIGNAL ? (LONG)completed.signal<<8 : completed.status;
}
