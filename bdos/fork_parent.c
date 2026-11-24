#include "emutos.h"
#include "gemerror.h"
#include "biosext.h"
#include "kheap.h"
#include "kproc.h"
#include "kthread.h"
#include "process_observer.h"
#include "ksched.h"
#include "native_sched_backend.h"
#include "fork_parent.h"
extern PD *run;

struct fork_parent_dependency {
    KPROC *parent; /* identity only; scheduler owns parent execution/stacks */
    PROCESS_OBSERVER *child;
    KTHREAD *thread; /* borrowed under its live execution, never a ref cycle */
    UQUAD parent_number,child_number,execution,exec_generation;
    ULONG tid;
    FORK_PARENT_STATE state;
    FORK_PARENT_REASON reason;
};
static ULONG live_dependencies;
ULONG fork_parent_test_live(void) { return live_dependencies; }

static BOOL current_parent(const FORK_PARENT_DEPENDENCY *dep)
{
    KTHREAD *thread=ksched_exit_thread(run);
    return dep && thread && thread==dep->thread && kthread_process(thread)==dep->parent &&
        kthread_tid(thread)==dep->tid && ksched_context_id()==dep->execution &&
        kproc_ref_number(dep->parent)==dep->parent_number && kproc_live_pd(dep->parent)==run &&
        kthread_fork_dependency(thread)==dep;
}
LONG fork_parent_prepare(KTHREAD *thread,KPROC *child,FORK_PARENT_DEPENDENCY **out)
{
    FORK_PARENT_DEPENDENCY *dep;
    KPROC *parent;
    LONG error;
    if (!out || *out || !thread || thread!=ksched_exit_thread(run) ||
        !kthread_is_executing(thread) || kthread_fork_dependency(thread) ||
        ksched_cancellation_pending()) return EACCDN;
    parent=kthread_process(thread);
    if (!parent || parent==child || !ksched_context_id() ||
        !kproc_thread_is_sole(parent,thread) || kproc_has_aspace_peer(parent)) return EACCDN;
    if (live_dependencies==~(ULONG)0) return EAGAIN;
    dep=kalloc(sizeof(*dep));
    if (!dep) return ENSMEM;
    dep->parent=kproc_retain_live(parent);dep->child=NULL;
    dep->thread=thread;dep->parent_number=kproc_ref_number(parent);
    dep->child_number=kproc_ref_number(child);dep->execution=ksched_context_id();
    dep->tid=kthread_tid(thread);dep->exec_generation=kproc_ref_exec_generation(child);
    dep->state=FORK_PARENT_PREPARED;dep->reason=FORK_PARENT_NONE;
    error=dep->parent?process_observer_attach(child,&dep->child):EACCDN;
    if (!error && !kthread_fork_attach(thread,dep)) error=EACCDN;
    if (error) {
        process_observer_release(&dep->child);kproc_release(dep->parent);kfree(dep);return error;
    }
    live_dependencies++;
    *out=dep;
    return E_OK;
}
BOOL fork_parent_validate(const FORK_PARENT_DEPENDENCY *dep)
{
    return dep && dep->state==FORK_PARENT_PREPARED && current_parent(dep) &&
        kproc_thread_is_sole(dep->parent,dep->thread) &&
        kproc_ref_number(dep->child)==dep->child_number && kproc_live_pd(dep->child) &&
        kproc_ref_exec_generation(dep->child)==dep->exec_generation && !kproc_ref_terminal(dep->child);
}
void fork_parent_publish(FORK_PARENT_DEPENDENCY *dep)
{
    if (!fork_parent_validate(dep)) panic("fork parent publication identity changed");
    dep->state=FORK_PARENT_PUBLISHED;
}
void fork_parent_cancel(FORK_PARENT_DEPENDENCY *dep,FORK_PARENT_REASON reason)
{
    if (!dep || dep->state==FORK_PARENT_RELEASED || dep->state==FORK_PARENT_CANCELLED) return;
    if (reason!=FORK_PARENT_ABORT && reason!=FORK_PARENT_CANCELLATION &&
        reason!=FORK_PARENT_RETIRED && reason!=FORK_PARENT_INVALID) panic("fork parent invalid cancellation");
    dep->reason=reason;dep->state=FORK_PARENT_CANCELLED;
}
static FORK_PARENT_REASON child_reason(const FORK_PARENT_DEPENDENCY *dep)
{
    PROCESS_COMPLETION snapshot;
    UQUAD generation;
    if (!dep || dep->state!=FORK_PARENT_PUBLISHED) return FORK_PARENT_NONE;
    if (kproc_ref_number(dep->child)!=dep->child_number) panic("fork child incarnation changed");
    if (process_observer_snapshot(dep->child,&snapshot) && snapshot.state==PROCESS_COMPLETED &&
        (snapshot.order_low || snapshot.order_high)) return FORK_PARENT_EXIT;
    generation=kproc_ref_exec_generation(dep->child);
    if (generation<dep->exec_generation) panic("fork child exec generation regressed");
    return generation>dep->exec_generation?FORK_PARENT_EXEC:FORK_PARENT_NONE;
}
BOOL fork_parent_ready(void *context)
{
    const FORK_PARENT_DEPENDENCY *dep=context;
    return dep && (dep->state==FORK_PARENT_CANCELLED || dep->state==FORK_PARENT_RELEASED ||
        child_reason(dep)!=FORK_PARENT_NONE ||
        (dep->state==FORK_PARENT_PUBLISHED && kproc_ref_terminal(dep->child)));
}
LONG fork_parent_wait(FORK_PARENT_DEPENDENCY *dep,FORK_PARENT_REASON *out)
{
    uintptr_t flags;
    LONG error=EACCDN;
    if (!dep || !out || dep->state!=FORK_PARENT_PUBLISHED || !current_parent(dep)) return EACCDN;
    flags=native_sched_irq_save();
    for (;;) {
        FORK_PARENT_REASON reason;
        KSCHED_WAIT_RESULT wait;
        /* Cancellation wins even when completion/exec is already ready. */
        if (ksched_cancellation_pending()) { fork_parent_cancel(dep,FORK_PARENT_CANCELLATION);break; }
        if (!current_parent(dep)) { fork_parent_cancel(dep,FORK_PARENT_INVALID);break; }
        reason=child_reason(dep);
        if (reason!=FORK_PARENT_NONE) {
            dep->reason=reason;dep->state=FORK_PARENT_RELEASED;*out=reason;error=E_OK;break;
        }
        if (dep->state!=FORK_PARENT_PUBLISHED || kproc_ref_terminal(dep->child)) {
            fork_parent_cancel(dep,FORK_PARENT_INVALID);break;
        }
        wait=ksched_wait_cancelable(fork_parent_ready,dep);
        if (wait==KSCHED_WAIT_CANCELLED) { fork_parent_cancel(dep,FORK_PARENT_CANCELLATION);break; }
        if (wait==KSCHED_WAIT_UNMANAGED) { fork_parent_cancel(dep,FORK_PARENT_INVALID);error=EINVFN;break; }
        /* A false/spurious wake goes back through real blocking, never poll. */
    }
    native_sched_irq_restore(flags);
    return error;
}
static void dispose_dependency(FORK_PARENT_DEPENDENCY *dep)
{
    process_observer_release(&dep->child);kproc_release(dep->parent);
    if (!live_dependencies) panic("fork parent dependency underflow");
    live_dependencies--;kfree(dep);
}
void fork_parent_release(FORK_PARENT_DEPENDENCY **out)
{
    FORK_PARENT_DEPENDENCY *dep;
    if (!out || !*out) return;
    dep=*out;
    if (!current_parent(dep) || !kthread_fork_detach(dep->thread,dep))
        panic("fork parent release lost execution owner");
    *out=NULL;
    fork_parent_cancel(dep,FORK_PARENT_ABORT);
    dispose_dependency(dep);
}
void fork_parent_retire(FORK_PARENT_DEPENDENCY **out)
{
    FORK_PARENT_DEPENDENCY *dep;
    if (!out || !*out) return;
    dep=*out;*out=NULL;
    fork_parent_cancel(dep,FORK_PARENT_RETIRED);
    dispose_dependency(dep);
}
FORK_PARENT_STATE fork_parent_state(const FORK_PARENT_DEPENDENCY *dep) { return dep->state; }
FORK_PARENT_REASON fork_parent_reason(const FORK_PARENT_DEPENDENCY *dep) { return dep->reason; }
KPROC *fork_parent_child(const FORK_PARENT_DEPENDENCY *dep) { return dep?dep->child:NULL; }
UQUAD fork_parent_execution(const FORK_PARENT_DEPENDENCY *dep) { return dep?dep->execution:0; }
