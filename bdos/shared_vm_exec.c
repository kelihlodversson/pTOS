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
#include "kprint.h"
#include "aspace32.h"
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
        error=proc_clone_prepare_startup(tx,&startup);
    }
    if(!error) {stage=4;error=proc_clone_tpa_startup_stack(tx); }
    if(!error) {stage=5;error=proc_clone_observe_async(tx,&observer); }
    if(!error && ksched_cancellation_pending()) error=EACCDN;
    /* As FreeMiNT's exec_region() does (cpushi of the text): code the
     * caller put in the TPA went in through the data side, so publish it
     * before the child's first fetch (68040/68060 copyback caches). */
    if(!error) aspace_hw_code_changed();
    if(!error) {stage=6;error=proc_clone_spawn(&tx,&ids); }
    if(!error) kproc_shared_exec_finish(target);
done:
    if(error) KDEBUG(("shared VM preparation stage=%u error=%ld\n",stage,(long)error));
    UNUSED(stage);
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
