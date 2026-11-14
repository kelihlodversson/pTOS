/* Generic execution identity; dispatch remains in the existing scheduler. */
#include "emutos.h"
#include "biosext.h"
#include "kheap.h"
#include "kproc.h"
#include "kaspace.h"
#include "kthread.h"
#include "fs.h"
#include "proc.h"
#include "gemerror.h"
#include "string.h"

#if CONF_WITH_PTHJOIN
struct thread_completion {
    struct thread_completion *next;
    KPROC *process; /* borrowed only while linked; retirement unlinks it */
    KTHREAD *thread, *claimant; /* borrowed while these execution owners live */
    ULONG tid, refs;
    UWORD status;
    BOOL linked, ready, has_exit;
};
static struct thread_completion *completions;
static void completion_unlink(struct thread_completion *record);
static void completion_put(struct thread_completion *record)
{
    if (!record || !record->refs) panic("invalid TID completion release");
    if (!--record->refs) kfree(record);
}
#endif

struct kthread {
    ULONG tid;
    KPROC *process;             /* owning identity reference */
    KASPACE *space;             /* owning mapping reference */
    KTHREAD *process_next;      /* borrowed process membership */
    KASPACE_LEASE *stack_hold;
    void *execution;
    void (*reclaim_execution)(void *);
    UBYTE state;
    BOOL attached;
    BOOL identity_only;
    BOOL has_exit;
    UWORD exit_status;
#if CONF_WITH_PTHJOIN
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

static KTHREAD *create_thread(KPROC *process, KASPACE *space, BOOL identity_only)
{
    KTHREAD *thread;

    if (last_tid == ~(ULONG)0)
        return NULL;
    thread = kalloc(sizeof(*thread));
    if (!thread)
        return NULL;
#if CONF_WITH_PTHJOIN
    thread->completion = kalloc(sizeof(*thread->completion));
    if (!thread->completion) { kfree(thread); return NULL; }
    bzero(thread->completion, sizeof(*thread->completion));
    thread->completion->refs = 1; /* execution owner; registry acquired at start */
    thread->completion->thread = thread;
    thread->joining = NULL;
#endif
    thread->stack_hold = NULL;
    thread->execution = NULL;
    thread->reclaim_execution = NULL;
    thread->state = THREAD_NEW;
    thread->has_exit = FALSE;
    thread->identity_only = identity_only;
    if (!kproc_thread_attach_space(process, space, thread, &thread->process,
                                   &thread->space, &thread->process_next, identity_only)) {
#if CONF_WITH_PTHJOIN
        completion_put(thread->completion);
#endif
        kfree(thread);
        return NULL;
    }
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST || CONF_WITH_PRIVATE_CLONE_TEST
    if (fail_attach_after && !--fail_attach_after) {
        kproc_thread_detach(thread->process, thread, thread->process_next);
        kaspace_put(thread->space);
        kproc_release(thread->process);
#if CONF_WITH_PTHJOIN
        completion_put(thread->completion);
#endif
        kfree(thread);
        return NULL;
    }
#endif
    thread->attached = TRUE;
    thread->tid = ++last_tid;
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST || CONF_WITH_PRIVATE_CLONE_TEST
    live_threads++;
#endif
    return thread;
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

#if CONF_WITH_NATIVE_SCHED
BOOL kthread_clone_run_ready(const KTHREAD *thread)
{
    return thread && thread->identity_only && thread->state == THREAD_NEW &&
        thread->attached && thread->stack_hold && !thread->execution &&
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

BOOL kthread_start(KTHREAD *thread)
{
    if (!thread || thread->identity_only || thread->state != THREAD_NEW || !thread->attached ||
        !thread->reclaim_execution || !kproc_thread_start_allowed(thread->process) ||
        kproc_active_space(thread->process) != thread->space)
        return FALSE;
#if CONF_WITH_PTHJOIN
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

BOOL kthread_is_executing(const KTHREAD *thread)
{
    return thread && thread->state == THREAD_EXECUTING;
}

BOOL kthread_exit_status(const KTHREAD *thread, UWORD *status)
{
    if (!thread || !thread->has_exit) return FALSE;
    if (status) *status = thread->exit_status;
    return TRUE;
}
BOOL kthread_finish_exit(KTHREAD *thread, UWORD status)
{
    if (!kthread_finish_handoff(thread)) return FALSE;
    thread->exit_status = status;
    thread->has_exit = TRUE;
    return TRUE;
}
BOOL kthread_finish_handoff(KTHREAD *thread)
{
    if (!kthread_is_executing(thread) || !thread->attached ||
        !kproc_finish_thread_handoff(thread->process, thread))
        return FALSE;
    thread->attached = FALSE;
    thread->state = THREAD_STOPPED;
    return TRUE;
}

BOOL kthread_stop(KTHREAD *thread)
{
    if (!thread || (thread->state != THREAD_NEW && thread->state != THREAD_EXECUTING))
        return FALSE;
    thread->state = THREAD_STOPPED;
    return TRUE;
}

BOOL kthread_is_stopped(const KTHREAD *thread)
{
    return thread && thread->state == THREAD_STOPPED;
}

BOOL kthread_reap(KTHREAD *thread)
{
    BOOL dispose;
    KPROC *process;
    if (!thread || thread->state != THREAD_STOPPED)
        return FALSE;
    thread->state = THREAD_REAPING;
    if (thread->reclaim_execution)
        thread->reclaim_execution(thread->execution);
    kaspace_lease_release(&thread->stack_hold);
    process = thread->process;
    dispose = thread->attached &&
        kproc_thread_detach(process, thread, thread->process_next);
    kaspace_put(thread->space);
#if CONF_WITH_PTHJOIN
    kthread_join_cancel(thread);
    thread->completion->thread = NULL;
    thread->completion->status = thread->has_exit ? thread->exit_status : 0;
    thread->completion->has_exit = thread->has_exit;
    /* Only now can a joiner reuse the user stack: both execution stacks and
     * its durable mapping hold are gone, and this continuation is dead. */
    thread->completion->ready = TRUE;
    if (!thread->has_exit) completion_unlink(thread->completion);
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

/* Used only to walk the process's non-owning membership list. */
KTHREAD **kthread_next_link(KTHREAD *thread) { return &thread->process_next; }

#if CONF_WITH_PTHJOIN
/* Serialized cooperative lifecycle operations. Registry membership owns only
 * small completion metadata, never PROCESS/ASPACE/resources or a user PD. */
static void completion_unlink(struct thread_completion *record)
{
    struct thread_completion **link;
    if (!record->linked) return;
    for (link = &completions; *link != record; link = &(*link)->next) {
        if (!*link) panic("lost TID completion");
    }
    *link = record->next;
    record->next = NULL; record->linked = FALSE; record->process = NULL;
    completion_put(record);
}
void kthread_forget_completions(KPROC *process)
{
    struct thread_completion *record = completions, *next;
    while (record) {
        next = record->next;
        if (record->process == process) completion_unlink(record);
        record = next;
    }
}
LONG kthread_join_begin(KTHREAD *caller, ULONG tid)
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
    record->refs++;
    record->claimant = caller;
    caller->joining = record;
    return E_OK;
}
BOOL kthread_join_ready(void *resource)
{
    KTHREAD *caller = resource;
    return caller->joining && caller->joining->ready;
}
BOOL kthread_join_status(KTHREAD *caller, UWORD *status)
{
    if (!caller || !caller->joining || !caller->joining->ready ||
        !caller->joining->has_exit) return FALSE;
    *status = caller->joining->status;
    return TRUE;
}
void kthread_join_cancel(KTHREAD *caller)
{
    struct thread_completion *record;
    if (!caller || !(record = caller->joining)) return;
    if (record->claimant != caller) panic("TID join claim owner changed");
    caller->joining = NULL; record->claimant = NULL;
    completion_put(record);
}
void kthread_join_consume(KTHREAD *caller)
{
    struct thread_completion *record = caller->joining;
    if (!record || record->claimant != caller || !record->ready || !record->has_exit)
        panic("invalid TID completion consume");
    /* Called after successful copyout, with no allocation/callback/yield. */
    completion_unlink(record);
    kthread_join_cancel(caller);
}
#endif
