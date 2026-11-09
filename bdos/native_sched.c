/* Bounded cooperative kernel tasks, independent of the legacy AES scheduler.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "emutos.h"
#include "biosext.h"
#include "ksched.h"
#include "kproc.h"
#include "native_sched_backend.h"
#include "kthread_context.h"
#include "kthread.h"
#include "thread_startup.h"
#include "proc_clone.h"
#include "fs.h"
#include "proc.h"
#include <stddef.h>
#include "kheap.h"
#include "kprint.h"
#include "gemerror.h"
#include "string.h"
#if CONF_WITH_X86_64_MEMTEST
#include "../bios/machine/pc-x86_64/pc_x86_64_memtest.h"
#endif
#if CONF_WITH_CUIS
#include "../aes/cuis.h"
#endif
extern PD *run;

typedef enum { TASK_UNUSED, TASK_RESERVED, TASK_RETIRING, TASK_NEW, TASK_RUNNING, TASK_BLOCKED, TASK_DRAINING, TASK_DONE, TASK_ZOMBIE
    , TASK_DEFERRED
} TASK_STATE;
typedef struct kernel_task {
    PD *program, *parent;
    struct kernel_task *completion_owner; /* borrowed; NULL means this is the anchor */
    BOOL process_completed;
#if CONF_WITH_PTERMGRP
    BOOL cancel_boundary; /* only waits whose callers explicitly checkpoint */
    BOOL cancel_delivering; /* termination hooks must not recursively cancel */
#endif
    KPROC *process;              /* observer identity, including after exit */
    KTHREAD *thread;            /* owns execution until root-safe reap */
#if CONF_WITH_X86_64_MEMTEST
    ULONG prepared_tid;
#endif
    KPROC *deferred_owner_identity; /* canonical predecessor, not PD/resources */
    WORD pid, signal;
    BOOL asynchronous, take_ownership, exited;
    THREAD_STARTUP_DRAFT *startup;
    KTHREAD_CONTEXT execution;
    TASK_STATE state;
    BOOL (*ready)(void *);
    void *argument;
    LONG result;
    ULONG usage[2];
} KERNEL_TASK;
static struct {
    BOOL active;
    UWORD initial, next;
    KERNEL_TASK *tasks, *current;
    NATIVE_SCHED_CONTEXT root;
    jmp_buf resume;
    uintptr_t flags;
    PROC_CLONE_TRANSACTION *pending_clone;
} scheduler;
static UQUAD next_identity;
#if CONF_WITH_X86_64_MEMTEST
static BOOL clone_masked;
BOOL ksched_test_clone_masked(void) { return clone_masked; }
#endif
static void publish_task(void **slot, TASK_STATE state);
static KERNEL_TASK *completion_task(KERNEL_TASK *task)
{
    return task->completion_owner ? task->completion_owner : task;
}
BOOL ksched_sibling_launch(PD *pd)
{
    return ksched_spawn_allowed() && scheduler.current->completion_owner && scheduler.current->program == pd;
}
THREAD_STARTUP_DRAFT *ksched_thread_startup(PD *pd)
{
    KERNEL_TASK *task = scheduler.current;
    return scheduler.active && task && task->program == pd ? task->startup : NULL;
}
BOOL ksched_take_syscall_stack(PD *pd, THREAD_STARTUP_STACK *owner)
{
    KERNEL_TASK *task = scheduler.current;
    KTHREAD_STACK stack;
    if (!owner || owner->physical || owner->top || !ksched_sibling_launch(pd)) return FALSE;
    stack = kthread_stack_detach(&task->execution.syscall_stack);
    owner->physical = stack.physical;
    owner->top = stack.top;
    return stack.physical != 0;
}


BOOL ksched_spawn_allowed(void)
{
    return scheduler.active && scheduler.current && scheduler.current->state == TASK_RUNNING;
}
BOOL ksched_active(void) { return scheduler.active; }

KTHREAD *ksched_exit_thread(PD *pd)
{
    KERNEL_TASK *task = scheduler.current;
    if (!scheduler.active || !task || task->state != TASK_RUNNING ||
        task->program != pd || !kthread_is_executing(task->thread))
        return NULL;
    return task->thread;
}

#if CONF_WITH_PTERMGRP
/* This runs on a restored task/syscall stack, never on scheduler root. A
 * nested child owns a different launch chain and must return normally. */
void ksched_cancel_point(void)
{
    KTHREAD *thread = ksched_exit_thread(run);
    UWORD status;
    if (thread && !scheduler.current->cancel_delivering &&
        kproc_group_status(kthread_process(thread), &status)) xterm(status);
}
void ksched_enter_exit(PD *pd)
{
    if (ksched_exit_thread(pd)) scheduler.current->cancel_delivering = TRUE;
}
LONG ksched_termgrp(UWORD status)
{
    KTHREAD *thread = ksched_exit_thread(run);
    uintptr_t flags;
    if (!thread) return EINVFN;
    if (scheduler.current->cancel_delivering) return EACCDN;
    flags = native_sched_irq_save();
    if (!kproc_request_group_exit(kthread_process(thread), status)) {
        native_sched_irq_restore(flags); return EACCDN;
    }
    native_sched_irq_restore(flags);
    ksched_cancel_point();
    panic("group exit returned");
    return EINVFN;
}
#endif

static void check_task_thread(KERNEL_TASK *task, UWORD stage)
{
#if CONF_WITH_X86_64_MEMTEST
    if (!task->thread || kthread_tid(task->thread) != task->prepared_tid ||
        kthread_process(task->thread) != task->process)
        panic("scheduler: task thread identity changed");
    x86_64_memtest_task_thread(task->thread, task->execution.stack.physical, stage);
#else
    UNUSED(task);
    UNUSED(stage);
#endif
}

BOOL ksched_finalizing_process(const KPROC *process)
{
    KERNEL_TASK *task=scheduler.current;
    /* The THREAD is already stopped by launcher handoff. Its scheduler
     * continuation still owns final PROCESS/wait publication until root. */
    return scheduler.active && task && task->state==TASK_RUNNING && task->process==process;
}

BOOL ksched_finish_exit(KPROC *process, PD *pd, UWORD status)
{
    KTHREAD *thread = ksched_exit_thread(pd);
    BOOL final;
    if (!thread) return TRUE; /* Nested synchronous child, not this task's process. */
    final = kproc_exit_handoff_pending(process);
    /* Fault intent belongs to this execution, never to a surviving sibling.
     * Group exit preserves its first normal status instead of a later fault. */
    if (final && scheduler.current->signal
#if CONF_WITH_PTERMGRP
        && !kproc_group_status(process, NULL)
#endif
       ) kproc_note_fault(pd, scheduler.current->signal);
    if (kthread_process(thread) != process || !kthread_finish_exit(thread, status))
        panic("scheduler: invalid launcher exit handoff");
    scheduler.current->exited = TRUE;
    check_task_thread(scheduler.current, 3);
    return final;
}

/* The thread is the sole owner of its backend scheduler stack. Called only
 * for an unpublished task or after root has made its continuation unreachable.
 * The syscall stack has a different owner: the existing gouser launcher. */
static void reclaim_task_execution(void *resource)
{
    KTHREAD_CONTEXT *execution = resource;
    if (scheduler.current && &scheduler.current->execution == execution)
        panic("scheduler: reclaiming current execution stack");
#if CONF_WITH_X86_64_MEMTEST
    x86_64_memtest_task_thread(NULL, execution->stack.physical, 7);
#endif
    kthread_stack_release(&execution->stack);
    kthread_stack_release(&execution->syscall_stack);
    {
        KERNEL_TASK *task = (KERNEL_TASK *)((UBYTE *)execution - offsetof(KERNEL_TASK, execution));
        if (task->startup) thread_startup_reclaim(&task->startup);
    }
}

static void adopt_task_execution(KERNEL_TASK *task)
{
    if (!task->thread || !kthread_set_execution(task->thread, &task->execution, reclaim_task_execution))
        panic("scheduler: execution ownership transfer failed");
#if CONF_WITH_X86_64_MEMTEST
    task->prepared_tid = kthread_tid(task->thread);
#endif
    check_task_thread(task, 0);
}

static BOOL attach_task_thread(KERNEL_TASK *task)
{
    task->thread = kthread_create(task->program);
    if (!task->thread) return FALSE;
    adopt_task_execution(task);
    return TRUE;
}

static void discard_task_execution(KERNEL_TASK *task)
{
    if (task->thread) {
        kthread_destroy(task->thread);
        task->thread = NULL;
    } else {
        kthread_stack_release(&task->execution.stack);
    }
}

BOOL ksched_wait_ready(BOOL (*ready)(void *), void *argument)
{
    uintptr_t flags;
    BOOL managed;
    flags = native_sched_irq_save();
    managed = ksched_block(ready,argument);
    native_sched_irq_restore(flags);
    return managed;
}

UQUAD ksched_context_id(void)
{
    KERNEL_TASK *task = scheduler.current;
    return scheduler.active && task &&
           (task->state == TASK_RUNNING || task->state == TASK_BLOCKED) ? task->execution.identity : 0;
}

void ksched_context_restore(UQUAD identity)
{
    UWORD i;
    scheduler.current = NULL;
    if (!scheduler.active || !identity) return;
    for (i = 0; i < KSCHED_LIMIT; i++) {
        KERNEL_TASK *task = &scheduler.tasks[i];
        if (task->execution.identity == identity &&
            (task->state == TASK_RUNNING || task->state == TASK_BLOCKED)) {
            scheduler.current = task;
            return;
        }
    }
}

BOOL ksched_pid_reserved(WORD pid)
{
    UWORD i;
#if CONF_WITH_CUIS
    if (cuis_child_pid_reserved(pid)) return TRUE;
#endif
    if (!scheduler.active) return FALSE;
    for (i = 0; i < KSCHED_LIMIT; i++)
        if (scheduler.tasks[i].state != TASK_UNUSED && scheduler.tasks[i].pid == pid) return TRUE;
    return FALSE;
}

BOOL ksched_parent_pid(PD *pd, WORD *pid)
{
    UWORD i;
    if (!scheduler.active) return FALSE;
    for (i = scheduler.initial; i < KSCHED_LIMIT; i++) {
        KERNEL_TASK *task = &scheduler.tasks[i];
        if (task->state != TASK_UNUSED && task->state != TASK_RESERVED && task->state != TASK_RETIRING && task->program == pd) {
            KERNEL_TASK *owner = completion_task(task);
            *pid = kproc_pid(owner->parent ? owner->parent : scheduler.root.pd);
            return TRUE;
        }
    }
    return FALSE;
}

/* Called before a process's record/basepage is freed, including nested
 * synchronous children. No orphan keeps a pointer to its former parent. */
void ksched_exiting(PD *pd, UWORD result)
{
    UWORD i;
    KPROC *exiting;
#if CONF_WITH_CUIS
    BOOL asynchronous = FALSE;
    if (!scheduler.active) {
        PD *parent = kproc_notification_parent(pd);
        if (parent) cuis_child_completed(parent,kproc_pid(pd),result);
        return;
    }
#else
    if (!scheduler.active) return;
#endif
    exiting = kproc_retain(pd); /* identity remains valid through committed cleanup */
    for (i = 0; i < KSCHED_LIMIT; i++) {
        KERNEL_TASK *task = &scheduler.tasks[i];
        if (task->state == TASK_UNUSED || task->state == TASK_RESERVED || task->state == TASK_RETIRING) continue;
        if (task->parent == pd) {
            task->parent = NULL;
            if (task->state == TASK_ZOMBIE) task->state = TASK_DONE;
        }
        if (!task->completion_owner && exiting && task->process == exiting) {
            ULONG usage[8];
            if (task->process_completed) panic("duplicate PROCESS completion");
            task->process_completed = TRUE;
#if CONF_WITH_PTERMGRP
            if (kproc_group_status(exiting, NULL) && scheduler.current)
                scheduler.current->signal = 0;
#endif
            if (scheduler.current && scheduler.current->process == task->process)
                task->signal = scheduler.current->signal;
#if CONF_WITH_CUIS
            asynchronous = task->asynchronous;
#endif
            kproc_usage(pd, usage);
            task->usage[0] = usage[1] + usage[3];
            task->usage[1] = 0;
            task->result = result;
            task->exited = TRUE;
            /* The continuation only needs the trusted PID/result from now
             * on; process cleanup is about to release the basepage. */
            task->program = NULL;
        }
    }
    kproc_release(exiting);
#if CONF_WITH_CUIS
    /* Nested synchronous children never return through task_entry(). Their
     * trusted parent/PID still exist here; async tasks publish at task_entry
     * after their final result/signal is known. */
    if (!asynchronous) {
        PD *parent = kproc_notification_parent(pd);
        if (parent) cuis_child_completed(parent,kproc_pid(pd),result);
    }
#endif
}

/* Activate only the retained canonical predecessor, after PROCESS cleanup.
 * The caller owns a reference; a reused PD can never admit this successor. */
void ksched_exited_identity(KPROC *process, PD *pd)
{
    UWORD i;
    UNUSED(pd);
    if (!scheduler.active || !process) return;
    for (i = 0; i < KSCHED_LIMIT; i++) {
        KERNEL_TASK *task = &scheduler.tasks[i];
        if (task->state == TASK_DEFERRED && task->deferred_owner_identity == process) {
            task->deferred_owner_identity = NULL;
            kproc_release(process);
            check_task_thread(task, 5);
            task->state = TASK_NEW;
        }
    }
}

void ksched_fault(PD *pd, WORD signal)
{
    UWORD i;
    if (!scheduler.active) {
        kproc_note_fault(pd, signal);
#if CONF_WITH_CUIS
        PD *parent = kproc_notification_parent(pd);
        if (parent) cuis_child_fault(parent,kproc_pid(pd),signal);
#endif
        return;
    }
    /* A foreign callback changes run, but retains the invoking task's
     * continuation. Attribute its fault to the owner, whose exit is deferred.
     * Nested synchronous children have no task entry of their own. */
    for (i = 0; i < KSCHED_LIMIT; i++) {
        KERNEL_TASK *task = &scheduler.tasks[i];
        if (scheduler.current && scheduler.current->program == pd && task != scheduler.current)
            continue;
        if ((task->state == TASK_NEW || task->state == TASK_RUNNING ||
             task->state == TASK_BLOCKED) && task->program == pd) {
            if (!task->signal) task->signal = signal;
#if CONF_WITH_CUIS
            if (!completion_task(task)->asynchronous &&
                kproc_thread_is_sole(task->process, task->thread)) {
                PD *parent = kproc_notification_parent(pd);
                if (parent) cuis_child_fault(parent,kproc_pid(pd),task->signal);
            }
#endif
            return;
        }
    }
    kproc_note_fault(pd, signal); /* Nested child: no THREAD slot or live siblings. */
#if CONF_WITH_CUIS
    /* Nested synchronous children have no task slot of their own. */
    {
        PD *parent = kproc_notification_parent(pd);
        if (parent) cuis_child_fault(parent,kproc_pid(pd),signal);
    }
#endif
}

static void task_entry(void)
{
    KERNEL_TASK *task = scheduler.current;
    BOOL refused;
    check_task_thread(task, 2);
#if CONF_WITH_X86_64_MEMTEST
    if (task->startup && kthread_create(task->program))
        panic("clone admitted unvalidated execution after publication");
#endif
    {
        LONG status = proc_start_scheduled(task->program, task->take_ownership);
        if (!task->process_completed) task->result = status;
    }
    refused = !task->exited;
    if (refused) {
        if (task->completion_owner)
            panic("published sibling refused its prepared startup");
        /* No user continuation was published. Stop, then defer the refused
         * process's disposal until root reaps this execution resource. */
        if (!kthread_stop(task->thread))
            panic("scheduler: refused task did not stop");
        proc_discard_scheduled(task->program);
    }
    if (!kthread_is_stopped(task->thread))
        panic("scheduler: exited task has a resumable thread");
#if CONF_WITH_X86_64_MEMTEST
    if (task->asynchronous && !task->completion_owner && !task->parent && task->result)
        kcprintf("orphan PID %d returned %ld\n", task->pid, (long)task->result);
#endif
    task->program = NULL;
    {
        KERNEL_TASK *owner = completion_task(task);
        if (owner->process_completed || refused) {
            owner->state = owner->asynchronous && owner->parent ? TASK_ZOMBIE : TASK_DONE;
            /* Status and resources are finalized; wait visibility precedes
             * generic observer readiness. A refused launch is abandoned. */
            if (owner->process_completed) kproc_publish_observers(owner->process);
            /* A refused unstarted task may still retain its association.
             * Retirement during root reap publishes abandonment, not status. */
#if CONF_WITH_CUIS
            if (owner->state == TASK_ZOMBIE)
                cuis_child_completed(owner->parent,owner->pid,
                    owner->signal ? (UWORD)owner->signal << 8 : (UWORD)owner->result);
#endif
        } else if (owner == task) {
            /* Initial TID has stopped, but PID/resources/children remain live.
             * Its continuation is root-reapable; the PROCESS slot is not. */
            task->state = TASK_DRAINING;
        }
        if (owner != task) task->state = TASK_DONE;
    }
    native_sched_save(&task->execution.cpu);
    longjmp(scheduler.resume, 1);
}

BOOL ksched_block(BOOL (*ready)(void *), void *argument)
{
    KERNEL_TASK *task = scheduler.current;
    if (!ksched_spawn_allowed()) return FALSE;
    task->ready = ready;
    task->argument = argument;
    task->state = TASK_BLOCKED;
    native_sched_save(&task->execution.cpu);
    if (!setjmp(task->execution.resume)) longjmp(scheduler.resume, 1);
    task->ready = NULL;
    task->argument = NULL;
    return TRUE;
}

BOOL ksched_cancellation_pending(void)
{
#if CONF_WITH_PTERMGRP
    KTHREAD *thread = ksched_exit_thread(run);
    return thread && !scheduler.current->cancel_delivering &&
        kproc_group_status(kthread_process(thread), NULL);
#else
    return FALSE;
#endif
}

KSCHED_WAIT_RESULT ksched_wait_cancelable(BOOL (*ready)(void *), void *argument)
{
    KERNEL_TASK *task = scheduler.current;
    if (!ksched_spawn_allowed()) return KSCHED_WAIT_UNMANAGED;
#if CONF_WITH_PTERMGRP
    if (ksched_cancellation_pending()) return KSCHED_WAIT_CANCELLED;
    task->cancel_boundary = TRUE;
#endif
    ksched_block(ready, argument);
#if CONF_WITH_PTERMGRP
    task->cancel_boundary = FALSE;
    if (ksched_cancellation_pending()) return KSCHED_WAIT_CANCELLED;
#else
    UNUSED(task);
#endif
    return KSCHED_WAIT_READY;
}

static BOOL cancellation_wait(BOOL (*ready)(void *), void *argument)
{
    return ksched_wait_cancelable(ready, argument) != KSCHED_WAIT_UNMANAGED;
}

static BOOL always_ready(void *argument) { (void)argument; return TRUE; }
LONG ksched_yield(void)
{
    uintptr_t flags;
#if CONF_WITH_X86_64_MEMTEST && CONF_WITH_MMAP
    x86_64_memtest_clone_yield(); /* process context, before IRQ exclusion */
#endif
    flags = native_sched_irq_save();
    cancellation_wait(always_ready, NULL);
    native_sched_irq_restore(flags);
#if CONF_WITH_PTERMGRP
    ksched_cancel_point();
#endif
    return E_OK;
}

#if CONF_WITH_PTHJOIN
LONG ksched_join(ULONG tid, UQUAD address)
{
    KTHREAD *caller = ksched_exit_thread(run);
    uintptr_t flags;
    UWORD status;
    ULONG output;
    LONG error;
    if (!caller) return EINVFN;
    if (address && (address >= USER_VA_LIMIT ||
        (address & (__alignof__(ULONG) - 1)) ||
        !kproc_user_access(run, address, sizeof(output), TRUE))) return EIMBA;
    flags = native_sched_irq_save();
    error = kthread_join_begin(caller, tid);
    if (error) { native_sched_irq_restore(flags); return error; }
    while (!kthread_join_ready(caller)) {
        cancellation_wait(kthread_join_ready, caller);
        native_sched_irq_restore(flags);
#if CONF_WITH_PTERMGRP
        ksched_cancel_point();
#endif
        flags = native_sched_irq_save();
    }
    native_sched_irq_restore(flags);
    if (!kthread_join_status(caller, &status)) {
        kthread_join_cancel(caller); return ESRCH;
    }
    output = status;
    /* No TID status is consumed until the entire copy succeeds. An escaping
     * memory fault stops the caller: root reap cancels its retained claim. */
    if (address && !kproc_copy_to_user(address, &output, sizeof(output))) {
        kthread_join_cancel(caller); return EIMBA;
    }
    flags = native_sched_irq_save();
    kthread_join_consume(caller);
    native_sched_irq_restore(flags);
    return E_OK;
}
#endif

/* Finish only after all private destruction has returned. The linked arena
 * implementation preserves its storage/admission metadata in this helper. */
static void clear_task(KERNEL_TASK *task)
{
    uintptr_t flags = native_sched_irq_save();
    memset(task, 0, sizeof(*task));
    native_sched_irq_restore(flags);
}
static void discard_process_task(KERNEL_TASK *task)
{
    KPROC *process, *owner;
    KTHREAD *thread;
    uintptr_t flags = native_sched_irq_save();
    check_task_thread(task, 6);
    if (task->state != TASK_RESERVED && task->state != TASK_DEFERRED)
        panic("process retirement outside private admission");
    process = task->process;
    owner = task->deferred_owner_identity;
    thread = task->thread;
    /* Transfer ownership to this continuation before any callback can yield.
     * RETIRING is private: neither a public child nor a root-reapable task.
     * Its backend/stack storage stays reserved until THREAD destruction ends. */
    task->state = TASK_RETIRING;
    task->program = NULL;
    task->parent = NULL;
    task->process = NULL;
    task->thread = NULL;
    task->deferred_owner_identity = NULL;
    task->ready = NULL;
    task->argument = NULL;
    native_sched_irq_restore(flags);
    proc_dispose_process(process);
    if (thread) kthread_destroy(thread);
    kproc_release(process);
    if (owner) kproc_release(owner);
    clear_task(task);
}
static void cancel_successors(KPROC *owner)
{
    UWORD i;
    for (i = 0; i < KSCHED_LIMIT; i++)
        if (scheduler.tasks[i].state == TASK_DEFERRED &&
            scheduler.tasks[i].deferred_owner_identity == owner)
            discard_process_task(&scheduler.tasks[i]);
}
LONG ksched_defer_cancel(void)
{
    KPROC *owner;
    if (!scheduler.active) return E_OK;
    owner = kproc_retain(run);
    if (!owner) return EAGAIN;
    if (!kproc_exec_commit_begin(owner)) { kproc_release(owner); return EACCDN; }
    cancel_successors(owner);
    kproc_exec_commit_end(owner);
    kproc_release(owner);
    return E_OK;
}
LONG ksched_process_prepare(PD *pd, BOOL deferred, void **slot)
{
    UWORD i;
    KERNEL_TASK *task;
    KPROC *owner = NULL;
    void *reservation = NULL;
    LONG result;
    if (!slot || *slot) return EINVAL;
    if (!ksched_spawn_allowed()) return EINVFN;
    if (next_identity == ~0ULL) return EAGAIN;
    if (!kproc_unlaunched(pd) || kproc_user_root(pd)) return EIMBA;
    for (i = 0; i < KSCHED_LIMIT; i++)
        if (scheduler.tasks[i].program == pd) return EACCDN;
    if (deferred) {
        owner = kproc_retain(run);
        if (!owner) return ENSMEM;
    }
    result = ksched_task_reserve(FALSE, &reservation);
    if (result != E_OK) goto failed_owner;
    task = reservation;
    task->program = pd; /* Exclude recursive preparation of this image. */
    if (!kthread_stack_allocate(&task->execution.stack)) {
        result = ENSMEM;
        ksched_task_cancel(&reservation);
        goto failed_owner;
    }
    task->process = kproc_retain(pd);
    if (!task->process) {
        kthread_stack_release(&task->execution.stack);
        ksched_task_cancel(&reservation);
        result = ENSMEM;
        goto failed_owner;
    }
    if (!kproc_prepare_async_thread(pd, run, scheduler.root.pd, &task->thread)) {
        kproc_release(task->process);
        kthread_stack_release(&task->execution.stack);
        ksched_task_cancel(&reservation);
        result = ENSMEM;
        goto failed_owner;
    }
    adopt_task_execution(task);
    task->pid = kproc_pid(pd);
    task->parent = deferred ? NULL : run;
    task->deferred_owner_identity = owner;
    task->asynchronous = TRUE;
    task->take_ownership = TRUE;
    kproc_commit_async(pd);
    *slot = reservation;
    return E_OK;
 failed_owner:
    if (owner) kproc_release(owner);
    return result;
}
LONG ksched_process_publish(void **slot)
{
    KERNEL_TASK *task = *slot;
    LONG pid = task->pid;
    if (task->state != TASK_RESERVED || !task->process || !task->thread)
        panic("process publication without preparation");
    if (task->deferred_owner_identity) publish_task(slot, TASK_DEFERRED);
    else ksched_task_publish(slot);
    return pid;
}
/* Caller already owns the predecessor selection transaction. Destructors
 * finish before the first new publication store, never under runqueue exclusion. */
static LONG finish_deferred_commit(void **slot, KPROC *owner)
{
    LONG result;
    cancel_successors(owner);
    result = ksched_process_publish(slot);
    kproc_exec_commit_end(owner);
    return result;
}
LONG ksched_process_commit(void **slot)
{
    KERNEL_TASK *task;
    KPROC *owner;
    if (!slot || !*slot) return EINVAL;
    task = *slot;
    if (task->state != TASK_RESERVED || !task->process || !task->thread) return EACCDN;
    owner = task->deferred_owner_identity;
    if (!owner) return ksched_process_publish(slot);
    if (!kproc_exec_commit_begin(owner)) return EACCDN; /* Slot remains owned. */
    return finish_deferred_commit(slot, owner);
}

void ksched_process_cancel(void **slot)
{
    KERNEL_TASK *task;
    if (!slot || !*slot) return;
    task = *slot;
    if (task->state != TASK_RESERVED || !task->process || !task->thread)
        panic("process cancellation without preparation");
    *slot = NULL;
    discard_process_task(task);
}
#if CONF_WITH_CUIS
static LONG spawn_configured(PD *pd, ULONG session, ULONG token, BOOL deferred)
{
    void *slot = NULL;
    KPROC *owner = NULL;
    LONG result;
    if (!ksched_spawn_allowed()) return EINVFN;
    if (deferred) {
        if (cuis_service(CUIS_SESSION_QUERY, session) != E_OK) return EACCDN;
        /* Legacy immediate launch must reject reentry before preparation can
         * consume the loader's image. Generic owned rights may retry COMMIT. */
        owner = kproc_retain(run);
        if (!owner) return ENSMEM;
        if (!kproc_exec_commit_begin(owner)) { kproc_release(owner); return EACCDN; }
    } else if (session || token) {
        result = cuis_child_bind_spawn(session, token, pd);
        if (result != E_OK) return result;
    }
    result = ksched_process_prepare(pd, deferred, &slot);
    if (result != E_OK) {
        if (owner) { kproc_exec_commit_end(owner); kproc_release(owner); }
        return result;
    }
    if (owner) {
        result = finish_deferred_commit(&slot, owner);
        kproc_release(owner);
        return E_OK;
    }
    return ksched_process_publish(&slot);
}
LONG ksched_spawn(PD *pd) { return spawn_configured(pd,0,0,FALSE); }
LONG ksched_spawn_observed(PD *pd, ULONG session, ULONG token)
{ return spawn_configured(pd,session,token,FALSE); }
LONG ksched_defer(PD *pd, ULONG session)
{ return spawn_configured(pd,session,0,TRUE); }
#else
LONG ksched_spawn(PD *pd)
{
    void *slot = NULL;
    LONG result = ksched_process_prepare(pd, FALSE, &slot);
    return result == E_OK ? ksched_process_publish(&slot) : result;
}
#endif

typedef struct { PD *parent; WORD pid; } CHILD_WAIT;
static BOOL child_ready(void *argument)
{
    CHILD_WAIT *wait = argument;
    UWORD i;
    BOOL found = FALSE;
    for (i = scheduler.initial; i < KSCHED_LIMIT; i++) {
        KERNEL_TASK *task = &scheduler.tasks[i];
        if (task->completion_owner || task->state == TASK_UNUSED || task->state == TASK_RESERVED || task->state == TASK_RETIRING || task->parent != wait->parent ||
            (wait->pid > 0 && task->pid != wait->pid)) continue;
        found = TRUE;
        if (task->state == TASK_ZOMBIE) return TRUE;
    }
    return !found;
}

LONG ksched_wait(WORD pid, WORD options, UQUAD address)
{
    CHILD_WAIT wait;
    uintptr_t flags;
    UWORD i;
    if (options & ~3) return EINVAL;
    /* No process-group or stopped-process model is implemented yet. */
    if (!pid || pid < -1) return EINVFN;
    if (!ksched_spawn_allowed()) return EFILNF;
    wait.parent = run; wait.pid = pid;
    flags = native_sched_irq_save();
    for (;;) {
        BOOL found = FALSE;
        for (i = scheduler.initial; i < KSCHED_LIMIT; i++) {
            KERNEL_TASK *task = &scheduler.tasks[i];
            ULONG status;
            if (task->completion_owner || task->state == TASK_UNUSED || task->state == TASK_RESERVED || task->state == TASK_RETIRING || task->parent != run ||
                (pid > 0 && task->pid != pid)) continue;
            found = TRUE;
            if (task->state != TASK_ZOMBIE) continue;
            /* Back/copy output outside IRQ exclusion. This path never yields,
             * so the zombie remains owned and cannot change under the copy. */
            native_sched_irq_restore(flags);
            if (address && !kproc_copy_to_user(address, task->usage, sizeof(task->usage))) return EIMBA;
            kproc_add_child_usage(run, task->usage[0]);
            native_sched_irq_disable();
            status = ((ULONG)(UWORD)task->pid << 16) |
                     (task->signal ? ((ULONG)task->signal << 8) : (task->result & 255));
            task->parent = NULL;
            task->pid = 0;
            kproc_release(task->process);
            task->process = NULL;
            task->state = TASK_UNUSED;
            native_sched_irq_restore(flags);
            return (LONG)status;
        }
        if (!found || (options & 1)) {
            native_sched_irq_restore(flags);
            return found ? 0 : EFILNF;
        }
        cancellation_wait(child_ready, &wait);
        native_sched_irq_restore(flags);
#if CONF_WITH_PTERMGRP
        ksched_cancel_point();
#endif
        flags = native_sched_irq_save();
    }
}

/* Called only after the scheduler has switched back to its root context.
 * A zombie keeps its process identity for waiters, but its continuation and
 * stack can never run again. Repeated passes leave already-reaped stacks alone. */
#if CONF_WITH_X86_64_MEMTEST
static ULONG retiring_root_passes;
ULONG ksched_test_retiring_root_passes(void) { return retiring_root_passes; }
#endif
static BOOL reap_finished_on_root(KERNEL_TASK *task)
{
#if CONF_WITH_X86_64_MEMTEST
    if (!scheduler.current && task->state == TASK_RETIRING) retiring_root_passes++;
#endif
    if (scheduler.current || (task->state != TASK_DONE && task->state != TASK_ZOMBIE && task->state != TASK_DRAINING))
        return FALSE;
    if (task->thread) {
        check_task_thread(task, 4);
        if (!kthread_reap(task->thread))
            panic("scheduler: root cannot reap unfinished thread");
        task->thread = NULL;
    }
    if (task->state == TASK_DONE && task->process) {
        kproc_release(task->process);
        task->process = NULL;
    }
    if (task->asynchronous && task->state == TASK_DONE) {
        task->pid = 0;
        task->state = TASK_UNUSED;
    }
    return TRUE;
}

static LONG dispatch_group(UWORD count, LONG *results);

static LONG run_group(PD * const *programs, UWORD count, LONG *results,
                      BOOL take_ownership, BOOL prepare)
{
    UWORD i, j;
    if (scheduler.active || scheduler.tasks) return EAGAIN;
    if (!count || count > KSCHED_LIMIT || (prepare && count != 1)) return EINVAL;
    if (next_identity > ~0ULL - count) return EAGAIN;
    for (i = 0; i < count; i++) {
        if (!kproc_unlaunched(programs[i]) || (!prepare && !kproc_user_root(programs[i]))) return EIMBA;
        for (j = 0; j < i; j++) if (programs[i] == programs[j]) return EINVAL;
    }
    scheduler.tasks = kalloc(KSCHED_LIMIT * sizeof(*scheduler.tasks));
    if (!scheduler.tasks) return ENSMEM;
    memset(scheduler.tasks, 0, KSCHED_LIMIT * sizeof(*scheduler.tasks));
    for (i = 0; i < count; i++) {
        KERNEL_TASK *task = &scheduler.tasks[i];
        task->program = programs[i];
        task->pid = kproc_pid(programs[i]);
        task->execution.identity = ++next_identity;
        task->take_ownership = take_ownership;
        task->state = TASK_NEW;
        task->process = kproc_retain(programs[i]);
        if (!task->process || !kthread_stack_allocate(&task->execution.stack)) {
            kproc_release(task->process);
            for (j = 0; j < i; j++) {
                kthread_stack_release(&scheduler.tasks[j].execution.stack);
                kproc_release(scheduler.tasks[j].process);
            }
            kfree(scheduler.tasks);
            scheduler.tasks = NULL;
            return ENSMEM;
        }
    }
    /* Ordinary launches allocate every scheduler resource before adopting
     * the image or pinning the caller's retained basepage/environment. */
    if (prepare && !kproc_prepare_user_thread(programs[0], run, &scheduler.tasks[0].thread)) {
        for (i = 0; i < count; i++) {
            kthread_stack_release(&scheduler.tasks[i].execution.stack);
            kproc_release(scheduler.tasks[i].process);
        }
        kfree(scheduler.tasks);
        scheduler.tasks = NULL;
        return ENSMEM;
    }
    /* Every task acquires exactly one generic identity before publication.
     * Preparation/association failures keep all programs with the caller. */
    for (i = 0; i < count; i++) {
        if (prepare) {
            adopt_task_execution(&scheduler.tasks[i]);
        } else if (!attach_task_thread(&scheduler.tasks[i])) {
            for (j = 0; j < count; j++) {
                discard_task_execution(&scheduler.tasks[j]);
                kproc_release(scheduler.tasks[j].process);
            }
            kfree(scheduler.tasks);
            scheduler.tasks = NULL;
            return ENSMEM;
        }
    }
    return dispatch_group(count, results);
}

static BOOL cancel_wait_ready(const KERNEL_TASK *task)
{
#if CONF_WITH_PTERMGRP
    return task->cancel_boundary && !task->cancel_delivering &&
        task->execution.cpu.pd == task->program && kproc_group_status(task->process, NULL);
#else
    UNUSED(task);
    return FALSE;
#endif
}

static LONG dispatch_group(UWORD count, LONG *results)
{
    UWORD i;
    scheduler.initial = count;
    scheduler.next = 0;
    scheduler.current = NULL;
    scheduler.active = TRUE;
    native_sched_save(&scheduler.root);
    scheduler.flags = scheduler.root.flags;
    scheduler.root.flags = native_sched_masked(scheduler.root.flags);
    if (setjmp(scheduler.resume)) native_sched_restore(&scheduler.root);
    else if (scheduler.pending_clone) {
        PROC_CLONE_TRANSACTION *tx = scheduler.pending_clone;
        scheduler.pending_clone = NULL;
        proc_clone_commit_prepared(tx, scheduler.tasks);
    }
    for (;;) {
        BOOL pending = FALSE;
        scheduler.current = NULL;
        /* Reclaim a finished task's stack only from this root stack. */
        for (i = 0; i < KSCHED_LIMIT; i++)
            reap_finished_on_root(&scheduler.tasks[i]);
        for (i = 0; i < KSCHED_LIMIT; i++) {
            UWORD slot = (scheduler.next + i) % KSCHED_LIMIT;
            KERNEL_TASK *task = &scheduler.tasks[slot];
            if (task->state == TASK_UNUSED || task->state == TASK_RESERVED || task->state == TASK_RETIRING || task->state == TASK_DONE || task->state == TASK_ZOMBIE) continue;
            if (task->state == TASK_DRAINING) { pending = TRUE; continue; }
            pending = TRUE;
            if (task->state == TASK_DEFERRED) continue;
            if (task->state == TASK_BLOCKED && !task->ready(task->argument) &&
                !cancel_wait_ready(task)) continue;
            scheduler.next = (slot + 1) % KSCHED_LIMIT;
            scheduler.current = task;
            if (task->state == TASK_NEW) {
                if (task->startup) {
                    if (!kthread_is_executing(task->thread)) panic("scheduler: unpublished clone identity");
                } else if (!kthread_start(task->thread))
                    panic("scheduler: task identity cannot start");
                check_task_thread(task, 1);
                task->state = TASK_RUNNING;
                native_sched_enter(task->execution.stack.top, task_entry);
            }
            task->state = TASK_RUNNING;
            native_sched_restore(&task->execution.cpu);
            longjmp(task->execution.resume, 1);
        }
        if (!pending) break;
        native_sched_idle();
    }
    for (i = 0; i < count; i++) results[i] = scheduler.tasks[i].result;
    kfree(scheduler.tasks);
    scheduler.tasks = scheduler.current = NULL;
    scheduler.active = FALSE;
    scheduler.root.flags = scheduler.flags;
    native_sched_restore(&scheduler.root);
    return E_OK;
}

LONG ksched_run(PD * const *programs, UWORD count, LONG *results)
{
    return run_group(programs, count, results, TRUE, FALSE);
}

LONG ksched_exec(PD *program, BOOL take_ownership, LONG *result)
{
    return run_group(&program, 1, result, take_ownership, TRUE);
}

#if CONF_WITH_X86_64_MEMTEST
static BOOL fail_clone_slot;
void ksched_test_fail_clone_slot(void) { fail_clone_slot = TRUE; }
#endif
/* A reservation owns storage, not PROCESS/THREAD resources. It stays private
 * across preparation callbacks and cannot be selected by recursive spawn. */
LONG ksched_task_reserve(BOOL root, void **slot)
{
    KERNEL_TASK *task;
    UWORD index = 0;
    if (!slot || *slot) return EINVAL;
    if (root) {
        if (scheduler.active || scheduler.tasks) return EAGAIN;
    } else {
        if (!ksched_spawn_allowed()) return EINVFN;
        for (index = scheduler.initial; index < KSCHED_LIMIT; index++)
            if (scheduler.tasks[index].state == TASK_UNUSED) break;
        if (index == KSCHED_LIMIT) return EAGAIN;
    }
#if CONF_WITH_X86_64_MEMTEST
    if (fail_clone_slot) { fail_clone_slot = FALSE; return ENSMEM; }
#endif
    if (next_identity == ~0ULL) return EAGAIN;
    if (root) {
        scheduler.tasks = kalloc(KSCHED_LIMIT * sizeof(*scheduler.tasks));
        if (!scheduler.tasks) return ENSMEM;
        memset(scheduler.tasks, 0, KSCHED_LIMIT * sizeof(*scheduler.tasks));
    }
    task = &scheduler.tasks[index];
    memset(task, 0, sizeof(*task));
    task->execution.identity = ++next_identity;
    task->state = TASK_RESERVED;
    *slot = task;
    return E_OK;
}
void ksched_task_cancel(void **slot)
{
    KERNEL_TASK *task;
    if (!slot || !*slot) return;
    task = *slot;
    if (!scheduler.tasks || task->state != TASK_RESERVED ||
        (!scheduler.active && task != scheduler.tasks))
        panic("task cancellation outside private reservation");
    *slot = NULL;
    memset(task, 0, sizeof(*task));
    if (!scheduler.active) { kfree(scheduler.tasks); scheduler.tasks = NULL; }
}
static void publish_task(void **slot, TASK_STATE state)
{
    KERNEL_TASK *task = *slot;
    *slot = NULL;
    __asm__ volatile ("" ::: "memory");
    task->state = state;
}
void ksched_task_publish(void **slot) { publish_task(slot, TASK_NEW); }
BOOL ksched_startup_reclaimable(const THREAD_STARTUP_DRAFT *startup)
{
    UWORD i;
    if (!scheduler.active || scheduler.current || !startup) return FALSE;
    for (i = 0; i < KSCHED_LIMIT; i++) {
        KERNEL_TASK *task = &scheduler.tasks[i];
        if (task->startup == startup)
            return !task->program && task->thread && !kthread_is_executing(task->thread) &&
                (task->state == TASK_DONE || task->state == TASK_ZOMBIE || task->state == TASK_DRAINING);
    }
    return FALSE;
}
BOOL ksched_clone_context(KPROC *source, PD **controller)
{
#if CONF_WITH_PTERMGRP
    if (kproc_group_status(source, NULL)) return FALSE;
#endif
    if (!ksched_spawn_allowed() || scheduler.current->process != source ||
        scheduler.current->program != run || kproc_live_pd(source) != run) return FALSE;
    *controller = scheduler.root.pd; return TRUE;
}
BOOL ksched_clone_begin(KPROC *source, PD **controller, KSCHED_PUBLICATION_GUARD *guard)
{
    uintptr_t flags;
    flags = native_sched_irq_save();
    if (!ksched_clone_context(source, controller)) {
        native_sched_irq_restore(flags);
        return FALSE;
    }
#if CONF_WITH_X86_64_MEMTEST
    if (clone_masked) panic("nested clone publication exclusion");
    clone_masked = TRUE;
#endif
    guard->flags = flags; return TRUE;
}
void ksched_clone_end(const KSCHED_PUBLICATION_GUARD *guard)
{
#if CONF_WITH_X86_64_MEMTEST
    if (!clone_masked) panic("clone publication exclusion already released");
    clone_masked = FALSE;
#endif
    native_sched_irq_restore(guard->flags);
}
void ksched_clone_commit(void *slot, KPROC *process, KTHREAD *thread, THREAD_STARTUP_DRAFT *startup,
                         THREAD_STARTUP_STACK *stack, KPROC *logical_parent, BOOL same_process, THREAD_STARTUP_STACK *syscall_stack)
{
    KERNEL_TASK *task = slot;
    if (task->state != TASK_RESERVED) panic("clone binding without reservation");
    task->program = kproc_live_pd(process); task->pid = kproc_ref_pid(process);
    task->process = process; task->thread = thread; task->startup = startup;
    task->execution.stack.physical = stack->physical; task->execution.stack.top = stack->top;
    stack->physical = stack->top = 0;
    task->take_ownership = FALSE;
    if (same_process) {
        if (!ksched_spawn_allowed() || scheduler.current->process != process || !syscall_stack || !syscall_stack->physical)
            panic("sibling publication context changed");
        task->completion_owner = completion_task(scheduler.current);
        task->asynchronous = TRUE; /* root recycles TID slots, never zombies */
        task->execution.syscall_stack.physical = syscall_stack->physical;
        task->execution.syscall_stack.top = syscall_stack->top;
        syscall_stack->physical = syscall_stack->top = 0;
    } else if (logical_parent) {
        if (scheduler.current->process != logical_parent || kproc_live_pd(logical_parent) != run)
            panic("clone publication parent changed");
        task->asynchronous = TRUE; task->parent = run;
    }
#if CONF_WITH_X86_64_MEMTEST
    task->prepared_tid = kthread_tid(thread);
#endif
    kthread_clone_commit(thread, &task->execution, reclaim_task_execution);
    if (!kthread_start(thread)) panic("clone activation invariant");
    /* Still TASK_RESERVED: no dispatcher can observe the bound context. */
}
LONG ksched_clone_dispatch(void **slot, PROC_CLONE_TRANSACTION *transaction, LONG *status)
{
    *slot = NULL; /* consumed by the root commit, no fallible work remains */
    scheduler.pending_clone = transaction;
    return dispatch_group(1, status);
}
