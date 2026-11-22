/* Generic execution identity; dispatch remains in the existing scheduler. */
#include "emutos.h"
#include "biosext.h"
#include "kheap.h"
#include "kproc.h"
#include "kaspace.h"
#include "kthread.h"
#include "kowner.h"
#include "ksched_sequence.h"
#include "fs.h"
#include "proc.h"
#include "gemerror.h"
#include "string.h"
#if CONF_WITH_FORK_CORE
#include "fork_parent.h"
#endif
#if CONF_WITH_EXEC_REPLACE
#include "exec_handoff.h"
#endif

#if CONF_WITH_MULTITHREADING
struct thread_completion {
    struct thread_completion *next;
    KPROC *process; /* borrowed only while linked; retirement unlinks it */
    KTHREAD *thread, *claimant; /* borrowed while these execution owners live */
    ULONG tid, refs;
    KSCHED_WAIT_SEQUENCE changes;
    UWORD status;
    BOOL linked, ready, has_exit;
};
static struct thread_completion *completions;
static BOOL completion_unlink(struct thread_completion *record);
static void completion_put(struct thread_completion *record)
{
    BOOL retired;
    KOWNER_GUARD guard; kowner_enter(&guard,KOWNER_PROCESS);
    if (!record || !record->refs) panic("invalid TID completion release");
    retired=!--record->refs;
    kowner_leave(&guard);
    if(retired) { kowner_assert_unlocked();kfree(record); }
}
#endif

struct kthread {
    ULONG tid;
    KPROC *process;             /* owning identity reference */
    KASPACE *space;             /* owning mapping reference */
    KTHREAD *process_next;      /* borrowed process membership */
    KASPACE_LEASE *stack_hold;
#if CONF_WITH_SHARED_VM_PEXEC
    BOOL tpa_stack;             /* 4/104: starts on its TPA's stack, held by nobody */
#endif
    void *execution;
    void (*reclaim_execution)(void *);
    UBYTE state;
    BOOL attached;
    BOOL identity_only;
    BOOL has_exit;
    UWORD exit_status;
#if CONF_WITH_FORK_CORE
    FORK_PARENT_DEPENDENCY *fork_parent;
#endif
#if CONF_WITH_MULTITHREADING
    struct thread_completion *completion, *joining;
#endif
};
enum { THREAD_NEW, THREAD_EXECUTING, THREAD_STOPPED, THREAD_REAPING };

/* Never reuse a TID during a boot. Exhaustion fails closed. */
static ULONG last_tid;
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST || CONF_WITH_PRIVATE_CLONE_TEST
static UWORD fail_attach_after;
static ULONG live_threads;
void kthread_test_fail_attach_after(UWORD count) { fail_attach_after = count; }
ULONG kthread_test_live(void) { return live_threads; }
#endif
#if CONF_WITH_X86_64_MEMTEST
void kthread_test_fail_after_attach(void) { fail_attach_after = 1; }
ULONG kthread_test_set_last_tid(ULONG tid)
{
    ULONG previous = last_tid;
    last_tid = tid;
    return previous;
}
#endif

static KTHREAD *create_thread(KPROC *process,KASPACE *space,BOOL identity_only)
{
    KTHREAD *thread;
    BOOL attached=FALSE,accepted=FALSE;
    KOWNER_GUARD guard;
    thread=kalloc(sizeof(*thread));if(!thread)return NULL;
    memset(thread,0,sizeof(*thread));
#if CONF_WITH_MULTITHREADING
    thread->completion=kalloc(sizeof(*thread->completion));
    if(!thread->completion) { kfree(thread);return NULL; }
    memset(thread->completion,0,sizeof(*thread->completion));
    thread->completion->refs=1;thread->completion->thread=thread;
#endif
    thread->state=THREAD_NEW;thread->identity_only=identity_only;
    kowner_enter(&guard,KOWNER_PROCESS);
    if(last_tid!=~(ULONG)0) {
        attached=kproc_thread_attach_space(process,space,thread,&thread->process,
                                           &thread->space,&thread->process_next,identity_only);
        accepted=attached;
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST || CONF_WITH_PRIVATE_CLONE_TEST
        if(attached && fail_attach_after && !--fail_attach_after)accepted=FALSE;
#endif
        if(accepted) {
            thread->attached=TRUE;thread->tid=++last_tid;
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST || CONF_WITH_PRIVATE_CLONE_TEST
            live_threads++;
#endif
        } else if(attached)kproc_thread_detach(thread->process,thread,thread->process_next);
    }
    kowner_leave(&guard);
    if(accepted)return thread;
    if(attached) { kaspace_put(thread->space);kproc_release(thread->process); }
#if CONF_WITH_MULTITHREADING
    completion_put(thread->completion);
#endif
    kfree(thread);return NULL;
}

KTHREAD *kthread_prepare(PD *pd, KASPACE *space)
{
    KPROC *process = kproc_retain(pd);
    KTHREAD *thread = process ? create_thread(process, space, FALSE) : NULL;
    kproc_release(process);
    return thread;
}

KTHREAD *kthread_create(PD *pd)
{
    KPROC *process = kproc_retain(pd);
    KTHREAD *thread = process ? create_thread(process, kproc_active_space(process), FALSE) : NULL;
    kproc_release(process);
    return thread;
}

KTHREAD *kthread_construct_identity(KPROC *process, KASPACE *space)
{
    return create_thread(process, space, TRUE);
}

BOOL kthread_set_execution(KTHREAD *thread, void *resource, void (*reclaim)(void *))
{
    if (!thread || thread->identity_only || thread->state != THREAD_NEW || thread->reclaim_execution ||
        !resource || !reclaim)
        return FALSE;
    thread->execution = resource;
    thread->reclaim_execution = reclaim;
    return TRUE;
}

BOOL kthread_take_stack_hold(KTHREAD *thread, KASPACE_LEASE **hold)
{
    if (!thread || thread->state != THREAD_NEW || thread->stack_hold || !hold ||
        kaspace_stack_hold_space(*hold) != thread->space) return FALSE;
    thread->stack_hold = *hold; *hold = NULL;
    return TRUE;
}
#if CONF_WITH_SHARED_VM_PEXEC
BOOL kthread_use_tpa_stack(KTHREAD *thread)
{
    if (!thread || thread->state != THREAD_NEW || thread->stack_hold || thread->tpa_stack) return FALSE;
    thread->tpa_stack = TRUE;
    return TRUE;
}
#endif

#if CONF_WITH_MULTITHREADING
BOOL kthread_clone_run_ready(const KTHREAD *thread)
{
    return thread && thread->identity_only && thread->state == THREAD_NEW &&
        thread->attached && (thread->stack_hold
#if CONF_WITH_SHARED_VM_PEXEC
        || thread->tpa_stack
#endif
        ) && !thread->execution &&
        (kproc_thread_is_sole(thread->process, (KTHREAD *)thread) ||
         kproc_clone_sibling_ready(thread->process, (KTHREAD *)thread, thread->space));
}
void kthread_clone_commit(KTHREAD *thread, void *resource, void (*reclaim)(void *))
{
    if (!kthread_clone_run_ready(thread) || !kproc_clone_execution_bound(thread->process, thread) ||
        !resource || !reclaim) panic("unprepared clone THREAD activation");
    thread->execution = resource; thread->reclaim_execution = reclaim;
    thread->identity_only = FALSE;
}
#endif

static BOOL kthread_start_owned(KTHREAD *thread)
{
    if (!thread || thread->identity_only || thread->state != THREAD_NEW || !thread->attached ||
        !thread->reclaim_execution || !kproc_thread_start_allowed(thread->process) ||
        kproc_active_space(thread->process) != thread->space)
        return FALSE;
#if CONF_WITH_MULTITHREADING
    {
        struct thread_completion *record = thread->completion;
        record->tid = thread->tid;
        record->process = thread->process;
        record->next = completions; completions = record;
        record->linked = TRUE; record->refs++;
    }
#endif
    thread->state = THREAD_EXECUTING;
    return TRUE;
}
BOOL kthread_start(KTHREAD *thread)
{
    KOWNER_GUARD owner_guard; kowner_enter(&owner_guard,KOWNER_PROCESS);
    BOOL result=kthread_start_owned(thread);
    kowner_leave(&owner_guard);
    return result;
}

BOOL kthread_is_executing(const KTHREAD *thread)
{
    return thread && thread->state == THREAD_EXECUTING;
}

#if CONF_WITH_EXEC_REPLACE
BOOL kthread_exec_handoff_ready(const KTHREAD *thread, const void *execution)
{
    return thread && kthread_is_executing(thread) && thread->attached &&
        !thread->identity_only && thread->execution == execution && thread->reclaim_execution
#if CONF_WITH_MULTITHREADING
        && !thread->joining
#endif
        ;
}
void kthread_exec_handoff_stack(KTHREAD *thread, const KSCHED_EXEC_HANDOFF *handoff,
                               KASPACE_LEASE **hold, KASPACE_LEASE **old)
{
    if (!thread || !kthread_exec_handoff_ready(thread, thread->execution) ||
        !ksched_exec_handoff_quiescent(handoff, thread, thread->execution) ||
        !hold || (*hold ? kaspace_stack_hold_space(*hold) != thread->space
                        : !ksched_exec_handoff_replacing(handoff)) || !old || *old)
        panic("exec handoff: unauthenticated stack transfer");
    /* Only Pexec(200) installs an empty hold: the new image's stack is its PROCESS's. */
    *old = thread->stack_hold;
    thread->stack_hold = *hold;
    *hold = NULL;
    /* Membership, state, root, backend, TID and join completion stay intact. */
}
/* Pexec(200) of a nested PROCESS: the same rebind under the launcher's
 * relaunch token instead of the scheduler-root handoff token. */
void kthread_exec_rebind_nested(KTHREAD *thread, KASPACE **space)
{
    KASPACE *old;
    if (!thread || !space || !*space || *space == thread->space ||
        !kthread_is_executing(thread) || !thread->attached || thread->identity_only ||
        !ksched_exec_nested_committing(thread))
        panic("exec relaunch: unauthenticated THREAD rebind");
    old = thread->space;
    thread->space = *space;
    *space = old;
}
/* Pexec(200) only: the same executing THREAD adopts the committed root. The
 * caller's owning reference is exchanged for the old one; nothing is freed. */
void kthread_exec_rebind(KTHREAD *thread, const KSCHED_EXEC_HANDOFF *handoff, KASPACE **space)
{
    KASPACE *old;
    if (!thread || !space || !*space || *space == thread->space ||
        !kthread_exec_handoff_ready(thread, thread->execution) ||
        !ksched_exec_handoff_quiescent(handoff, thread, thread->execution))
        panic("exec handoff: unauthenticated THREAD rebind");
    old = thread->space;
    thread->space = *space;
    *space = old;
}
#endif

static BOOL kthread_exit_status_owned(const KTHREAD *thread, UWORD *status)
{
    if (!thread || !thread->has_exit) return FALSE;
    if (status) *status = thread->exit_status;
    return TRUE;
}
BOOL kthread_exit_status(const KTHREAD *thread, UWORD *status)
{
    KOWNER_GUARD owner_guard; kowner_enter(&owner_guard,KOWNER_PROCESS);
    BOOL result=kthread_exit_status_owned(thread,status);
    kowner_leave(&owner_guard);
    return result;
}
static BOOL kthread_finish_exit_owned(KTHREAD *thread, UWORD status)
{
    if (!kthread_finish_handoff(thread)) return FALSE;
    thread->exit_status = status;
    thread->has_exit = TRUE;
    return TRUE;
}
BOOL kthread_finish_exit(KTHREAD *thread, UWORD status)
{
    KOWNER_GUARD owner_guard; kowner_enter(&owner_guard,KOWNER_PROCESS);
    BOOL result=kthread_finish_exit_owned(thread,status);
    kowner_leave(&owner_guard);
    return result;
}
static BOOL kthread_finish_handoff_owned(KTHREAD *thread)
{
    if (!kthread_is_executing(thread) || !thread->attached ||
        !kproc_finish_thread_handoff(thread->process, thread))
        return FALSE;
    thread->attached = FALSE;
    thread->state = THREAD_STOPPED;
    return TRUE;
}
BOOL kthread_finish_handoff(KTHREAD *thread)
{
    KOWNER_GUARD owner_guard; kowner_enter(&owner_guard,KOWNER_PROCESS);
    BOOL result=kthread_finish_handoff_owned(thread);
    kowner_leave(&owner_guard);
    return result;
}

static BOOL kthread_stop_owned(KTHREAD *thread)
{
    if (!thread || (thread->state != THREAD_NEW && thread->state != THREAD_EXECUTING))
        return FALSE;
    thread->state = THREAD_STOPPED;
    return TRUE;
}
BOOL kthread_stop(KTHREAD *thread)
{
    KOWNER_GUARD owner_guard; kowner_enter(&owner_guard,KOWNER_PROCESS);
    BOOL result=kthread_stop_owned(thread);
    kowner_leave(&owner_guard);
    return result;
}

BOOL kthread_is_stopped(const KTHREAD *thread)
{
    return thread && thread->state == THREAD_STOPPED;
}

BOOL kthread_reap(KTHREAD *thread)
{
    BOOL dispose;
    KPROC *process;
    KOWNER_GUARD guard; kowner_enter(&guard,KOWNER_PROCESS);
    if (!thread || thread->state != THREAD_STOPPED) {
        kowner_leave(&guard);return FALSE;
    }
    thread->state = THREAD_REAPING;
    kowner_leave(&guard);
    if (thread->reclaim_execution)
        thread->reclaim_execution(thread->execution);
#if CONF_WITH_FORK_CORE
    /* Root has unpublished the blocked predicate and stopped execution. */
    fork_parent_retire(&thread->fork_parent);
#endif
    kaspace_lease_release(&thread->stack_hold);
    process = thread->process;
    dispose = thread->attached &&
        kproc_thread_detach(process, thread, thread->process_next);
    kaspace_put(thread->space);
#if CONF_WITH_MULTITHREADING
    kthread_join_cancel(thread);
    kowner_enter(&guard,KOWNER_PROCESS);
    thread->completion->thread = NULL;
    thread->completion->status = thread->has_exit ? thread->exit_status : 0;
    thread->completion->has_exit = thread->has_exit;
    /* Only now can a joiner reuse the user stack: both execution stacks and
     * its durable mapping hold are gone, and this continuation is dead. */
    thread->completion->ready = TRUE;
    ksched_sequence_advance(&thread->completion->changes);
    if (!thread->has_exit && completion_unlink(thread->completion))
        panic("TID completion lost its execution reference");
    kowner_leave(&guard);
    completion_put(thread->completion);
#endif
    kfree(thread);
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST || CONF_WITH_PRIVATE_CLONE_TEST
    live_threads--;
#endif
    if (dispose)
        proc_dispose_process(process);
    kproc_release(process);
    return TRUE;
}

BOOL kthread_complete(KTHREAD *thread, UWORD status)
{
    if (!thread || thread->state != THREAD_STOPPED ||
        !proc_prepare_completion(thread->process, thread, status))
        return FALSE;
    return kthread_reap(thread);
}

void kthread_destroy(KTHREAD *thread)
{
    if (!thread)
        return;
    if (kthread_is_executing(thread))
        panic("kthread_destroy: execution has not stopped");
    kthread_stop(thread);
    kthread_reap(thread);
}

ULONG kthread_tid(const KTHREAD *thread) { return thread ? thread->tid : 0; }
KPROC *kthread_process(const KTHREAD *thread) { return thread ? thread->process : NULL; }
KASPACE *kthread_aspace(const KTHREAD *thread) { return thread ? thread->space : NULL; }
#if CONF_WITH_FORK_CORE
BOOL kthread_fork_attach(KTHREAD *thread,FORK_PARENT_DEPENDENCY *dep)
{
    if (!thread || thread->state!=THREAD_EXECUTING || thread->fork_parent || !dep) return FALSE;
    thread->fork_parent=dep;return TRUE;
}
BOOL kthread_fork_detach(KTHREAD *thread,const FORK_PARENT_DEPENDENCY *dep)
{
    if (!thread || !dep || thread->fork_parent!=dep) return FALSE;
    thread->fork_parent=NULL;return TRUE;
}
FORK_PARENT_DEPENDENCY *kthread_fork_dependency(const KTHREAD *thread)
{ return thread?thread->fork_parent:NULL; }
#endif

/* Used only to walk the process's non-owning membership list. */
KTHREAD **kthread_next_link(KTHREAD *thread) { return &thread->process_next; }

#if CONF_WITH_MULTITHREADING
/* Serialized cooperative lifecycle operations. Registry membership owns only
 * small completion metadata, never PROCESS/ASPACE/resources or a user PD. */
/* Called with PROCESS ownership. Detach only; caller frees after unlocking. */
static BOOL completion_unlink(struct thread_completion *record)
{
    struct thread_completion **link;
    if (!record->linked) return FALSE;
    for (link = &completions; *link != record; link = &(*link)->next)
        if (!*link) panic("lost TID completion");
    *link = record->next;
    record->next = NULL; record->linked = FALSE; record->process = NULL;
    if(!record->refs)panic("TID registry reference underflow");
    return !--record->refs;
}
void kthread_forget_completions(KPROC *process)
{
    struct thread_completion *record,*next,*retired=NULL;
    KOWNER_GUARD guard; kowner_enter(&guard,KOWNER_PROCESS);
    record=completions;
    while(record) {
        next=record->next;
        if(record->process==process && completion_unlink(record)) {
            record->next=retired;retired=record;
        }
        record=next;
    }
    kowner_leave(&guard);
    while(retired) { record=retired;retired=record->next;kowner_assert_unlocked();kfree(record); }
}
static LONG kthread_join_begin_owned(KTHREAD *caller, ULONG tid)
{
    struct thread_completion *record, *edge;
    KTHREAD *target;
    if (!kthread_is_executing(caller) || caller->joining) return EACCDN;
    if (!tid || tid > 0x7fffffffUL) return ESRCH;
    if (tid == caller->tid) return EDEADLK;
    for (record = completions; record; record = record->next)
        if (record->tid == tid && record->process == caller->process) break;
    if (!record || (record->ready && !record->has_exit)) return ESRCH;
    if (record->claimant) return EACCDN;
    /* Joining edges are acyclic. A join that closes a cycle is rejected;
     * traversal is bounded by the finite live completion registry. */
    for (target = record->thread; target && (edge = target->joining); target = edge->thread)
        if (edge == caller->completion) return EDEADLK;
    if(record->refs==~(ULONG)0)return ENSMEM;
    record->refs++;
    record->claimant = caller;
    caller->joining = record;
    return E_OK;
}
LONG kthread_join_begin(KTHREAD *caller, ULONG tid)
{
    KOWNER_GUARD owner_guard; kowner_enter(&owner_guard,KOWNER_PROCESS);
    LONG result=kthread_join_begin_owned(caller,tid);
    kowner_leave(&owner_guard);
    return result;
}
static BOOL kthread_join_ready_owned(void *resource)
{
    KTHREAD *caller = resource;
    return caller->joining && caller->joining->ready;
}
BOOL kthread_join_ready(void *resource)
{
    KOWNER_GUARD owner_guard; kowner_enter(&owner_guard,KOWNER_PROCESS);
    BOOL result=kthread_join_ready_owned(resource);
    kowner_leave(&owner_guard);
    return result;
}
static BOOL kthread_join_status_owned(KTHREAD *caller, UWORD *status)
{
    if (!caller || !caller->joining || !caller->joining->ready ||
        !caller->joining->has_exit) return FALSE;
    *status = caller->joining->status;
    return TRUE;
}
BOOL kthread_join_status(KTHREAD *caller, UWORD *status)
{
    KOWNER_GUARD owner_guard; kowner_enter(&owner_guard,KOWNER_PROCESS);
    BOOL result=kthread_join_status_owned(caller,status);
    kowner_leave(&owner_guard);
    return result;
}
void kthread_join_observe(KTHREAD *caller,KSCHED_WAIT_OBSERVATION *observation)
{
    KOWNER_GUARD guard; kowner_enter(&guard,KOWNER_PROCESS);
    if(!caller || !caller->joining)panic("TID observation without retained claim");
    ksched_sequence_observe(&caller->joining->changes,observation);
    kowner_leave(&guard);
}
void kthread_join_cancel(KTHREAD *caller)
{
    struct thread_completion *record;
    KOWNER_GUARD guard; kowner_enter(&guard,KOWNER_PROCESS);
    if (!caller || !(record = caller->joining)) { kowner_leave(&guard);return; }
    if (record->claimant != caller) panic("TID join claim owner changed");
    caller->joining = NULL; record->claimant = NULL;
    kowner_leave(&guard);
    completion_put(record);
}
void kthread_join_consume(KTHREAD *caller)
{
    BOOL retired;
    struct thread_completion *record;
    KOWNER_GUARD guard; kowner_enter(&guard,KOWNER_PROCESS);
    record=caller->joining;
    if (!record || record->claimant != caller || !record->ready || !record->has_exit)
        panic("invalid TID completion consume");
    if(completion_unlink(record))panic("TID consume lost its claimant reference");
    caller->joining=NULL;record->claimant=NULL;
    if(!record->refs)panic("TID claimant reference underflow");
    retired=!--record->refs;
    kowner_leave(&guard);
    if(retired) { kowner_assert_unlocked();kfree(record); }
}
#endif
