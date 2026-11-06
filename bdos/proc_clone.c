/* Trusted construction; public Pclone and SAME_PROCESS startup remain gated. */
#include "emutos.h"
#include "gemerror.h"
#include "biosext.h"
#include "kheap.h"
#include "kproc.h"
#include "kaspace.h"
#include "procmem.h"
#include "kthread.h"
#include "proc_clone.h"
#if CONF_WITH_NATIVE_SCHED
#include "ksched.h"
extern PD *run;
#endif

struct proc_clone_transaction {
    KPROC *source, *process; /* owning identity references */
    KASPACE *space;          /* owning until target adopts it or rollback */
    KTHREAD *thread;         /* owns execution-resource retention, not runnable */
    PROC_CLONE_PROCESS_POLICY policy;
    BOOL owns_process;
    THREAD_STARTUP_DRAFT *startup;
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
    thread_startup_abort(&tx->startup);
#if CONF_WITH_NATIVE_SCHED
    kproc_clone_drop_compat(tx->process);
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
    space = kproc_active_space(args->source);
    if (!space || (target && target != args->source && !kproc_clone_target_ready(target, NULL)))
        return EACCDN;
    tx = kalloc(sizeof(*tx));
    if (!tx) return ENSMEM;
    tx->source = tx->process = NULL;
    tx->space = NULL;
    tx->thread = NULL;
    tx->startup = NULL;
    tx->policy = args->process_policy;
    tx->owns_process = FALSE;
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
    }
    tx->thread = NULL;
    *transaction = NULL; /* no callback can abort this transaction twice */
    release_transaction(tx);
    *result = ready; /* kernel-owned result only; never a user child-TID write */
    return E_OK;
}

LONG proc_clone_prepare_startup(PROC_CLONE_TRANSACTION *tx, const THREAD_STARTUP_ARGS *args)
{
    if (!tx || !args || tx->startup) return EINVAL;
    if (kproc_active_space(tx->source) != tx->space ||
        (tx->policy != PROC_CLONE_SAME_PROCESS && !kproc_clone_target_ready(tx->process, tx->thread)))
        return EACCDN;
    return thread_startup_prepare(tx->space, args, &tx->startup);
}

static BOOL startup_target_live(const PROC_CLONE_TRANSACTION *tx)
{
    return tx && tx->startup && kproc_active_space(tx->source) == tx->space &&
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
    error = ksched_clone_reserve(&slot);
    if (error) return error;
    if (!kproc_clone_prepare_compat(tx->process, tx->thread, tx->space)) { error = ENSMEM; goto failed; }
    if (!kproc_clone_prepare_parent(tx->process, tx->source)) { error = ENSMEM; goto failed; }
    error = thread_startup_run_preflight(startup);
    if (error) goto failed;
    if (!kproc_clone_run_ready(tx->process, tx->thread, tx->space) ||
        kproc_active_space(tx->source) != tx->space) { error = EACCDN; goto failed; }
    error = thread_startup_seal(startup);
    if (error) goto failed;
#if CONF_WITH_X86_64_MEMTEST
    if (cancel_publication) {
        cancel_publication = FALSE;
        ksched_clone_cancel(&slot);
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
    kproc_clone_drop_compat(tx->process);
    ksched_clone_cancel(&slot);
    return error;
}
#endif

#if CONF_WITH_NATIVE_SCHED
/* Invoked directly by the existing scheduler root, never as an observer or
 * user callback. Every fallible preflight and the root setjmp already passed. */
void proc_clone_commit_prepared(PROC_CLONE_TRANSACTION *tx, void *slot)
{
    THREAD_STARTUP_STACK_OWNERS owners = { { 0, 0 }, { 0, 0 } };
    WORD pid = kproc_ref_pid(tx->process);
    ULONG tid = kthread_tid(tx->thread);
    THREAD_STARTUP_DRAFT *startup = tx->startup;
    if (!thread_startup_take_stacks(startup, &owners)) panic("clone publication: missing prepared stacks");
    kproc_clone_commit_run(tx->process, tx->space, startup, owners.syscall.physical, owners.syscall.top);
    tx->space = NULL;
    owners.syscall.physical = owners.syscall.top = 0;
    ksched_clone_commit(slot, tx->process, tx->thread, startup, owners.scheduler.physical, owners.scheduler.top);
    owners.scheduler.physical = owners.scheduler.top = 0;
    tx->process = NULL; tx->thread = NULL; tx->startup = NULL; tx->owns_process = FALSE;
    /* First externally visible store starts here. Only no-fail stores and
     * visibility barriers follow; the bound slot is still TASK_UNUSED. */
    thread_startup_commit_stores(startup, pid, tid);
    thread_startup_publish(startup);
    ksched_clone_publish(slot);
}
#endif

KPROC *proc_clone_process(const PROC_CLONE_TRANSACTION *tx)
{
    return tx ? tx->process : NULL;
}
