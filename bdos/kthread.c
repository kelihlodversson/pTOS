/* Generic execution identity; dispatch remains in the existing scheduler. */
#include "emutos.h"
#include "biosext.h"
#include "kheap.h"
#include "kproc.h"
#include "kaspace.h"
#include "kthread.h"
#include "fs.h"
#include "proc.h"

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
};
enum { THREAD_NEW, THREAD_EXECUTING, THREAD_STOPPED, THREAD_REAPING };

/* Never reuse a TID during a boot. Exhaustion fails closed. */
static ULONG last_tid;
#if CONF_WITH_X86_64_MEMTEST
static UWORD fail_attach_after;
void kthread_test_fail_after_attach(void) { fail_attach_after = 1; }
void kthread_test_fail_attach_after(UWORD count) { fail_attach_after = count; }
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
    thread->stack_hold = NULL;
    thread->execution = NULL;
    thread->reclaim_execution = NULL;
    thread->state = THREAD_NEW;
    thread->identity_only = identity_only;
    if (!kproc_thread_attach_space(process, space, thread, &thread->process,
                                   &thread->space, &thread->process_next)) {
        kfree(thread);
        return NULL;
    }
#if CONF_WITH_X86_64_MEMTEST
    if (fail_attach_after && !--fail_attach_after) {
        kproc_thread_detach(thread->process, thread, thread->process_next);
        kaspace_put(thread->space);
        kproc_release(thread->process);
        kfree(thread);
        return NULL;
    }
#endif
    thread->attached = TRUE;
    thread->tid = ++last_tid;
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

BOOL kthread_start(KTHREAD *thread)
{
    if (!thread || thread->identity_only || thread->state != THREAD_NEW || !thread->attached ||
        !thread->reclaim_execution || !kproc_thread_start_allowed(thread->process) ||
        kproc_active_space(thread->process) != thread->space)
        return FALSE;
    thread->state = THREAD_EXECUTING;
    return TRUE;
}

BOOL kthread_is_executing(const KTHREAD *thread)
{
    return thread && thread->state == THREAD_EXECUTING;
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
    kfree(thread);
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
