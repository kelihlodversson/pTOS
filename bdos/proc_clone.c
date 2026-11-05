/* Construction transaction only: startup/publication remain disabled. */
#include "emutos.h"
#include "gemerror.h"
#include "kheap.h"
#include "kproc.h"
#include "kaspace.h"
#include "kthread.h"
#include "proc_clone.h"

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
    /* A draft is not a mapping lease, safe exit bridge or runnable token.
     * Do not transfer ownership/publish outputs until all gates are supplied. */
    if (tx->startup) return EINVFN;
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
