/* Trusted PROCESS/THREAD construction; public policy lives in clone.c. */
#include "emutos.h"
#include "gemerror.h"
#include "biosext.h"
#include "kheap.h"
#include "kproc.h"
#include "kaspace.h"
#include "procmem.h"
#include "kthread.h"
#include "proc_clone.h"
#include "syscall_return.h"
#include "process_observer.h"
#if CONF_WITH_FORK_CORE
#include "fork_parent.h"
#endif
#include "kproc_image_policy.h"
#if CONF_WITH_PROCESS_FILES
#include "process_files.h"
#endif
#if CONF_WITH_MULTITHREADING
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
    KASPACE *source_space;   /* separately retained while private VM prepares */
    KASPACE_MEMBER *mapping_members; /* transactional PROCESS mapping claims */
    KTHREAD *thread;         /* owns execution-resource retention, not runnable */
    PROC_CLONE_PROCESS_POLICY policy;
    BOOL owns_process;
    BOOL owns_compat; /* only NEW_PROCESS preparation, never source resources */
    THREAD_STARTUP_DRAFT *startup;
    PROC_CLONE_OBSERVER *observer; /* borrowed from owned startup registration */
    BOOL asynchronous;
    BOOL loaded_shared;
    BOOL owns_shared_exec; /* exact preparation token was transferred to this attempt */
#if CONF_WITH_FORK_CORE
    FORK_PARENT_DEPENDENCY *parent_dependency; /* borrowed from calling THREAD */
#endif
#if CONF_WITH_IMAGE_POLICY_TEST
    KPROC_IMAGE_PREPARATION *image_policy;
#endif
#if CONF_WITH_PROCESS_FILES
    KPROC_FILE_SNAPSHOT *files; /* admitted source plus reversible exact owners */
#endif
};

#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_PRIVATE_CLONE_TEST || CONF_WITH_PROCESS_FILES_TEST
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
#if CONF_WITH_FORK_CORE
    if (tx->parent_dependency) fork_parent_cancel(tx->parent_dependency,FORK_PARENT_ABORT);
#endif
#if CONF_WITH_MULTITHREADING && CONF_WITH_X86_64_MEMTEST
    if (ksched_test_clone_masked()) panic("clone teardown in publication exclusion");
#endif
#if CONF_WITH_IMAGE_POLICY_TEST
    kproc_image_policy_release(&tx->image_policy);
#endif
#if CONF_WITH_PROCESS_FILES
    kproc_files_snapshot_end(&tx->files);
#endif
    thread_startup_abort(&tx->startup);
#if CONF_WITH_MULTITHREADING
    if (tx->owns_compat) kproc_clone_drop_compat(tx->process);
    if(tx->owns_shared_exec) {
        kthread_destroy(tx->thread);tx->thread=NULL;
        if(tx->process) kproc_shared_exec_abort(tx->process);
    }
#endif
    /* Special execution holds and the permission journal are gone before
     * canonical claim release may retire mappings or call file cleanup. */
    kproc_abort_mapping_owners(tx->process,tx->space,&tx->mapping_members);
    if (tx->owns_process) proc_dispose_process(tx->process);
    kthread_destroy(tx->thread);
#if CONF_WITH_PRIVATE_CLONE
    {
        BOOL rollback = tx->space && tx->source_space && tx->space != tx->source_space;
        kaspace_put(tx->space);
        if (rollback) aspace_cow_collapse(kaspace_backend(tx->source_space));
    }
#else
    kaspace_put(tx->space);
#endif
    kaspace_put(tx->source_space);
    kproc_release(tx->process);
    kproc_release(tx->source);
    kfree(tx);
}

static LONG clone_prepare(const PROC_CLONE_ARGS *args, PROC_CLONE_TRANSACTION **transaction,BOOL loaded_shared)
{
    PROC_CLONE_TRANSACTION *tx;
    KPROC *target;
    KASPACE *space;
    LONG error = ENSMEM;

    if (!args || !transaction || *transaction || !args->source ||
        (args->process_policy != PROC_CLONE_SAME_PROCESS &&
         args->process_policy != PROC_CLONE_PREPARED_PROCESS &&
         args->process_policy != PROC_CLONE_NEW_PROCESS &&
         args->process_policy != PROC_CLONE_LOADED_PROCESS) ||
        (args->vm_policy != PROC_CLONE_SHARE_VM && args->vm_policy != PROC_CLONE_PRIVATE_VM &&
         args->vm_policy != PROC_CLONE_PREPARED_VM && args->vm_policy != PROC_CLONE_SCOPED_VM))
        return EINVAL;
    if (args->process_policy != PROC_CLONE_NEW_PROCESS && args->resources)
        return EINVAL;
    if ((args->process_policy == PROC_CLONE_LOADED_PROCESS) !=
        (args->vm_policy == PROC_CLONE_PREPARED_VM)) return EINVAL;
    if (args->vm_policy==PROC_CLONE_SCOPED_VM && args->process_policy!=PROC_CLONE_NEW_PROCESS) return EINVAL;
    if (args->process_policy == PROC_CLONE_LOADED_PROCESS) {
        if (args->prepared_process || !args->prepared_space ||
            !kproc_thread_start_allowed(args->source) ||
            !kproc_unlaunched(kproc_live_pd(args->source))) return EACCDN;
        target = args->source;
    } else if (args->process_policy == PROC_CLONE_SAME_PROCESS) {
        if (args->prepared_process) return EINVAL;
        target = args->source;
    } else if (args->process_policy == PROC_CLONE_NEW_PROCESS) {
        if ((args->prepared_process && !loaded_shared) || (args->resources & ~PROC_CLONE_RESOURCE_MASK)) return EINVAL;
#if CONF_WITH_PROCESS_FILES
        if ((args->resources & PROC_CLONE_COPY_DESCRIPTORS) &&
            ((args->resources & PROC_CLONE_COPY_FILES) || args->vm_policy != PROC_CLONE_SHARE_VM))
            return EINVAL;
#endif
        target = loaded_shared ? args->prepared_process : NULL;
#if CONF_WITH_MULTITHREADING
        if(loaded_shared && (!target || !kproc_shared_exec_constructible(target))) return EACCDN;
#else
        if(loaded_shared) return EINVFN;
#endif
    } else {
        target = args->prepared_process;
        if (!target || target == args->source) return EINVAL;
    }
    if (args->vm_policy == PROC_CLONE_PRIVATE_VM) {
        if (args->process_policy == PROC_CLONE_SAME_PROCESS) return EINVAL;
        if (args->process_policy != PROC_CLONE_NEW_PROCESS) return EINVFN;
#if CONF_WITH_PRIVATE_CLONE
        if (!kproc_clone_private_runtime(NULL, args->source)) return EACCDN;
#else
        return EINVFN;
#endif
    }
#if CONF_WITH_MULTITHREADING
    if (kproc_group_status(args->source, NULL)) return EACCDN;
#endif
    space = args->process_policy == PROC_CLONE_LOADED_PROCESS ? args->prepared_space :
        kproc_active_space(args->source);
    if (args->process_policy == PROC_CLONE_LOADED_PROCESS &&
        kproc_active_space(args->source) && kproc_active_space(args->source) != space) return EACCDN;
    if (!space || (target && target != args->source && !kproc_clone_target_ready(target, NULL)))
        return EACCDN;
    tx = kalloc(sizeof(*tx));
    if (!tx) return ENSMEM;
    tx->source = tx->process = NULL;
    tx->space = NULL;
    tx->source_space = NULL;
    tx->mapping_members = NULL;
    tx->thread = NULL;
    tx->startup = NULL;
#if CONF_WITH_IMAGE_POLICY_TEST
    tx->image_policy=NULL;
#endif
    tx->observer = NULL; tx->asynchronous = FALSE;
    tx->policy = args->process_policy;
    tx->owns_process = FALSE;
    tx->loaded_shared=loaded_shared;
    tx->owns_shared_exec=FALSE;
#if CONF_WITH_PROCESS_FILES
    tx->files=NULL;
#endif
    tx->owns_compat = FALSE;
#if CONF_WITH_FORK_CORE
    tx->parent_dependency=NULL;
#endif
    tx->source = kproc_retain_live(args->source);
    if (!tx->source) { error = EACCDN; goto failed; }
    tx->source_space = kaspace_share(space);
    if (!tx->source_space) goto failed;
    if ((args->process_policy != PROC_CLONE_LOADED_PROCESS && injected(1))) goto failed;
    if (tx->policy == PROC_CLONE_NEW_PROCESS && !loaded_shared) {
        UWORD resources=args->resources;
#if CONF_WITH_PROCESS_FILES
        UWORD file_policy = (resources & PROC_CLONE_COPY_FILES) ? PROCESS_FILES_ALL : PROCESS_FILES_DESCRIPTORS;
        if (resources & (PROC_CLONE_COPY_FILES | PROC_CLONE_COPY_DESCRIPTORS)) {
            error=kproc_files_snapshot_begin(tx->source,&tx->files);
            if (error) goto failed;
            resources &= ~(PROC_CLONE_COPY_FILES | PROC_CLONE_COPY_DESCRIPTORS | PROC_CLONE_COPY_STANDARD_HANDLES);
        }
#endif
        error = proc_create_clone_process(tx->source, resources, &tx->process);
        if (error != E_OK) goto failed;
        tx->owns_process = TRUE;
#if CONF_WITH_PROCESS_FILES
        if (tx->files) {
            if (file_policy == PROCESS_FILES_DESCRIPTORS) kproc_clone_clear_dta(tx->process);
            error=kproc_files_snapshot_prepare(tx->files,tx->process,file_policy);
            if (error) goto failed;
        }
#endif
        error = ENSMEM;
    } else {
        tx->process = kproc_retain_live(target);
        if (!tx->process) { error = EACCDN; goto failed; }
    }
    if ((args->process_policy != PROC_CLONE_LOADED_PROCESS && injected(2))) goto failed;
    if (args->vm_policy != PROC_CLONE_PRIVATE_VM) tx->space = kaspace_share(space);
#if CONF_WITH_PRIVATE_CLONE
    else {
        error = kaspace_clone(space, &tx->space);
        if (error) goto failed;
        error=kproc_clone_prepare_private_image(tx->process,tx->source);
        if (error) goto failed;
        error = ENSMEM;
    }
#endif
    if (!tx->space || (args->process_policy != PROC_CLONE_LOADED_PROCESS && injected(3))) goto failed;
    if (args->process_policy!=PROC_CLONE_SAME_PROCESS && args->process_policy!=PROC_CLONE_LOADED_PROCESS) {
#if CONF_WITH_MULTITHREADING
        if(loaded_shared) error=kproc_shared_exec_take_mapping_owners(tx->process,&tx->mapping_members);
        else
#endif
        error=
              args->vm_policy==PROC_CLONE_SCOPED_VM ?
              kproc_prepare_scoped_mapping_owners(tx->process,tx->source,tx->space,&tx->mapping_members) :
              kproc_clone_prepare_mapping_owners(tx->process,tx->source,tx->space,
                    args->vm_policy==PROC_CLONE_PRIVATE_VM,&tx->mapping_members);
        if (error) goto failed;
        if(loaded_shared) tx->owns_shared_exec=TRUE;
        error=ENSMEM;
    }
#if CONF_WITH_SHARED_VM_PEXEC || CONF_WITH_PRIVATE_CLONE
    if(!loaded_shared && args->process_policy==PROC_CLONE_NEW_PROCESS &&
        args->vm_policy!=PROC_CLONE_PRIVATE_VM) {
        error=kproc_shared_dependencies_prepare(tx->process,tx->source);
        if(error) goto failed;
        error=ENSMEM;
    }
#endif
    tx->thread = args->process_policy == PROC_CLONE_LOADED_PROCESS ?
        kthread_prepare(kproc_live_pd(tx->process), tx->space) :
        kthread_construct_identity(tx->process, tx->space);
    if (!tx->thread || (args->process_policy != PROC_CLONE_LOADED_PROCESS && injected(4))) goto failed;
    *transaction = tx; /* the only publication during prepare */
    return E_OK;
failed:
    release_transaction(tx);
    return error;
}

LONG proc_clone(const PROC_CLONE_ARGS *args,PROC_CLONE_TRANSACTION **transaction)
{ return clone_prepare(args,transaction,FALSE); }
#if CONF_WITH_MULTITHREADING
LONG proc_clone_loaded_shared(KPROC *source,KPROC *target,PROC_CLONE_TRANSACTION **transaction)
{
    PROC_CLONE_ARGS args={0};
    args.source=source;args.prepared_process=target;
    args.process_policy=PROC_CLONE_NEW_PROCESS;args.vm_policy=PROC_CLONE_SHARE_VM;
    return clone_prepare(&args,transaction,TRUE);
}
#endif

LONG proc_clone_commit(PROC_CLONE_TRANSACTION **transaction, PROC_CLONE_RESULT *result)
{
    PROC_CLONE_TRANSACTION *tx;
    PROC_CLONE_RESULT ready;
    if (!transaction || !*transaction || !result || result->thread ||
        result->owned_process || result->pid || result->tid) return EINVAL;
    tx = *transaction;
#if CONF_WITH_MULTITHREADING
    if (kproc_group_status(tx->source, NULL) || kproc_group_status(tx->process, NULL))
        return EACCDN;
#endif
    if (tx->policy == PROC_CLONE_LOADED_PROCESS) {
        KASPACE *active = kproc_active_space(tx->process);
        if (!kproc_thread_start_allowed(tx->process) ||
            !kproc_unlaunched(kproc_live_pd(tx->process)) || (active && active != tx->space) ||
            tx->startup || kthread_process(tx->thread) != tx->process ||
            kthread_aspace(tx->thread) != tx->space) return EACCDN;
    } else if (kproc_active_space(tx->source) != tx->source_space) return EACCDN;
#if CONF_WITH_PRIVATE_CLONE
    if (tx->space != tx->source_space &&
        !kproc_clone_private_runtime(NULL,tx->source)) return EACCDN;
#endif
    /* Identity-only commit cannot publish a startup image. The controlled
     * root runner is a separate NEW_PROCESS-only transaction boundary. */
    if (tx->startup) return thread_startup_publication_check(tx->startup);
#if CONF_WITH_PROCESS_FILES
    if (tx->files && kproc_files_snapshot_seal(tx->files)) return EACCDN;
#endif

    if (tx->policy != PROC_CLONE_SAME_PROCESS && tx->policy != PROC_CLONE_LOADED_PROCESS) {
        if (!kproc_clone_adopt_space(tx->process, tx->thread, tx->space))
            return EACCDN;
        kproc_clone_commit_mapping_owners(tx->process,tx->space,&tx->mapping_members);
        tx->space = NULL; /* moved, not another reference */
    }
#if CONF_WITH_PROCESS_FILES
    if (tx->files) kproc_files_snapshot_commit(tx->files,tx->process);
#endif
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

KTHREAD *proc_clone_prepare_loaded_thread(PD *pd, KASPACE *space)
{
    PROC_CLONE_ARGS args = { 0 };
    PROC_CLONE_TRANSACTION *tx = NULL;
    PROC_CLONE_RESULT result = PROC_CLONE_RESULT_INIT;
    KPROC *target = kproc_retain(pd);
    LONG error;
    if (!target) return NULL;
    args.source = target;
    args.process_policy = PROC_CLONE_LOADED_PROCESS;
    args.vm_policy = PROC_CLONE_PREPARED_VM;
    args.prepared_space = space;
    error = proc_clone(&args, &tx);
    if (!error) error = proc_clone_commit(&tx, &result);
    proc_clone_abort(&tx);
    kproc_release(target);
    return error ? NULL : result.thread;
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
#if CONF_WITH_MULTITHREADING
        kproc_group_status(tx->source, NULL) || kproc_group_status(tx->process, NULL) ||
#endif
        kproc_active_space(tx->source) != tx->source_space ||
        (tx->policy != PROC_CLONE_SAME_PROCESS && !kproc_clone_target_ready(tx->process, tx->thread)))
        return EACCDN;
#if CONF_WITH_PRIVATE_CLONE
    if (tx->space != tx->source_space)
        return thread_startup_prepare_cloned(tx->source_space, tx->space, args, FALSE, &tx->startup);
#endif
    return thread_startup_prepare(tx->space, args, &tx->startup);
}


static BOOL startup_target_live(const PROC_CLONE_TRANSACTION *tx)
{
    return tx && tx->startup &&
#if CONF_WITH_IMAGE_POLICY_TEST
        (!tx->image_policy || kproc_image_policy_validate(tx->image_policy)==E_OK) &&
#endif
#if CONF_WITH_PROCESS_FILES
        (!tx->files || kproc_files_snapshot_validate(tx->files)) &&
#endif
#if CONF_WITH_MULTITHREADING
        !kproc_group_status(tx->source, NULL) && !kproc_group_status(tx->process, NULL) &&
#endif
#if CONF_WITH_PRIVATE_CLONE
        (tx->space == tx->source_space || kproc_clone_private_runtime(NULL,tx->source)) &&
#endif
        kproc_active_space(tx->source) == tx->source_space &&
        (tx->policy == PROC_CLONE_SAME_PROCESS || kproc_clone_target_ready(tx->process, tx->thread));
}

LONG proc_clone_prepare_outputs(PROC_CLONE_TRANSACTION *tx, VADDR pid, VADDR tid)
{
    if (!startup_target_live(tx)) return EACCDN;
    /* Child-space output aliases cannot stand in for parent copyout. */
    if (tx->space != tx->source_space && (pid || tid)) return EINVFN;
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
#if CONF_WITH_SHARED_VM_PEXEC
LONG proc_clone_tpa_startup_stack(PROC_CLONE_TRANSACTION *tx)
{
    if (!startup_target_live(tx) || !tx->loaded_shared) return EACCDN;
    return kthread_use_tpa_stack(tx->thread) ? E_OK : EACCDN;
}
#endif

#if CONF_WITH_IMAGE_POLICY_TEST
LONG proc_clone_prepare_image_policy(PROC_CLONE_TRANSACTION *tx,const KPROC_IMAGE_POLICY *policy)
{
    if (!tx || tx->policy!=PROC_CLONE_NEW_PROCESS || !tx->owns_process ||
        tx->loaded_shared || tx->image_policy) return EINVAL;
    if (!startup_target_live(tx) || !kthread_clone_run_ready(tx->thread)) return EACCDN;
    return kproc_image_policy_prepare(tx->process,tx->thread,policy,&tx->image_policy);
}
#endif

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

#if CONF_WITH_MULTITHREADING
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
            int backed = -1;
#if CONF_WITH_PRIVATE_CLONE
            backed = aspace_cow_fault(as,page,access);
#endif
            if (backed == -1) backed = kaspace_heap_demand_page(space,page,access);
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

#if CONF_WITH_SAVED_RETURN
LONG proc_clone_prepare_saved_return(PROC_CLONE_TRANSACTION *tx, const SYSCALL_RETURN *saved)
{
    if (!tx || !saved || tx->startup || tx->policy != PROC_CLONE_NEW_PROCESS ||
        saved->space != tx->source_space || saved->owner != ksched_exit_thread(run) ||
        kthread_process(saved->owner) != tx->source ||
        kthread_tid(tx->thread) == kthread_tid(saved->owner) ||
        !kproc_thread_is_sole(tx->source, saved->owner)) return EACCDN;
#if CONF_WITH_PRIVATE_CLONE
    if (tx->space != tx->source_space)
        return thread_startup_prepare_cloned_return(tx->source_space, tx->space, saved, &tx->startup);
#endif
    return thread_startup_prepare_saved_return(tx->space, saved, &tx->startup);
}
#if CONF_WITH_FORK_RETURN_TEST || CONF_WITH_FORK
/* Timer return capture also selects SAVED_RETURN. It does not use the
 * syscall-return child construction helpers or enable either fork entry. */
static LONG return_back_continuation(KASPACE *space, const SYSCALL_RETURN *saved)
{
    LONG error = public_back_range(space, saved->mappings.entry, 1,
                                   ASPACE_PROT_USER | ASPACE_PROT_EXEC);
    if (error) return error;
    return public_back_range(space, saved->mappings.stack_base,
                             saved->mappings.stack_size, ASPACE_PROT_USER | ASPACE_PROT_WRITE);
}
static BOOL return_child_completed(void *context)
{
    PROCESS_COMPLETION completed;
    return proc_clone_observer_process_snapshot(context, &completed) &&
        completed.state == PROCESS_COMPLETED && (completed.order_low || completed.order_high);
}

/* Authenticated architecture dispatchers only. The shared test child must
 * exit before the parent can reuse its user stack. A private parent returns
 * without waiting (Pfork), or waits for the exact child's final completion
 * or committed Pexec(200) (Pvfork). The test probe inherits no resources;
 * public Pfork/Pvfork inherit every descriptor, the directories and the
 * DTA/search state through the PROCESS file snapshot. */
static LONG return_clone(SYSCALL_RETURN *saved, UWORD fail_stage, UWORD resources)
{
    PROC_CLONE_TRANSACTION *tx = NULL;
    PROC_CLONE_OBSERVER *observer = NULL;
    PROC_CLONE_IDENTITIES created = { 0, 0 };
    KPROC *source = kproc_retain(run);
    PD *controller;
    PROC_CLONE_ARGS args = { 0 };
    LONG error = EINVFN;
    UWORD stage = 0;
    BOOL private_vm = FALSE;
#if CONF_WITH_FORK_CORE
    BOOL suspend_parent=FALSE,cancelled=FALSE;
    FORK_PARENT_DEPENDENCY *dependency=NULL;
    FORK_PARENT_REASON reason=FORK_PARENT_NONE;
    KSCHED_CLEANUP_SCOPE scope={0};
    ksched_cleanup_begin(&scope);
#endif
    if (!source || !saved) goto done;
    saved->owner = ksched_exit_thread(run);
    saved->space = kproc_active_space(source);
    if (!saved->owner || !ksched_clone_context(source, &controller)) goto done;
    /* Private native assertion aid: identity, not an execution-stack address. */
    if (fail_stage == 0xfffe) { error = kthread_tid(saved->owner); goto done; }
#if CONF_WITH_FORK_CORE
    if (fail_stage==0xfffd) { error=ksched_test_fork_parent_blocked(source);goto done; }
    if (fail_stage==0xfffc) { error=(LONG)fork_parent_test_live();goto done; }
    if (fail_stage==0xfffb) { error=ksched_test_fork_cancel_parent(source)?E_OK:EACCDN;goto done; }
    if (fail_stage==0xfffa) { error=ksched_test_fork_cancel_survived(source)?E_OK:EACCDN;goto done; }
    if (fail_stage==0xfff9) { error=(LONG)ksched_test_fork_cancel_survivors();goto done; }
    suspend_parent=(fail_stage&FORK_RETURN_VFORK)!=0;
    if (suspend_parent && !(fail_stage&FORK_RETURN_PRIVATE)) goto done;
    fail_stage&=~FORK_RETURN_VFORK;
#endif
    /* Public Pfork/Pvfork refuse a multi-THREAD or shared-root caller with
     * EACCDN; the test probe keeps its EINVFN contract. */
    if (resources) error = EACCDN;
    if (!kproc_thread_is_sole(source,saved->owner)) goto done;
#if CONF_WITH_PRIVATE_CLONE
    private_vm = (fail_stage & FORK_RETURN_PRIVATE) != 0;
    fail_stage &= ~FORK_RETURN_PRIVATE;
    /* kaspace_clone copies captured lazy reservations into the child's own
     * canonical records (#971, #1000), so a source holding them forks too. */
#endif
    if (fail_stage >
#if CONF_WITH_FORK_CORE
        (suspend_parent?8:7)
#else
        7
#endif
        || kproc_has_aspace_peer(source)) goto done;
    /* Back the captured stack window in the caller first, through its own
     * stack, heap, mapping and captured-lazy authority, so the clone holds
     * every page of it; an untouched page below SP is not an error. */
    error = kproc_back_user_write_checked(saved->mappings.stack_base, saved->mappings.stack_size);
    if (error) goto done;
    error = EINVFN;
    args.source = source; args.prepared_process = NULL;
    args.process_policy = PROC_CLONE_NEW_PROCESS;
    args.vm_policy = private_vm ? PROC_CLONE_PRIVATE_VM : PROC_CLONE_SHARE_VM;
    args.resources = resources;
#define RETURN_STAGE(call) do { error = (call); if (error) goto done; \
    if (++stage == fail_stage) { error = ENSMEM; goto done; } } while (0)
    RETURN_STAGE(proc_clone(&args, &tx));
    /* The return PC can start an untouched demand-paged page (a trap that
     * ends at a page boundary); materialize it as clone_start_entry() does. */
    RETURN_STAGE(return_back_continuation(tx->space, saved));
    RETURN_STAGE(proc_clone_prepare_saved_return(tx, saved));
    RETURN_STAGE(proc_clone_retain_startup_stack(tx));
    RETURN_STAGE(proc_clone_prepare_outputs(tx, 0, 0));
    RETURN_STAGE(proc_clone_observe_async(tx, &observer));
    RETURN_STAGE(thread_startup_validate(tx->startup));
#if CONF_WITH_FORK_CORE
    if (suspend_parent) {
        RETURN_STAGE(fork_parent_prepare(saved->owner,tx->process,&dependency));
        tx->parent_dependency=dependency;
    }
#endif
#undef RETURN_STAGE
    error = proc_clone_spawn(&tx, &created);
    if (!error) {
        /* No dispatch occurred after publication. Retained observation is
         * non-consuming: the caller still owns one public Pwaitpid reap. */
        if (!private_vm && !ksched_wait_ready(return_child_completed, observer))
            panic("saved return lost managed parent");
#if CONF_WITH_FORK_CORE
        if (suspend_parent) {
            error=fork_parent_wait(dependency,&reason);
            cancelled=ksched_cancellation_pending();
            /* The child is published either way: unless the parent itself
             * is cancelled, it gets the PID to reap, not a bare error. */
            if (error && cancelled) goto done;
        }
#endif
        error = created.pid;
    }
done:
    proc_clone_abort(&tx);
#if CONF_WITH_FORK_CORE
    fork_parent_release(&dependency);
#endif
    proc_clone_observer_release(&observer);
    kproc_release(source);
#if CONF_WITH_FORK_CORE
    ksched_cleanup_end(&scope);
#if CONF_WITH_MULTITHREADING
    if (cancelled || ksched_cancellation_pending()) ksched_cancel_point();
#endif
#endif
    return error;
}
#if CONF_WITH_FORK_RETURN_TEST
LONG proc_clone_return_probe(SYSCALL_RETURN *saved, UWORD fail_stage)
{
    return return_clone(saved, fail_stage, 0);
}
#endif
#if CONF_WITH_FORK
LONG proc_clone_fork(SYSCALL_RETURN *saved, BOOL vfork)
{
    return return_clone(saved, FORK_RETURN_PRIVATE | (vfork ? FORK_RETURN_VFORK : 0),
                        PROC_CLONE_COPY_STANDARD_HANDLES | PROC_CLONE_COPY_CWD | PROC_CLONE_COPY_FILES);
}
#endif
#endif
#endif

static LONG clone_start_entry(const PROC_CLONE_ARGS *args, const THREAD_STARTUP_ARGS *startup,
                             VADDR pid, VADDR tid, BOOL return_pid,BOOL scoped,
                             const struct kproc_image_policy *image_policy)
{
    PROC_CLONE_TRANSACTION *tx = NULL;
    PROC_CLONE_OBSERVER *observer = NULL;
    PROC_CLONE_IDENTITIES created = { 0, 0 };
    PD *controller;
    ULONG identity;
    LONG error;
#if !CONF_WITH_IMAGE_POLICY_TEST
    if (image_policy) return EINVFN;
#endif
    if (!args || !startup || (args->process_policy != PROC_CLONE_NEW_PROCESS &&
        args->process_policy != PROC_CLONE_SAME_PROCESS) ||
        (scoped ? (args->vm_policy!=PROC_CLONE_SCOPED_VM || args->process_policy!=PROC_CLONE_NEW_PROCESS || return_pid) :
         return_pid ? (args->vm_policy != PROC_CLONE_PRIVATE_VM || args->process_policy != PROC_CLONE_NEW_PROCESS || pid || tid)
                    : args->vm_policy != PROC_CLONE_SHARE_VM))
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
    if (!error) error =
#if CONF_WITH_PRIVATE_CLONE
        return_pid ? thread_startup_prepare_cloned(tx->source_space, tx->space, startup, TRUE, &tx->startup) :
#endif
        thread_startup_prepare_public(tx->space,startup,&tx->startup);
    if (!error) error = proc_clone_retain_startup_stack(tx);
#if CONF_WITH_IMAGE_POLICY_TEST
    if (!error && image_policy) error=proc_clone_prepare_image_policy(tx,image_policy);
#endif
    if (!error) error = proc_clone_prepare_outputs(tx,pid,tid);
    if (!error) error = proc_clone_observe_async(tx,&observer);
    if (!error) error = proc_clone_spawn(&tx,&created);
done:
    proc_clone_abort(&tx);
    proc_clone_observer_release(&observer);
    return error ? error : (return_pid ? (LONG)created.pid : (LONG)identity);
}
LONG proc_clone_start_public(const PROC_CLONE_ARGS *args, const THREAD_STARTUP_ARGS *startup, VADDR pid, VADDR tid)
{ return clone_start_entry(args, startup, pid, tid, FALSE,FALSE,NULL); }
#if CONF_WITH_IMAGE_POLICY_TEST
LONG proc_clone_start_image_policy_test(const PROC_CLONE_ARGS *args,const THREAD_STARTUP_ARGS *startup)
{
    KPROC_IMAGE_POLICY policy;
    LONG error;
    if (!args || args->process_policy!=PROC_CLONE_NEW_PROCESS || args->vm_policy!=PROC_CLONE_SHARE_VM)
        return EINVAL;
    error=kproc_image_policy_capture(args->source,&policy);
    return error?error:clone_start_entry(args,startup,0,0,FALSE,FALSE,&policy);
}
#endif
#if CONF_WITH_PRIVATE_CLONE_TEST
LONG proc_clone_start_private_test(const PROC_CLONE_ARGS *args, const THREAD_STARTUP_ARGS *startup)
{ return clone_start_entry(args, startup, 0, 0, TRUE,FALSE,NULL); }
LONG proc_clone_start_scoped_test(const PROC_CLONE_ARGS *args,const THREAD_STARTUP_ARGS *startup,VADDR pid,VADDR tid)
{ return clone_start_entry(args,startup,pid,tid,FALSE,TRUE,NULL); }
#endif

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
    if (kproc_live_pd(tx->source) != run || !aspace_is_current(kaspace_backend(tx->source_space))) return EINVFN;
    if (((tx->policy != PROC_CLONE_NEW_PROCESS || (!tx->owns_process && !tx->loaded_shared)) &&
         tx->policy != PROC_CLONE_SAME_PROCESS) || !tx->startup ||
        !tx->observer || tx->observer->state != PROC_CLONE_OBSERVER_PREPARED) return EINVFN;
    if (!startup_target_live(tx) || !kthread_clone_run_ready(tx->thread)) return EACCDN;
    if (thread_startup_state(tx->startup) != THREAD_STARTUP_OBSERVED) return EINVAL;
    if (!(tx->loaded_shared ? ksched_loaded_clone_context(tx->source,&controller) : ksched_clone_context(tx->source,&controller))) return EINVFN;
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
#if CONF_WITH_FORK_CORE
    if (tx->parent_dependency && !fork_parent_validate(tx->parent_dependency)) { error=EACCDN;goto failed; }
#endif
    if (!(tx->policy == PROC_CLONE_SAME_PROCESS ?
          kproc_clone_sibling_ready(tx->process, tx->thread, tx->space) :
          kproc_clone_run_ready(tx->process, tx->thread, tx->space)) ||
        !startup_target_live(tx)) { error = EACCDN; goto failed; }
    /* Only no-allocation/no-callback revalidation, sealing and stores occur
     * under backend exclusion. Cleanup always restores the entry IRQ state. */
    {
        PD *confirmed;
        if (!(tx->loaded_shared ? ksched_loaded_clone_begin(tx->source,&confirmed,&guard) : ksched_clone_begin(tx->source,&confirmed,&guard))) { error = EINVFN; goto failed; }
        if (confirmed != controller) { ksched_clone_end(&guard); error = EACCDN; goto failed; }
    }
    error = thread_startup_seal(tx->startup);
#if CONF_WITH_PROCESS_FILES
    if (!error && tx->files) error=kproc_files_snapshot_seal(tx->files);
#endif
    if (error) { ksched_clone_end(&guard); goto failed; }
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_PRIVATE_CLONE_TEST || CONF_WITH_PROCESS_FILES_TEST
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
    if (kproc_live_pd(tx->source) != run || !aspace_is_current(kaspace_backend(tx->source_space))) return EINVFN;
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
        !startup_target_live(tx)) { error = EACCDN; goto failed; }
    error = thread_startup_seal(startup);
#if CONF_WITH_PROCESS_FILES
    if (!error && tx->files) error=kproc_files_snapshot_seal(tx->files);
#endif
    if (error) goto failed;
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_PRIVATE_CLONE_TEST || CONF_WITH_PROCESS_FILES_TEST
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

#if CONF_WITH_MULTITHREADING
/* Invoked by scheduler root or the serialized active creator, never an observer
 * or user callback. All fallible preparation precedes this no-fail transfer. */
void proc_clone_commit_prepared(PROC_CLONE_TRANSACTION *tx, void *slot)
{
    THREAD_STARTUP_STACK_OWNERS owners = { { 0, 0 }, { 0, 0 } };
    WORD pid = kproc_ref_pid(tx->process);
    ULONG tid = kthread_tid(tx->thread);
    THREAD_STARTUP_DRAFT *startup = tx->startup;
    if (!thread_startup_take_stacks(startup, &owners)) panic("clone publication: missing prepared stacks");
#if CONF_WITH_PROCESS_FILES
    if (tx->files) kproc_files_snapshot_commit(tx->files,tx->process);
#endif
#if CONF_WITH_FORK_CORE
    if (tx->parent_dependency) {
        fork_parent_publish(tx->parent_dependency);
        tx->parent_dependency=NULL; /* parent THREAD keeps it through suspension */
    }
#endif
    if (tx->policy == PROC_CLONE_SAME_PROCESS) {
        kproc_clone_commit_sibling(tx->process, tx->thread);
    } else {
        kproc_clone_commit_mapping_owners(tx->process,tx->space,&tx->mapping_members);
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
