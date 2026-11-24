/* Production dependency + KTHREAD ownership; CPU/PROCESS events are modeled.
 * This is not a Pexec200 or public Pvfork execution claim. */
#define main existing_join_fixture
#include "kthread_join.c"
#undef main
#include <stdio.h>
#include "fork_parent.h"
#include "process_observer.h"

static uintptr_t irq_mask;
static uintptr_t native_sched_irq_save(void) { uintptr_t old=irq_mask;irq_mask=1;return old; }
static void native_sched_irq_restore(uintptr_t old) { assert(irq_mask);irq_mask=old; }
#define NATIVE_SCHED_BACKEND_H
#include "../bdos/fork_parent.c"
#include "../bdos/process_observer.c"

PD *run;
static KTHREAD *current_thread;
static UQUAD current_execution=101;
static BOOL cancel_pending,source_peer;
static unsigned waits,action;
static struct {
    KPROC *identity;
    UQUAD number,epoch;
    PROCESS_COMPLETION completion;
} model[3];
static unsigned index_of(const KPROC *p)
{
    unsigned i;
    for(i=0;i<3;i++) if(model[i].identity==p) return i;
    abort();
}
KPROC *kproc_retain_live(KPROC *p) { return kproc_retain((PD *)p); }
PD *kproc_live_pd(const KPROC *p) { return p && p->live?(PD *)p:NULL; }
UQUAD kproc_ref_number(const KPROC *p) { return model[index_of(p)].number; }
UQUAD kproc_ref_exec_generation(const KPROC *p) { return model[index_of(p)].epoch; }
BOOL kproc_ref_completion(const KPROC *p,PROCESS_COMPLETION *out)
{ *out=model[index_of(p)].completion;return TRUE; }
BOOL kproc_ref_terminal(const KPROC *p)
{ const PROCESS_COMPLETION *c=&model[index_of(p)].completion;return c->order_low || c->state==PROCESS_ABANDONED; }
BOOL kproc_thread_is_sole(KPROC *p,KTHREAD *t)
{ return p->threads==t && !*kthread_next_link((KTHREAD *)t); }
BOOL kproc_has_aspace_peer(const KPROC *p) { (void)p;return source_peer; }
KTHREAD *ksched_exit_thread(PD *pd) { return pd==run?current_thread:NULL; }
UQUAD ksched_context_id(void) { return current_execution; }
BOOL ksched_cancellation_pending(void) { return cancel_pending; }
KSCHED_WAIT_RESULT ksched_wait_cancelable(BOOL (*ready)(void *),void *context)
{
    assert(irq_mask && !ready(context));
    waits++;
    assert(waits<5); /* Every false recheck must enter the scheduler. */
    if(action==1) { /* unrelated completion, then exact final completion */
        model[2].completion.state=PROCESS_COMPLETED;model[2].completion.order_low=18;
        if(waits==2) { model[1].completion.state=PROCESS_COMPLETED;model[1].completion.order_low=19; }
    } else if(action==2) { /* failed exec/unrelated exec, then exact commit */
        model[2].epoch++;
        if(waits==2) model[1].epoch++;
    } else if(action==3) cancel_pending=TRUE;
    else if(action==4) { /* early committed status and THREAD-only stop */
        model[1].completion.state=PROCESS_COMPLETED;
        if(waits==2) model[1].completion.order_low=20;
    } else abort();
    return cancel_pending?KSCHED_WAIT_CANCELLED:KSCHED_WAIT_READY;
}
static void reset_events(void)
{
    unsigned i;
    cancel_pending=source_peer=FALSE;waits=0;action=0;
    for(i=0;i<3;i++) { memset(&model[i].completion,0,sizeof(model[i].completion));model[i].epoch=0; }
}
int main(void)
{
    KASPACE space={1};
    KPROC parent={1,TRUE,NULL,&space}, child={1,TRUE,NULL,&space}, other={1,TRUE,NULL,&space};
    FORK_PARENT_DEPENDENCY *dep=NULL,*duplicate=NULL;
    FORK_PARENT_REASON reason=FORK_PARENT_NONE;
    KTHREAD *thread;
    unsigned i,before;
    model[0].identity=&parent;model[0].number=11;
    model[1].identity=&child;model[1].number=22;
    model[2].identity=&other;model[2].number=33;
    current_thread=thread=start(&parent);run=(PD *)&parent;
    before=allocations-frees;
    cancel_pending=TRUE;
    assert(fork_parent_prepare(thread,&child,&dep)==EACCDN && !dep);
    cancel_pending=FALSE;
    source_peer=TRUE;
    assert(fork_parent_prepare(thread,&child,&dep)==EACCDN && !dep);
    source_peer=FALSE;fail_after=1;
    assert(fork_parent_prepare(thread,&child,&dep)==ENSMEM && !dep);
    assert(allocations-frees==before && parent.refs==2 && child.refs==1);
    child.live=FALSE;
    assert(fork_parent_prepare(thread,&child,&dep)==EACCDN && !dep);
    child.live=TRUE;
    for(i=0;i<6;i++) {
        reset_events();
        assert(fork_parent_prepare(thread,&child,&dep)==E_OK);
        assert(fork_parent_validate(dep) && parent.refs==3 && child.refs==2 && fork_parent_test_live()==1);
        assert(fork_parent_prepare(thread,&other,&duplicate)==EACCDN && !duplicate);
        assert(!fork_parent_ready(dep));
        fork_parent_publish(dep);
        if(i==0) { /* exact immediate completion before wait registration */
            model[1].completion.state=PROCESS_COMPLETED;model[1].completion.order_low=1;
        } else action=i==1?1:i==2?2:i==3?3:4;
        if(i==5) { /* cancellation wins over already delivered completion */
            model[1].completion.state=PROCESS_COMPLETED;model[1].completion.order_low=1;cancel_pending=TRUE;
        }
        reason=FORK_PARENT_NONE;
        if(i==3 || i==5) {
            assert(fork_parent_wait(dep,&reason)==EACCDN);
            assert(fork_parent_state(dep)==FORK_PARENT_CANCELLED && fork_parent_reason(dep)==FORK_PARENT_CANCELLATION);
            assert(reason==FORK_PARENT_NONE);
        } else {
            assert(fork_parent_wait(dep,&reason)==E_OK);
            assert(reason==(i==2?FORK_PARENT_EXEC:FORK_PARENT_EXIT));
            assert(fork_parent_state(dep)==FORK_PARENT_RELEASED);
            assert(fork_parent_wait(dep,&reason)==EACCDN);
            fork_parent_cancel(dep,FORK_PARENT_ABORT);
            assert(fork_parent_reason(dep)==reason); /* no double release */
        }
        fork_parent_release(&dep);fork_parent_release(&dep);
        assert(!dep && !kthread_fork_dependency(thread) && !fork_parent_test_live());
        assert(parent.refs==2 && child.refs==1 && allocations-frees==before && irq_mask==0);
    }
    reset_events();
    assert(fork_parent_prepare(thread,&child,&dep)==E_OK);
    current_execution++;
    assert(!fork_parent_validate(dep));
    assert(fork_parent_wait(dep,&reason)==EACCDN);
    current_execution--;
    fork_parent_cancel(dep,FORK_PARENT_ABORT);fork_parent_release(&dep);
    assert(fork_parent_prepare(thread,&child,&dep)==E_OK);
    fork_parent_publish(dep);
    /* Existing root has unpublished its predicate before quiescent reap.
     * Its abandoned caller-stack handle is never read again. */
    current_thread=NULL;run=(PD *)&child;
    assert(kthread_stop(thread) && kthread_reap(thread));dep=NULL;
    assert(!fork_parent_test_live() && child.refs==1 && parent.refs==1);
    kthread_forget_completions(&parent);
    assert(space.refs==1 && allocations==frees);
    puts("COW parent dependency: exact child, early/late completion, exec epoch, cancellation, false wakes, rollback and quiescent THREAD reap PASS (host providers)");
    return 0;
}
