/* Trusted construction; runnable SAME_PROCESS startup remains gated. */
#include "emutos.h"
#include "gemerror.h"
#include "biosext.h"
#include "kheap.h"
#include "kproc.h"
#include "kaspace.h"
#include "procmem.h"
#include "kthread.h"
#include "proc_clone.h"
#include "process_observer.h"
#if CONF_WITH_NATIVE_SCHED
#include "ksched.h"
#include "ksched_lifetime.h"
extern PD *run;
#endif

struct proc_clone_observer {
    ULONG references;
    KPROC *process; /* identity only: never retains descriptors or a PD */
    WORD pid;
    ULONG tid;
    KTHREAD *thread; /* borrowed until root detach; no reference cycle */
    BOOL same_process, thread_completed;
    UWORD thread_status;
    PROC_CLONE_OBSERVER_STATE state;
};
struct proc_clone_transaction {
    KPROC *source, *process; /* owning identity references */
    KASPACE *space;          /* owning until target adopts it or rollback */
    KTHREAD *thread;         /* owns execution-resource retention, not runnable */
    PROC_CLONE_PROCESS_POLICY policy;
    BOOL owns_process;
    BOOL owns_compat; /* only NEW_PROCESS preparation, never source resources */
    THREAD_STARTUP_DRAFT *startup;
    PROC_CLONE_OBSERVER *observer; /* borrowed from owned startup registration */
    BOOL asynchronous;
};

#if CONF_WITH_X86_64_MEMTEST
static UWORD fail_stage;
static BOOL cancel_publication;
void proc_clone_test_cancel_publication(void) { cancel_publication = TRUE; }
void proc_clone_test_fail_after(UWORD stage) { fail_stage = stage; }
static BOOL injected(UWORD stage)
{
    if (stage != fail_stage) return FALSE;
    fail_stage = 0;
    return TRUE;
}
#else
#define injected(stage) FALSE
#endif

static void release_transaction(PROC_CLONE_TRANSACTION *tx)
{
#if CONF_WITH_NATIVE_SCHED && CONF_WITH_X86_64_MEMTEST
    if (ksched_test_clone_masked()) panic("clone teardown in publication exclusion");
#endif
    thread_startup_abort(&tx->startup);
#if CONF_WITH_NATIVE_SCHED
    if (tx->owns_compat) kproc_clone_drop_compat(tx->process);
#endif
    if (tx->owns_process) proc_dispose_process(tx->process);
    kthread_destroy(tx->thread);
    kaspace_put(tx->space);
    kproc_release(tx->process);
    kproc_release(tx->source);
    kfree(tx);
}

LONG proc_clone(const PROC_CLONE_ARGS *args, PROC_CLONE_TRANSACTION **transaction)
{
    PROC_CLONE_TRANSACTION *tx;
    KPROC *target;
    KASPACE *space;
    LONG error = ENSMEM;

    if (!args || !transaction || *transaction || !args->source ||
        (args->process_policy != PROC_CLONE_SAME_PROCESS &&
         args->process_policy != PROC_CLONE_PREPARED_PROCESS &&
         args->process_policy != PROC_CLONE_NEW_PROCESS) ||
        (args->vm_policy != PROC_CLONE_SHARE_VM && args->vm_policy != PROC_CLONE_PRIVATE_VM))
        return EINVAL;
    if (args->process_policy != PROC_CLONE_NEW_PROCESS && args->resources)
        return EINVAL;
    if (args->process_policy == PROC_CLONE_SAME_PROCESS) {
        if (args->prepared_process) return EINVAL;
        target = args->source;
    } else if (args->process_policy == PROC_CLONE_NEW_PROCESS) {
        if (args->prepared_process || (args->resources & ~PROC_CLONE_RESOURCE_MASK)) return EINVAL;
        target = NULL;
    } else {
        target = args->prepared_process;
        if (!target || target == args->source) return EINVAL;
    }
    if (args->vm_policy == PROC_CLONE_PRIVATE_VM)
        return args->process_policy == PROC_CLONE_SAME_PROCESS ? EINVAL : EINVFN;
#if CONF_WITH_PTERMGRP
    if (kproc_group_status(args->source, NULL)) return EACCDN;
#endif
    space = kproc_active_space(args->source);
    if (!space || (target && target != args->source && !kproc_clone_target_ready(target, NULL)))
        return EACCDN;
    tx = kalloc(sizeof(*tx));
    if (!tx) return ENSMEM;
    tx->source = tx->process = NULL;
    tx->space = NULL;
    tx->thread = NULL;
    tx->startup = NULL;
    tx->observer = NULL; tx->asynchronous = FALSE;
    tx->policy = args->process_policy;
    tx->owns_process = FALSE;
    tx->owns_compat = FALSE;
    tx->source = kproc_retain_live(args->source);
    if (!tx->source) { error = EACCDN; goto failed; }
    if (injected(1)) goto failed;
    if (tx->policy == PROC_CLONE_NEW_PROCESS) {
        error = proc_create_clone_process(tx->source, args->resources, &tx->process);
        if (error != E_OK) goto failed;
        tx->owns_process = TRUE;
        error = ENSMEM;
    } else {
        tx->process = kproc_retain_live(target);
        if (!tx->process) { error = EACCDN; goto failed; }
    }
    if (injected(2)) goto failed;
    tx->space = kaspace_share(space);
    if (!tx->space || injected(3)) goto failed;
    tx->thread = kthread_construct_identity(tx->process, space);
    if (!tx->thread || injected(4)) goto failed;
    *transaction = tx; /* the only publication during prepare */
    return E_OK;
failed:
    release_transaction(tx);
    return error;
}

LONG proc_clone_commit(PROC_CLONE_TRANSACTION **transaction, PROC_CLONE_RESULT *result)
{
    PROC_CLONE_TRANSACTION *tx;
    PROC_CLONE_RESULT ready;
    if (!transaction || !*transaction || !result || result->thread ||
        result->owned_process || result->pid || result->tid) return EINVAL;
    tx = *transaction;
#if CONF_WITH_PTERMGRP
    if (kproc_group_status(tx->source, NULL) || kproc_group_status(tx->process, NULL))
        return EACCDN;
#endif
    if (kproc_active_space(tx->source) != tx->space)
        return EACCDN;
    /* Identity-only commit cannot publish a startup image. The controlled
     * root runner is a separate NEW_PROCESS-only transaction boundary. */
    if (tx->startup) return thread_startup_publication_check(tx->startup);
    if (tx->policy != PROC_CLONE_SAME_PROCESS) {
        if (!kproc_clone_adopt_space(tx->process, tx->thread, tx->space))
            return EACCDN;
        tx->space = NULL; /* moved, not another reference */
    }
    ready.thread = tx->thread;
    ready.pid = kproc_ref_pid(tx->process);
    ready.tid = kthread_tid(tx->thread);
    ready.owned_process = tx->owns_process ? tx->process : NULL;
    if (tx->owns_process) {
        tx->process = NULL; /* transfer retained identity/disposal to result */
        tx->owns_process = FALSE;
        tx->owns_compat = FALSE;
    }
    tx->thread = NULL;
    *transaction = NULL; /* no callback can abort this transaction twice */
    release_transaction(tx);
    *result = ready; /* kernel-owned result only; never a user child-TID write */
    return E_OK;
}

static void observer_put(PROC_CLONE_OBSERVER *observer)
{
    if (!observer || !observer->references) panic("clone observer reference underflow");
    if (--observer->references) return;
    process_observer_release(&observer->process); kfree(observer);
}
static LONG observer_acquire(void *context)
{
    PROC_CLONE_OBSERVER *observer = context;
    if (observer->references == ~(ULONG)0) return EAGAIN;
    observer->references++; return E_OK;
}
static void observer_detach(void *context)
{
    PROC_CLONE_OBSERVER *observer = context;
    if (observer->state == PROC_CLONE_OBSERVER_PREPARED)
        observer->state = PROC_CLONE_OBSERVER_CANCELLED;
    else if (observer->state == PROC_CLONE_OBSERVER_PUBLISHED)
        observer->state = PROC_CLONE_OBSERVER_REAPED;
    else panic("clone observer detached twice");
    if (observer->thread) {
        observer->thread_completed = kthread_exit_status(observer->thread, &observer->thread_status);
        observer->thread = NULL;
    }
    observer_put(observer);
}
LONG proc_clone_observe_async(PROC_CLONE_TRANSACTION *tx, PROC_CLONE_OBSERVER **out)
{
    static const THREAD_STARTUP_OBSERVER ops = { observer_acquire, observer_detach };
    PROC_CLONE_OBSERVER *observer;
    LONG error;
    if (!tx || !out || *out || tx->observer || !tx->startup) return EINVAL;
    observer = kalloc(sizeof(*observer));
    if (!observer) return ENSMEM;
    observer->references = 1;
    observer->same_process = tx->policy == PROC_CLONE_SAME_PROCESS;
    observer->thread = observer->same_process ? tx->thread : NULL;
    observer->thread_completed = FALSE;
    observer->thread_status = 0;
    observer->process = NULL;
    error = process_observer_attach(tx->process, &observer->process);
    if (error) { kfree(observer); return error; }
    if (injected(5)) { observer_put(observer); return ENSMEM; }
    observer->pid = kproc_ref_pid(tx->process); observer->tid = kthread_tid(tx->thread);
    observer->state = PROC_CLONE_OBSERVER_PREPARED;
    error = proc_clone_observe_startup(tx, &ops, observer);
    if (error) { observer_put(observer); return error; }
    tx->observer = observer; *out = observer;
    return E_OK;
}
void proc_clone_observer_release(PROC_CLONE_OBSERVER **handle)
{
    PROC_CLONE_OBSERVER *observer;
    if (!handle || !*handle) return;
    observer = *handle; *handle = NULL; observer_put(observer);
}
PROC_CLONE_OBSERVER_STATE proc_clone_observer_state(const PROC_CLONE_OBSERVER *observer)
{ return observer->state; }
BOOL proc_clone_observer_process_snapshot(const PROC_CLONE_OBSERVER *observer, PROCESS_COMPLETION *out)
{
    if (!observer || observer->same_process ||
        (observer->state != PROC_CLONE_OBSERVER_PUBLISHED && observer->state != PROC_CLONE_OBSERVER_REAPED))
        return FALSE;
    return process_observer_snapshot(observer->process, out);
}
BOOL proc_clone_observer_result(const PROC_CLONE_OBSERVER *observer, PROC_CLONE_COMPLETION *out)
{
    UWORD status;
    if (!observer || !out ||
        (observer->state != PROC_CLONE_OBSERVER_PUBLISHED && observer->state != PROC_CLONE_OBSERVER_REAPED)) return FALSE;
    if (observer->same_process) {
        if (observer->thread) {
            if (!kthread_exit_status(observer->thread, &status)) return FALSE;
        } else {
            if (!observer->thread_completed) return FALSE;
            status = observer->thread_status;
        }
    } else if (!kproc_ref_status(observer->process, &status)) return FALSE;
    out->pid = observer->pid; out->tid = observer->tid; out->status = status;
    return TRUE; /* snapshot of existing completion, not another wait/reaper */
}

LONG proc_clone_prepare_startup(PROC_CLONE_TRANSACTION *tx, const THREAD_STARTUP_ARGS *args)
{
    if (!tx || !args || tx->startup) return EINVAL;
    if (
#if CONF_WITH_PTERMGRP
        kproc_group_status(tx->source, NULL) || kproc_group_status(tx->process, NULL) ||
#endif
        kproc_active_space(tx->source) != tx->space ||
        (tx->policy != PROC_CLONE_SAME_PROCESS && !kproc_clone_target_ready(tx->process, tx->thread)))
        return EACCDN;
    return thread_startup_prepare(tx->space, args, &tx->startup);
}

static BOOL startup_target_live(const PROC_CLONE_TRANSACTION *tx)
{
    return tx && tx->startup &&
#if CONF_WITH_PTERMGRP
        !kproc_group_status(tx->source, NULL) && !kproc_group_status(tx->process, NULL) &&
#endif
        kproc_active_space(tx->source) == tx->space &&
        (tx->policy == PROC_CLONE_SAME_PROCESS || kproc_clone_target_ready(tx->process, tx->thread));
}

LONG proc_clone_prepare_outputs(PROC_CLONE_TRANSACTION *tx, VADDR pid, VADDR tid)
{
    if (!startup_target_live(tx)) return EACCDN;
    return thread_startup_prepare_outputs(tx->startup, pid, tid);
}

LONG proc_clone_observe_startup(PROC_CLONE_TRANSACTION *tx,
                                 const THREAD_STARTUP_OBSERVER *observer, void *context)
{
    if (!startup_target_live(tx)) return EACCDN;
    return thread_startup_observe(tx->startup, observer, context);
}

LONG proc_clone_seal_startup(PROC_CLONE_TRANSACTION *tx)
{
    if (!startup_target_live(tx)) return EACCDN;
    return thread_startup_seal(tx->startup);
}

LONG proc_clone_retain_startup_stack(PROC_CLONE_TRANSACTION *tx)
{
    KASPACE_LEASE *hold = NULL;
    LONG error;
    if (!startup_target_live(tx)) return EACCDN;
    error = thread_startup_retain_stack(tx->startup, &hold);
    if (error) return error;
    if (!kthread_take_stack_hold(tx->thread, &hold)) {
        kaspace_lease_release(&hold); return EACCDN;
    }
    return E_OK;
}

const THREAD_STARTUP_DRAFT *proc_clone_startup(const PROC_CLONE_TRANSACTION *tx)
{
    return tx ? tx->startup : NULL;
}

void proc_clone_abort(PROC_CLONE_TRANSACTION **transaction)
{
    PROC_CLONE_TRANSACTION *tx;
    if (!transaction || !*transaction) return;
    tx = *transaction;
    *transaction = NULL;
    release_transaction(tx);
}

void proc_clone_release(PROC_CLONE_RESULT *result)
{
    KTHREAD *thread;
    KPROC *process;
    if (!result) return;
    thread = result->thread;
    process = result->owned_process;
    result->thread = NULL;
    result->owned_process = NULL;
    result->pid = 0;
    result->tid = 0;
    if (process) proc_dispose_process(process);
    kthread_destroy(thread);
    kproc_release(process);
}

#if CONF_WITH_NATIVE_SCHED
/* Public ABI wrapper receives normalized kernel arguments, never the user
 * structure. The observer caller reference is dropped before returning; the
 * startup registration owns the asynchronous lifetime through root reclaim. */
/* Materialize validated KASPACE reservations before acquiring startup leases.
 * Unlike ordinary user-copy demand handling, construction failure returns an
 * error rather than terminating the caller on allocation/I/O failure. */
static LONG public_back_range(KASPACE *space, VADDR address, ULONG bytes, UWORD access)
{
    VADDR page, last;
    ASPACE *as = kaspace_backend(space);
    if (!bytes) return E_OK;
    if (address >= USER_VA_LIMIT || bytes > USER_VA_LIMIT - address) return EIMBA;
    last = (address + bytes - 1) & ~(VADDR)(USER_PAGE_SIZE - 1);
    for (page = address & ~(VADDR)(USER_PAGE_SIZE - 1); ; page += USER_PAGE_SIZE) {
        UWORD prot;
        if (!aspace_translate(as,page,NULL,&prot) || (prot & access) != access) {
            int backed = kaspace_heap_demand_page(space,page,access);
#if CONF_WITH_MMAP
            if (backed == -1) backed = kaspace_mmap_demand_page(space,page,access);
#endif
            if (backed == 2) return ENSMEM;
            if (backed != 1 || !aspace_translate(as,page,NULL,&prot) || (prot & access) != access)
                return EIMBA;
        }
        if (page == last) return E_OK;
    }
}

LONG proc_clone_start_public(const PROC_CLONE_ARGS *args, const THREAD_STARTUP_ARGS *startup, VADDR pid, VADDR tid)
{
    PROC_CLONE_TRANSACTION *tx = NULL;
    PROC_CLONE_OBSERVER *observer = NULL;
    PROC_CLONE_IDENTITIES created = { 0, 0 };
    PD *controller;
    ULONG identity;
    LONG error;
    if (!args || !startup || (args->process_policy != PROC_CLONE_NEW_PROCESS &&
        args->process_policy != PROC_CLONE_SAME_PROCESS) || args->vm_policy != PROC_CLONE_SHARE_VM)
        return EINVAL;
    if (!ksched_clone_context(args->source,&controller)) return EINVFN;
    error = proc_clone(args,&tx);
    if (error) return error;
    identity = kthread_tid(tx->thread);
    if (!identity || identity > 0x7fffffffUL) { error = EAGAIN; goto done; }
    error = public_back_range(tx->space,startup->entry,1,ASPACE_PROT_USER | ASPACE_PROT_EXEC);
    if (!error) error = public_back_range(tx->space,startup->stack_base,startup->stack_size,ASPACE_PROT_USER | ASPACE_PROT_WRITE);
    if (!error && pid) error = public_back_range(tx->space,pid,4,ASPACE_PROT_USER | ASPACE_PROT_WRITE);
    if (!error && tid) error = public_back_range(tx->space,tid,4,ASPACE_PROT_USER | ASPACE_PROT_WRITE);
    if (!error) error = thread_startup_prepare_public(tx->space,startup,&tx->startup);
    if (!error) error = proc_clone_retain_startup_stack(tx);
    if (!error) error = proc_clone_prepare_outputs(tx,pid,tid);
    if (!error) error = proc_clone_observe_async(tx,&observer);
    if (!error) error = proc_clone_spawn(&tx,&created);
done:
    proc_clone_abort(&tx);
    proc_clone_observer_release(&observer);
    return error ? error : (LONG)identity;
}

LONG proc_clone_spawn(PROC_CLONE_TRANSACTION **handle, PROC_CLONE_IDENTITIES *created)
{
    PROC_CLONE_TRANSACTION *tx;
    KSCHED_PUBLICATION_GUARD guard;
    PD *controller;
    void *slot = NULL;
    LONG error;
    WORD pid;
    ULONG tid;
    if (!handle || !*handle || !created || created->pid || created->tid) return EINVAL;
    tx = *handle;
    if (kproc_live_pd(tx->source) != run || !aspace_is_current(kaspace_backend(tx->space))) return EINVFN;
    if (((tx->policy != PROC_CLONE_NEW_PROCESS || !tx->owns_process) &&
         tx->policy != PROC_CLONE_SAME_PROCESS) || !tx->startup ||
        !tx->observer || tx->observer->state != PROC_CLONE_OBSERVER_PREPARED) return EINVFN;
    if (!startup_target_live(tx) || !kthread_clone_run_ready(tx->thread)) return EACCDN;
    if (thread_startup_state(tx->startup) != THREAD_STARTUP_OBSERVED) return EINVAL;
    if (!ksched_clone_context(tx->source, &controller)) return EINVFN;
    error = ksched_task_reserve(FALSE, &slot);
    if (error) return error;
    if (tx->policy == PROC_CLONE_SAME_PROCESS) {
        if (tx->process != tx->source || !kproc_clone_sibling_ready(tx->process, tx->thread, tx->space) ||
            kproc_get_parent(run) != controller) { error = EACCDN; goto failed; }
    } else {
        if (!kproc_clone_prepare_compat(tx->process, tx->thread, tx->space)) { error = ENSMEM; goto failed; }
        tx->owns_compat = TRUE;
        if (!kproc_clone_prepare_controller(tx->process, controller) ||
            !kproc_clone_prepare_parent(tx->process, tx->source)) { error = ENSMEM; goto failed; }
    }
    error = thread_startup_run_preflight(tx->startup);
    if (error) goto failed;
    if (!(tx->policy == PROC_CLONE_SAME_PROCESS ?
          kproc_clone_sibling_ready(tx->process, tx->thread, tx->space) :
          kproc_clone_run_ready(tx->process, tx->thread, tx->space)) ||
        kproc_active_space(tx->source) != tx->space) { error = EACCDN; goto failed; }
    /* Only no-allocation/no-callback revalidation, sealing and stores occur
     * under backend exclusion. Cleanup always restores the entry IRQ state. */
    {
        PD *confirmed;
        if (!ksched_clone_begin(tx->source, &confirmed, &guard)) { error = EINVFN; goto failed; }
        if (confirmed != controller) { ksched_clone_end(&guard); error = EACCDN; goto failed; }
    }
    error = thread_startup_seal(tx->startup);
    if (error) { ksched_clone_end(&guard); goto failed; }
#if CONF_WITH_X86_64_MEMTEST
    if (cancel_publication) {
        cancel_publication = FALSE;
        ksched_clone_end(&guard);
        ksched_task_cancel(&slot); proc_clone_abort(handle);
        return EAGAIN;
    }
#endif
    pid = kproc_ref_pid(tx->process); tid = kthread_tid(tx->thread);
    tx->asynchronous = TRUE;
    *handle = NULL;
    proc_clone_commit_prepared(tx, slot); /* consumes reservation; no dispatch yet */
    created->pid = pid; created->tid = tid;
    ksched_clone_end(&guard);
    release_transaction(tx); /* outside publication exclusion */
    return E_OK;
failed:
    if (tx->owns_compat) { kproc_clone_drop_compat(tx->process); tx->owns_compat = FALSE; }
    ksched_task_cancel(&slot);
    return error;
}

LONG proc_clone_exec(PROC_CLONE_TRANSACTION **handle, PROC_CLONE_COMPLETION *completion)
{
    PROC_CLONE_TRANSACTION *tx;
    THREAD_STARTUP_DRAFT *startup;
    void *slot = NULL;
    LONG error, status;
    WORD pid;
    ULONG tid;
    if (!handle || !*handle || !completion || completion->pid || completion->tid || completion->status) return EINVAL;
    tx = *handle; startup = tx->startup;
    if (kproc_live_pd(tx->source) != run || !aspace_is_current(kaspace_backend(tx->space))) return EINVFN;
    if (tx->policy != PROC_CLONE_NEW_PROCESS || !tx->owns_process || !startup) return EINVFN;
    if (!startup_target_live(tx) || !kproc_clone_parent_ready(tx->source) ||
        !kthread_clone_run_ready(tx->thread)) return EACCDN;
    if (thread_startup_state(startup) != THREAD_STARTUP_OBSERVED) return EINVAL;
    error = ksched_task_reserve(TRUE, &slot);
    if (error) return error;
    if (!kproc_clone_prepare_compat(tx->process, tx->thread, tx->space)) { error = ENSMEM; goto failed; }
    tx->owns_compat = TRUE;
    if (!kproc_clone_prepare_parent(tx->process, tx->source) ||
        !kproc_clone_prepare_controller(tx->process,ksched_controller_pd())) { error = ENSMEM; goto failed; }
    error = thread_startup_run_preflight(startup);
    if (error) goto failed;
    if (!(tx->policy == PROC_CLONE_SAME_PROCESS ?
          kproc_clone_sibling_ready(tx->process, tx->thread, tx->space) :
          kproc_clone_run_ready(tx->process, tx->thread, tx->space)) ||
        kproc_active_space(tx->source) != tx->space) { error = EACCDN; goto failed; }
    error = thread_startup_seal(startup);
    if (error) goto failed;
#if CONF_WITH_X86_64_MEMTEST
    if (cancel_publication) {
        cancel_publication = FALSE;
        ksched_task_cancel(&slot);
        proc_clone_abort(handle); /* explicit cancellation consumes all ownership */
        return EAGAIN;
    }
#endif
    /* The existing dispatcher establishes its root continuation before the
     * no-fail commit. The canonical handle cannot be reentered by observers. */
    pid = kproc_ref_pid(tx->process); tid = kthread_tid(tx->thread);
    *handle = NULL;
    error = ksched_clone_dispatch(&slot, tx, &status);
    completion->pid = pid; completion->tid = tid; completion->status = status;
    release_transaction(tx);
    return error;
failed:
    if (tx->owns_compat) { kproc_clone_drop_compat(tx->process); tx->owns_compat = FALSE; }
    ksched_task_cancel(&slot);
    return error;
}
#endif

#if CONF_WITH_NATIVE_SCHED
/* Invoked by scheduler root or the serialized active creator, never an observer
 * or user callback. All fallible preparation precedes this no-fail transfer. */
void proc_clone_commit_prepared(PROC_CLONE_TRANSACTION *tx, void *slot)
{
    THREAD_STARTUP_STACK_OWNERS owners = { { 0, 0 }, { 0, 0 } };
    WORD pid = kproc_ref_pid(tx->process);
    ULONG tid = kthread_tid(tx->thread);
    THREAD_STARTUP_DRAFT *startup = tx->startup;
    if (!thread_startup_take_stacks(startup, &owners)) panic("clone publication: missing prepared stacks");
    if (tx->policy == PROC_CLONE_SAME_PROCESS) {
        kproc_clone_commit_sibling(tx->process, tx->thread);
    } else {
        kproc_clone_commit_run(tx->process, tx->space, startup, owners.syscall.physical, owners.syscall.top, tx->asynchronous);
        tx->space = NULL;
        tx->owns_compat = FALSE; /* live PROCESS now owns its compatibility maps */
        owners.syscall.physical = owners.syscall.top = 0;
    }
    ksched_clone_commit(slot, tx->process, tx->thread, startup, &owners.scheduler,
                         tx->asynchronous ? tx->source : NULL,
                         tx->policy == PROC_CLONE_SAME_PROCESS, &owners.syscall);
    if (tx->observer) tx->observer->state = PROC_CLONE_OBSERVER_PUBLISHED;
    tx->process = NULL; tx->thread = NULL; tx->startup = NULL; tx->owns_process = FALSE;
    /* First externally visible store starts here. Only no-fail stores and
     * visibility barriers follow; the bound slot is still TASK_RESERVED. */
    thread_startup_commit_stores(startup, pid, tid);
    thread_startup_publish(startup);
    ksched_task_publish(&slot);
}
#endif

KPROC *proc_clone_process(const PROC_CLONE_TRANSACTION *tx)
{
    return tx ? tx->process : NULL;
}
