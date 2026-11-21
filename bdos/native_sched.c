/* Cooperative kernel tasks, independent of the legacy AES scheduler.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "emutos.h"
#if CONF_WITH_FORK_CORE
#include "fork_parent.h"
#endif
#include "biosext.h"
#include "ksched.h"
#include "ksched_lifetime.h"
#include "ksched_wake.h"
#if CONF_WITH_NATIVE_PREEMPT
#include "ksched_quantum.h"
#endif
#include "kproc.h"
#include "native_sched_backend.h"
#include "kthread_context.h"
#include "kthread.h"
#include "thread_startup.h"
#include "proc_clone.h"
#include "process_observer.h"
#include "fs.h"
#include "callback_cpu.h"
#include "ksched_legacy_irq.h"
#include "kowner.h"
#include "proc.h"
#include <stddef.h>
#include "kheap.h"
#include "kprint.h"
#include "gemerror.h"
#include "string.h"
#if CONF_WITH_EXEC_REPLACE
#include "exec_handoff.h"
#include "exec_replace.h"
#include "exec_handoff_test.h"
#include "kaspace.h"
#endif
#if CONF_WITH_X86_64_MEMTEST
#include "../bios/machine/pc-x86_64/pc_x86_64_memtest.h"
#endif
extern PD *run;
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST
#include "region_selftest.h"
#endif

typedef enum { TASK_UNUSED, TASK_RESERVED, TASK_RETIRING, TASK_NEW, TASK_RUNNING, TASK_BLOCKED, TASK_DRAINING, TASK_DONE, TASK_ZOMBIE
    , TASK_DEFERRED
#if CONF_WITH_EXEC_REPLACE
    , TASK_REPLACING
#endif
} TASK_STATE;
struct ksched_nested;
typedef struct kernel_task {
    struct kernel_task *next_task; /* Stable arena link; never a lifetime reference. */
    /* Synchronous nested PROCESS executions hosted by this continuation. Each
     * frame owns its own PROCESS/THREAD identity; these fields never replace
     * the carrier's anchor, completion or PID state below. */
    struct ksched_nested *nested;          /* innermost associated owner, or NULL */
    struct ksched_nested *nested_prepared; /* private until its launcher entry */
    PD *program, *parent;
    UQUAD synchronous_wait_process;
    ULONG synchronous_waiter; /* non-recycled TID; no borrowed waiter stack */
    BOOL result_delivering; /* pin this zombie/slot across fallible user copy */
    struct kernel_task *completion_owner; /* owns one dependent reference; NULL is anchor */
    ULONG dependents; /* prevents anchor retirement/reuse until worker root reap */
    BOOL process_completed, result_published;
    KSCHED_ADMISSION completion; /* producer ownership, independent of arena position */
    UWORD result_slot;          /* cell in that admission, never a task-table index */
    UQUAD cleanup_serial, cleanup_ticket; /* Reject stale scope copies at a reused depth. */
    ULONG cleanup_depth; /* Owned by this validated execution, never its carrier child. */
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
#if CONF_WITH_EXEC_REPLACE
    KSCHED_EXEC_HANDOFF *replacement; /* private owning preparation */
    ULONG test_handoffs; /* transport evidence only, never a committed-exec epoch */
    ULONG exec_launches; /* committed Pexec(200) relaunches of this anchor */
    KSCHED_EXEC_HANDOFF *exec_retired; /* committed, awaiting off-root retirement */
#endif
    KTHREAD_CONTEXT execution;
    TASK_STATE state;
    BOOL (*ready)(void *);
    void *argument;
    KSCHED_WAIT_OBSERVATION observation; /* owner storage retained until resume */
    LONG result;
    ULONG usage[2];
} KERNEL_TASK;
/* One synchronous nested PROCESS execution on a carrier task (#713).
 *
 * The nested PROCESS has no schedulable continuation of its own: its user
 * image runs on its own syscall stack, entered and left through the existing
 * architecture launcher, whose frame lives on the enclosing execution's kernel
 * stack. This record is the scheduler's explicit, reversible association of
 * that logical execution with the carrier:
 *
 *   PREPARED  private to the carrier; owns the retained PROCESS identities and
 *             the trusted-factory THREAD. Nothing is runnable or observable.
 *   ACTIVE    innermost owner of the carrier. ksched_exit_thread() answers for
 *             this PROCESS only; every enclosing owner is suspended in its
 *             launcher and authenticates nobody.
 *   STOPPED   the launcher has left the nested syscall stack and detached the
 *             THREAD. The frame stays innermost, so no owner is authenticated
 *             while the common PROCESS finalizer runs.
 *
 * ksched_nested_leave() then restores the enclosing owner and reclaims the
 * THREAD on the enclosing kernel stack. The carrier's program/process/thread,
 * anchor, PID and completion admission are never rewritten. */
typedef enum { NESTED_PREPARED, NESTED_ACTIVE, NESTED_STOPPED, NESTED_RECLAIMING } NESTED_STATE;
typedef struct ksched_nested {
    struct ksched_nested *outer;  /* enclosing frame; NULL is the carrier's own owner */
    KERNEL_TASK *carrier;
    UQUAD carrier_execution;      /* non-recycled carrier identity at preparation */
    PD *program;                  /* nested basepage while its THREAD may execute */
    PD *enclosing;                /* loaded PROCESS to restore after the launcher */
    KPROC *process;               /* retained identity, including after exit */
    KPROC *enclosing_process;     /* retained identity of the enclosing owner */
    KTHREAD *thread;              /* owned until reclaimed off its syscall stack */
    KTHREAD *enclosing_thread;    /* borrowed: executing while its launcher is suspended */
    void *enclosing_launcher;     /* CPU-loaded launcher chain head at entry */
    ULONG tid;
    BOOL cancel_delivering;       /* this owner's termination hooks, never the carrier's */
    UQUAD cleanup_serial, cleanup_ticket;
    ULONG cleanup_depth;          /* enclosing cleanup scopes stay with their owner */
    NESTED_STATE state;
#if CONF_WITH_EXEC_REPLACE
    struct nested_exec *exec_preparing; /* this frame's Pexec(200), until its boundary */
#endif
} KSCHED_NESTED;
/* Shared queue/policy. Execution state is owned by the selected CPU below. */
static struct {
    BOOL active; /* queue exists, including while boot CPU root is parked */
    BOOL stopping; /* irreversible shutdown closes admission and dispatch */
    KERNEL_TASK *tasks, *next;
} scheduler;
static KSCHED_WAIT_SEQUENCE process_events;
#define ROOT_STACK_BYTES (16UL * 1024)
#define ROOT_STACK_GUARD 0x713a2001UL
typedef struct {
    PD *loaded_process; /* first: frozen run ABI aliases this CPU-owned slot */
    UWORD number; /* logical boot CPU 0; no additional CPU may dispatch yet */
    BOOL leaf_held, legacy_irq_held;
    KOWNER_CPU owners;
    uintptr_t legacy_irq_flags;
#if CONF_WITH_USER_CALLBACKS
    USER_CALLBACK_CPU callback; /* frame chain includes watchdog deadlines */
#endif
    FS_ERROR_STATE loaded_error; /* includes code/drive, not only longjmp registers */
    void *loaded_launcher; /* CPU-loaded nested chain, saved in each execution context. */
    KERNEL_TASK *current, *inflight;
    KSCHED_WAKE_STATE wake; /* permanent CPU-owned IRQ/idle handshake */
#if CONF_WITH_NATIVE_PREEMPT
    KSCHED_QUANTUM quantum;
#if CONF_WITH_PRIVATE_CLONE_TEST
    UWORD preempt_patterns;
    BOOL preempt_patterns_reported;
#endif
#endif
#if CONF_WITH_IRQ_RESTART
    UQUAD irq_selected, irq_last_selected, irq_user_observed, irq_last_user;
#endif
    BOOL entered, pump;
    ULONG budget;
    BOOL (*entry_ready)(void *);
    void *entry_argument; /* borrowed only during this CPU's live attachment */
    KPROC *entry_owner; /* retained identity, never a borrowed PD */
    NATIVE_SCHED_CONTEXT root; /* includes CPU-owned launcher/error snapshots */
    jmp_buf resume; /* persistent CPU root stack, never launcher storage */
    BOOL root_started, parked, waiter_attached;
    jmp_buf waiter_resume; /* valid only while this CPU's caller is attached */
    KSCHED_ADMISSION waiter; /* scoped consumer; independent producers own cells */
    PROC_CLONE_TRANSACTION *pending_clone;
    void *pending_slot;
#if CONF_WITH_EXEC_REPLACE
    KSCHED_EXEC_HANDOFF *pending_exec;
    struct nested_exec *nested_exec; /* committed Pexec(200) relaunch, masked to retire */
#endif
    ULONG stack[ROOT_STACK_BYTES/sizeof(ULONG)] __attribute__((aligned(16)));
} NATIVE_SCHED_CPU;
/* LTO may put a top-level .set in a different object from its target. The
 * preprocessed linker script resolves that alias after all LTO partitions;
 * retain its one target with stable hidden linkage. No second storage. */
#ifdef PTOS_LTO_RUN_ALIAS
NATIVE_SCHED_CPU boot_cpu __attribute__((used,externally_visible,visibility("hidden")));
#else
static NATIVE_SCHED_CPU boot_cpu;
#endif
/* TOS exports &run in OSHEADER. Keep that ABI without a second mutable mirror.
 * Only boot CPU dispatch exists; a future CPU selector must not export its
 * private slot as another system-wide TOS pointer or activate SMP here. */
typedef char loaded_process_abi_offset[offsetof(NATIVE_SCHED_CPU,loaded_process)==0 ? 1:-1];
#define KSCHED_STRING_(x) #x
#define KSCHED_STRING(x) KSCHED_STRING_(x)
/* An assembler alias leaves one complete storage object for instrumentation.
 * A C variable alias of pointer size registers a second, overlapping ASan
 * global and poisons the remaining CPU fields as the pointer's redzone. */
#ifndef PTOS_LTO_RUN_ALIAS
__asm__(".globl " KSCHED_STRING(__USER_LABEL_PREFIX__) "run\n"
        ".set " KSCHED_STRING(__USER_LABEL_PREFIX__) "run,"
                KSCHED_STRING(__USER_LABEL_PREFIX__) "boot_cpu");
#endif
#undef KSCHED_STRING
#undef KSCHED_STRING_
/* Deliberately boot-CPU-only. B1c/B2 must add architecture CPU-local selection;
 * callers must not infer SMP or asynchronous dispatch from this accessor. */
static NATIVE_SCHED_CPU *native_sched_cpu(void) { return &boot_cpu; }
#define execution_cpu (*native_sched_cpu())
KOWNER_CPU *kowner_cpu(void) { return &execution_cpu.owners; }
uintptr_t kowner_irq_save(void) { return native_sched_irq_save(); }
void kowner_irq_restore(uintptr_t flags) { native_sched_irq_restore(flags); }
#if CONF_WITH_USER_CALLBACKS
USER_CALLBACK_CPU *ksched_callback_cpu(void) { return &execution_cpu.callback; }
#endif
FS_ERROR_STATE *fs_error_current(void) { return &execution_cpu.loaded_error; }
void *ksched_loaded_launcher(void) { return execution_cpu.loaded_launcher; }
void ksched_set_loaded_launcher(void *launcher)
{
    if (execution_cpu.leaf_held) panic("scheduler: launcher change under runqueue leaf");
    execution_cpu.loaded_launcher=launcher;
}

/* B0 single-CPU leaf exclusion. This names/checks the critical boundary; IRQ
 * masking is not an SMP lock. Replace its exclusion with an IRQ-safe spinlock
 * before multiple CPUs may mutate the queue. No callbacks/destructors/copies,
 * allocations or continuation switches are allowed inside this boundary. */
typedef struct { uintptr_t flags; KOWNER_GUARD owner; } RUNQUEUE_GUARD;
static void runqueue_enter(RUNQUEUE_GUARD *guard)
{
    guard->flags=native_sched_irq_save();
    if(execution_cpu.leaf_held)panic("scheduler: recursive leaf exclusion");
    kowner_enter(&guard->owner,KOWNER_RUNQUEUE);
    execution_cpu.leaf_held=TRUE;
}
static void runqueue_leave(const RUNQUEUE_GUARD *guard)
{
    if(!execution_cpu.leaf_held)panic("scheduler: unbalanced leaf exclusion");
    execution_cpu.leaf_held=FALSE;
    kowner_leave(&guard->owner);
    native_sched_irq_restore(guard->flags);
}
static void runqueue_unlocked(void)
{
    if (execution_cpu.leaf_held) panic("scheduler: operation under leaf exclusion");
    kowner_assert_unlocked();
}
uintptr_t ksched_legacy_irq_disable(void)
{
    uintptr_t flags=native_sched_irq_save();
#if ARCH_ARM
    /* Preserve legacy disable_interrupts' FIQ mask as well as IRQ. */
    __asm__ volatile ("cpsid f" ::: "memory", "cc");
#endif
    runqueue_unlocked();
    if (execution_cpu.legacy_irq_held)
        panic("scheduler: nested legacy interrupt scope");
    execution_cpu.legacy_irq_flags=flags;
    execution_cpu.legacy_irq_held=TRUE;
    return flags;
}
void ksched_legacy_irq_enable(void)
{
    uintptr_t flags;
    runqueue_unlocked();
    if (!execution_cpu.legacy_irq_held)
        panic("scheduler: unmatched legacy interrupt scope");
    flags=execution_cpu.legacy_irq_flags;
    execution_cpu.legacy_irq_flags=0;
    execution_cpu.legacy_irq_held=FALSE;
    native_sched_irq_restore(flags);
}
void ksched_verify_switch_boundary(void)
{
    runqueue_unlocked();
    kowner_assert_unlocked();
    if (execution_cpu.legacy_irq_held)
        panic("scheduler: legacy interrupt scope cannot switch");
#if CONF_WITH_USER_CALLBACKS
    if (execution_cpu.callback.frame)
        panic("scheduler: callback continuation cannot switch");
#endif
}
/* Caller is root on its verified permanent stack. No current/IRQ continuation
 * can still execute on the acknowledged task's stack after this point. */
static void cpu_acknowledge_handoff(void)
{
    KERNEL_TASK *task;
    RUNQUEUE_GUARD guard; runqueue_enter(&guard);
    task=execution_cpu.inflight;
    if (task) {
        if (!task->execution.on_cpu || task->execution.owner_cpu!=execution_cpu.number)
            panic("scheduler: foreign CPU handoff");
#if CONF_WITH_USER_ASPACE
        task->execution.interrupted_user.valid=FALSE;
#endif
#if CONF_WITH_IRQ_RESTART
        interrupt_restart_root_ack(&task->execution);
#endif
        task->execution.on_cpu=FALSE;
        execution_cpu.inflight=NULL;
    }
    execution_cpu.current=NULL;
    runqueue_leave(&guard);
}
static void cpu_claim_task(KERNEL_TASK *task)
{
    RUNQUEUE_GUARD guard; runqueue_enter(&guard);
    if (execution_cpu.current || execution_cpu.inflight || task->execution.on_cpu)
        panic("scheduler: duplicate CPU ownership");
    task->execution.owner_cpu=execution_cpu.number;
    task->execution.on_cpu=TRUE;
    execution_cpu.current=execution_cpu.inflight=task;
#if CONF_WITH_NATIVE_PREEMPT
    ksched_quantum_reset(&execution_cpu.quantum);
#endif
#if CONF_WITH_IRQ_RESTART
    if (execution_cpu.irq_last_selected!=task->execution.identity) {
        if (execution_cpu.irq_selected==~(UQUAD)0) panic("IRQ restart: selection epoch exhausted");
        execution_cpu.irq_selected++;
        execution_cpu.irq_last_selected=task->execution.identity;
    }
#endif
    runqueue_leave(&guard);
}
#if CONF_WITH_IRQ_RESTART
UQUAD ksched_irq_restart_selected(void) { return execution_cpu.irq_selected; }
UQUAD ksched_irq_restart_user_observed(void) { return execution_cpu.irq_user_observed; }
void ksched_irq_restart_observe_user(UQUAD identity)
{
    /* Architecture USER IRQ/trap observation is masked and authenticated. */
    if (!execution_cpu.current || execution_cpu.current->execution.identity!=identity)
        panic("IRQ restart: foreign USER observation");
    if (execution_cpu.irq_last_user!=identity) {
        if (execution_cpu.irq_user_observed==~(UQUAD)0) panic("IRQ restart: USER epoch exhausted");
        execution_cpu.irq_user_observed++;
        execution_cpu.irq_last_user=identity;
    }
}
#endif
#if CONF_WITH_NATIVE_PREEMPT
void ksched_preempt_tick(void)
{
    KERNEL_TASK *task=execution_cpu.current;
    /* Called only by the existing 200 Hz timer with local IRQs masked. It
     * may interrupt kernel work: record expiry, never dispatch or inspect a
     * borrowed kernel frame. A due request survives every refused IRQ tail. */
    ksched_quantum_tick(&execution_cpu.quantum,
        !scheduler.stopping && task && task->execution.on_cpu && task->state==TASK_RUNNING);
}
BOOL ksched_preempt_capture_needed(void)
{
    return ksched_quantum_capture_needed(&execution_cpu.quantum) || ksched_cancellation_pending();
}
BOOL ksched_preempt_due(void)
{
    return execution_cpu.quantum.pending || ksched_cancellation_pending();
}
#if CONF_WITH_PRIVATE_CLONE_TEST
LONG ksched_preempt_test_patterns(BOOL reset)
{
    uintptr_t flags=native_sched_irq_save();
    LONG result=execution_cpu.preempt_patterns;
    if(reset) {
        execution_cpu.preempt_patterns=0;execution_cpu.preempt_patterns_reported=FALSE;
    }
    native_sched_irq_restore(flags);return result;
}
void ksched_preempt_probe_return(UWORD pattern)
{
    execution_cpu.preempt_patterns|=pattern;
}
#endif
#endif
#if CONF_WITH_CUIS_WAIT_TEST
static BOOL always_ready(void *argument);
LONG ksched_test_wait_admission(ULONG tid)
{
    KTHREAD *caller=ksched_exit_thread(run);
    KERNEL_TASK *task;
    LONG result=ESRCH;
    RUNQUEUE_GUARD guard;

    if (!caller) return ESRCH;
    if (!tid) return kthread_tid(caller);

    runqueue_enter(&guard);
    for (task=scheduler.tasks; task; task=task->next_task) {
        if (task->process!=kthread_process(caller) || task->nested ||
            !task->thread || kthread_tid(task->thread)!=tid) continue;
        /* Syield parks on an always-ready predicate. It does not admit
         * the input operation that the observer is waiting for. */
        result=task->state==TASK_BLOCKED && task->cancel_boundary &&
            task->ready && task->ready!=always_ready;
        break;
    }
    runqueue_leave(&guard);
    return result;
}
#endif
void ksched_shutdown_begin(void)
{
    RUNQUEUE_GUARD guard; runqueue_enter(&guard);
    scheduler.stopping=TRUE;
    runqueue_leave(&guard);
}
/* Permanent kernel storage, outside procmem and all PROCESS/ASPACE owners.
 * The parked CPU record keeps no PD/launcher reference; a permanent kernel
 * controller and retained admission records own those independent lifetimes.
 * Detached cooperative attachments never own the runqueue lifetime. */
static void check_root_stack(void)
{
    uintptr_t marker;
    if ((uintptr_t)&marker < (uintptr_t)execution_cpu.stack + sizeof(ULONG) ||
        (uintptr_t)&marker >= ((uintptr_t)execution_cpu.stack + sizeof(execution_cpu.stack)) ||
        execution_cpu.stack[0] != ROOT_STACK_GUARD)
        panic("scheduler: invalid persistent root stack");
}
/* The initial array holds bootstrap records; each task owns result provenance.
 * Additional slots are allocated without moving any live record:
 * completion_owner, THREAD backends and reservations point into these records.
 * IRQ exclusion/cooperative serialization protects traversal today, not SMP. */
static void clear_task(KERNEL_TASK *task)
{
    KERNEL_TASK *next;
    KSCHED_ADMISSION completion;
    RUNQUEUE_GUARD guard; runqueue_enter(&guard);
    if (task->result_delivering || task->dependents || task->completion_owner || task->execution.on_cpu ||
        task->nested || task->nested_prepared)
        panic("scheduler: clearing referenced task");
    completion=task->completion;
    task->completion.identity=0;
    task->state=TASK_RESERVED; /* do not reuse while a destructor can reenter */
    runqueue_leave(&guard);
    ksched_admission_release(&completion);
#if CONF_WITH_IRQ_RESTART
    interrupt_restart_dispose(&task->execution);
#endif
    runqueue_enter(&guard);
    /* Final release may reenter admission or root trimming. The reserved
     * record survives, but its successor link can legitimately change. */
    next=task->next_task;
    memset(task, 0, sizeof(*task));
    task->next_task = next;
    runqueue_leave(&guard);
}
static void initialize_task_table(void)
{
    UWORD i;
    memset(scheduler.tasks, 0, KSCHED_LIMIT * sizeof(*scheduler.tasks));
    for (i = 0; i < KSCHED_LIMIT - 1; i++)
        scheduler.tasks[i].next_task = &scheduler.tasks[i+1];
}
static BOOL ensure_task_table(void)
{
    if (scheduler.tasks) return TRUE;
    scheduler.tasks=kalloc(KSCHED_LIMIT*sizeof(*scheduler.tasks));
    if (!scheduler.tasks) return FALSE;
    initialize_task_table(); scheduler.active=TRUE;
    scheduler.next=scheduler.tasks;
    return TRUE;
}
static BOOL table_empty(void)
{
    KERNEL_TASK *task;
    for (task=scheduler.tasks;task;task=task->next_task)
        if (task->state!=TASK_UNUSED || task->dependents || task->completion_owner ||
            task->completion.identity || task->thread || task->process || task->execution.on_cpu) return FALSE;
    return TRUE;
}
static KERNEL_TASK *async_tasks(void) { return scheduler.tasks; }
static KERNEL_TASK *reserve_record(void)
{
    KERNEL_TASK *task,*last=NULL;
    if (!ensure_task_table()) return NULL;
    for(task=scheduler.tasks;task;last=task,task=task->next_task)
        if(task->state==TASK_UNUSED && !task->execution.on_cpu && !task->completion.identity && !task->dependents && !task->completion_owner) break;
    if(!task) {
        task=kalloc(sizeof(*task)); if(!task) return NULL;
        memset(task,0,sizeof(*task)); last->next_task=task;
    }
    clear_task(task); task->state=TASK_RESERVED;
#if CONF_WITH_NATIVE_PREEMPT
    if (interrupt_restart_reserve(&task->execution.irq_restart)) {
        clear_task(task);return NULL;
    }
#endif
    return task;
}
/* Transfer public ownership before any callback/last-reference release.
 * Reentrant admission cannot inherit or be erased by the old caller's handle. */
static void abandon_preparation(void)
{
    KPROC *owner;
    KSCHED_ADMISSION waiter;
    RUNQUEUE_GUARD guard; runqueue_enter(&guard);
    owner=execution_cpu.entry_owner; execution_cpu.entry_owner=NULL;
    waiter=execution_cpu.waiter; execution_cpu.waiter.identity=0;
    runqueue_leave(&guard);
    ksched_admission_abandon(&waiter);
    ksched_admission_release(&waiter);
    if(owner) kproc_release(owner);
}
/* One serialized unmanaged admission may prepare at a time. Producer refs and
 * the entry identity are acquired before image adoption or output publication. */
static BOOL prepare_batch(UWORD count,KERNEL_TASK **slots)
{
    UWORD i;
    KPROC *owner;
    runqueue_unlocked();
    if(scheduler.stopping || execution_cpu.entered || execution_cpu.waiter_attached || execution_cpu.waiter.identity || !count || count>KSCHED_LIMIT) return FALSE;
    owner=kproc_retain(run);
    if ((run && kproc_pid(run) && !owner) || ksched_admission_create(owner,count,&execution_cpu.waiter)!=E_OK) {
        kproc_release(owner); return FALSE;
    }
    execution_cpu.entry_owner=owner;
    for(i=0;i<count;i++) {
        slots[i]=reserve_record();
        if(!slots[i] || !ksched_admission_retain(&execution_cpu.waiter,&slots[i]->completion)) {
            if(slots[i]) clear_task(slots[i]);
            while(i) clear_task(slots[--i]);
            abandon_preparation();
            return FALSE;
        }
        slots[i]->result_slot=i;
    }
    return TRUE;
}
static void free_task_table(void)
{
    KERNEL_TASK *base, *task;
    KSCHED_ADMISSION waiter={0};
    KPROC *owner=NULL;
    RUNQUEUE_GUARD guard; runqueue_enter(&guard);
    if(!scheduler.tasks || !table_empty()) { runqueue_leave(&guard); return; }
    base=scheduler.tasks;
    scheduler.tasks=scheduler.next=NULL;
    scheduler.active=FALSE;
    if (!execution_cpu.waiter_attached) {
        waiter=execution_cpu.waiter; execution_cpu.waiter.identity=0;
        owner=execution_cpu.entry_owner; execution_cpu.entry_owner=NULL;
    }
    runqueue_leave(&guard);
    /* Globally unreachable before any release/free can reenter admission.
     * No cursor into the live runqueue is carried across a destructor. */
    for (task=base;task;task=task->next_task)
        ksched_admission_release(&task->completion);
    task=base[KSCHED_LIMIT-1].next_task;
    while(task) {
        KERNEL_TASK *next=task->next_task;
        kfree(task); task=next;
    }
    kfree(base);
    ksched_admission_abandon(&waiter);
    ksched_admission_release(&waiter);
    kproc_release(owner);
}
/* Root-only metadata trimming. No execution, dependent, result or private
 * reservation may reference an UNUSED node; scheduler.next is repaired first. */
static void trim_unused_records(void)
{
    KERNEL_TASK **link=&scheduler.tasks[KSCHED_LIMIT-1].next_task;
    while(*link) {
        RUNQUEUE_GUARD guard; runqueue_enter(&guard);
        KERNEL_TASK *task=*link;
        if(task->state==TASK_UNUSED && !task->thread && !task->process && !task->dependents &&
            !task->completion_owner && !task->completion.identity && !task->execution.on_cpu) {
            if(scheduler.next==task) scheduler.next=task->next_task ? task->next_task : scheduler.tasks;
            *link=task->next_task;
            runqueue_leave(&guard);
            kfree(task);
        } else {
            link=&task->next_task;
            runqueue_leave(&guard);
        }
    }
}
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST
/* Native lifecycle assertions, not a modeled context switch. The real user
 * PROCESS retires between begin and root reap; only retained identities remain. */
static struct {
    KPROC *owner; /* borrowed from its record */
    KSCHED_ADMISSION waiter, producer, other;
    BOOL completed, tested;
} lifetime_probe;
static void lifetime_probe_begin(KERNEL_TASK *task)
{
    LONG outputs[2]={-71,-72};
    /* A boot shell can remain live for the entire suite. Select a separately
     * completing async PROCESS, not that shell or a SAME_PROCESS sibling. */
    if (!task->asynchronous || task->completion_owner ||
        lifetime_probe.owner || lifetime_probe.tested) return;
    if (task->program==ksched_controller_pd() || kproc_pid(ksched_controller_pd()) ||
        ksched_admission_create(task->process,2,&lifetime_probe.waiter) ||
        !ksched_admission_retain(&lifetime_probe.waiter,&lifetime_probe.producer) ||
        ksched_admission_create(task->process,1,&lifetime_probe.other))
        panic("scheduler: native lifetime preparation");
    lifetime_probe.owner=task->process;
    if (lifetime_probe.waiter.identity==lifetime_probe.other.identity ||
        !ksched_admission_complete(&lifetime_probe.producer,0,71) ||
        ksched_admission_take(&lifetime_probe.waiter,outputs,2)!=EAGAIN ||
        outputs[0]!=-71 || outputs[1]!=-72)
        panic("scheduler: premature native result delivery");
}
static void lifetime_probe_exiting(KPROC *owner,UWORD status)
{
    LONG outputs[2]={-71,-72};
    if (!owner || lifetime_probe.owner!=owner) return;
    if (lifetime_probe.completed ||
        !ksched_admission_abandoned(&lifetime_probe.waiter) ||
        !ksched_admission_complete(&lifetime_probe.producer,1,status) ||
        ksched_admission_complete(&lifetime_probe.producer,1,status) ||
        !ksched_admission_complete(&lifetime_probe.other,0,status) ||
        ksched_admission_take(&lifetime_probe.waiter,outputs,2)!=EACCDN ||
        outputs[0]!=-71 || outputs[1]!=-72)
        panic("scheduler: native abandonment/completion");
    lifetime_probe.completed=TRUE;
    ksched_admission_release(&lifetime_probe.waiter);
}
static void lifetime_probe_reap(KERNEL_TASK *task)
{
    if (!task->process || task->process!=lifetime_probe.owner ||
        task->state==TASK_DRAINING) return;
    if (lifetime_probe.completed) {
        if (kproc_live_pd(lifetime_probe.owner) ||
            ksched_admission_owner_pd(&lifetime_probe.producer) ||
            !ksched_controller_pd()) panic("scheduler: retired native owner retained resources");
        lifetime_probe.tested=TRUE;
        kcprintf("native scheduler durable controller/completion abandonment: PASS\n");
    }
    ksched_admission_abandon(&lifetime_probe.waiter);
    ksched_admission_release(&lifetime_probe.waiter);
    ksched_admission_release(&lifetime_probe.producer);
    ksched_admission_release(&lifetime_probe.other);
    lifetime_probe.owner=NULL;
}
#endif
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST
static volatile BOOL irq_wake_probe_pending;
static KSCHED_WAIT_SEQUENCE generation_probe;
static BOOL generation_pending;
#endif
void ksched_irq_notify(void)
{
    uintptr_t flags=native_sched_irq_save();
    ksched_wake_publish(&execution_cpu.wake);
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST
    /* Only armed by verified root test code immediately before real idle.
     * The producer is an actual maskable IRQ, never a simulated user frame. */
    if (irq_wake_probe_pending) {
        irq_wake_probe_pending=FALSE;
        ksched_sequence_advance(&generation_probe);
        generation_pending=FALSE;
    }
#endif
    native_sched_irq_restore(flags);
}
void ksched_wake_sequence(KSCHED_WAIT_SEQUENCE *source)
{
    uintptr_t flags=native_sched_irq_save();
    if (!source) panic("scheduler: missing wake owner");
    ksched_sequence_advance(source);
    ksched_wake_publish(&execution_cpu.wake);
    native_sched_irq_restore(flags);
}
static UQUAD next_identity;
#if CONF_WITH_EXEC_REPLACE
typedef enum { HANDOFF_PREPARING, HANDOFF_PREPARED, HANDOFF_SEALED,
               HANDOFF_TRANSFERRED, HANDOFF_QUIESCENT, HANDOFF_INSTALLED } HANDOFF_STATE;
struct ksched_exec_handoff {
    KERNEL_TASK *task;
    KPROC *process;
    KTHREAD *thread;
    PD *pd;
    KASPACE *space;
    ASPACE_ROOT root;
    UQUAD number, execution, next_execution;
    ULONG tid;
    WORD pid;
    void *launcher;
    PADDR old_syscall;
    THREAD_STARTUP_DRAFT *startup, *retired_startup;
    KASPACE_LEASE *hold, *retired_hold;
    KTHREAD_STACK retired_stack;
    KSCHED_CLEANUP_SCOPE cleanup;
    HANDOFF_STATE state;
    uintptr_t irq_flags;
    /* Pexec(200) only: the image bundle and the root it commits */
    KPROC_EXEC_REPLACEMENT *receipt;
    KPROC_EXEC_IMAGE *bundle;
    KASPACE *target, *old_thread_space;
    BOOL rebound, committed;
#if CONF_WITH_NATIVE_PREEMPT
    IRQ_RESTART fresh_restart;
#endif
};

static BOOL exec_handoff_matches(const KSCHED_EXEC_HANDOFF *tx)
{
    KERNEL_TASK *task = execution_cpu.current;
    void *launcher = NULL;
    PADDR syscall = 0;
    return tx && task && tx->task == task && task->replacement == tx &&
        !scheduler.stopping && task->state == TASK_RUNNING &&
        task == execution_cpu.inflight && task->execution.on_cpu &&
        task->execution.owner_cpu == execution_cpu.number &&
        !task->nested && !task->nested_prepared && !task->completion_owner && !task->dependents &&
        !task->ready && !task->argument && !task->observation.source &&
        /* Pexec(200) keeps this task and PID, so an exact waiter's claim (an
         * anchored Pexec(0) parent's, #1015) stays valid across it. */
        (tx->receipt || (!task->synchronous_waiter && !task->synchronous_wait_process)) &&
        !task->process_completed && !task->result_published && !task->exited &&
        !task->execution.syscall_stack.physical &&
        tx->process == task->process && tx->pd == run && task->program == run &&
        kproc_live_pd(tx->process) == run && kproc_ref_number(tx->process) == tx->number &&
        kproc_ref_pid(tx->process) == tx->pid && task->pid == tx->pid &&
        task->thread == tx->thread && kthread_tid(tx->thread) == tx->tid &&
        kthread_exec_handoff_ready(tx->thread, &task->execution) &&
        tx->execution == task->execution.identity &&
        tx->space == kthread_aspace(tx->thread) &&
        tx->root == kproc_user_root(run) &&
        kproc_exec_handoff_allowed(tx->process, tx->thread, tx->receipt != NULL) &&
        !ksched_cancellation_pending() && tx->cleanup.active &&
        task->cleanup_depth == (tx->receipt ? 2UL : 1UL) &&
        tx->cleanup.depth == (tx->receipt ? 2UL : 1UL) &&
        (!tx->receipt || (!kproc_exec_replacement_validate(tx->receipt) &&
                          kproc_exec_image_sealable(tx->bundle) &&
                          kproc_exec_replacement_selected(tx->receipt) == tx->target)) &&
        task->cleanup_ticket == tx->cleanup.ticket &&
        proc_exec_stack_snapshot(&launcher, &syscall) &&
        launcher == tx->launcher && syscall == tx->old_syscall;
}

static void exec_handoff_abort(KSCHED_EXEC_HANDOFF **handle)
{
    KSCHED_EXEC_HANDOFF *tx = *handle;
    if (!tx) return;
    if (tx->state >= HANDOFF_TRANSFERRED || !execution_cpu.current ||
        execution_cpu.current != tx->task || tx->task->replacement != tx ||
        ksched_context_id() != tx->execution) panic("exec handoff: foreign abort");
    *handle = NULL;
#if CONF_WITH_NATIVE_PREEMPT
    interrupt_restart_retire(&tx->fresh_restart);
#endif
    thread_startup_abort(&tx->startup);
    kaspace_lease_release(&tx->hold);
    kaspace_put(tx->target);
    kaspace_put(tx->space);
    kproc_release(tx->process);
    tx->task->replacement = NULL;
    ksched_cleanup_end(&tx->cleanup);
    kfree(tx);
}

/* Only this exact CPU-root transaction can replace an executing THREAD's
 * durable stack hold. The THREAD stays attached and executing throughout. */
BOOL ksched_exec_handoff_quiescent(const KSCHED_EXEC_HANDOFF *tx,
                                  const KTHREAD *thread, const void *execution)
{
    return tx && execution_cpu.pending_exec == tx && tx->state == HANDOFF_QUIESCENT &&
        !execution_cpu.current && !execution_cpu.inflight && !tx->task->execution.on_cpu &&
        tx->task->replacement == tx && tx->task->state == TASK_REPLACING &&
        tx->thread == thread && tx->task->thread == thread &&
        execution == &tx->task->execution && tx->task->process == tx->process &&
        tx->task->execution.identity == tx->execution &&
        kproc_ref_number(tx->process) == tx->number &&
        kproc_live_pd(tx->process) == tx->pd && kproc_ref_pid(tx->process) == tx->pid &&
        kthread_tid(thread) == tx->tid &&
        kthread_aspace(thread) == (tx->rebound ? tx->target : tx->space) &&
        kproc_active_space(tx->process) == (tx->committed ? tx->target : tx->space);
}
BOOL ksched_exec_handoff_replacing(const KSCHED_EXEC_HANDOFF *tx)
{
    return tx && tx->bundle && execution_cpu.pending_exec == tx;
}
BOOL ksched_exec_handoff_retiring(const THREAD_STARTUP_DRAFT *startup)
{
    KSCHED_EXEC_HANDOFF *tx = execution_cpu.pending_exec;
    return tx && tx->state == HANDOFF_INSTALLED && startup == tx->retired_startup &&
        !execution_cpu.current && !execution_cpu.inflight && !tx->task->execution.on_cpu &&
        tx->task->replacement == tx && tx->task->state == TASK_REPLACING;
}
BOOL ksched_exec_handoff_launch(PD *pd)
{
    KERNEL_TASK *task = execution_cpu.current;
    return task && task->program == pd && (task->test_handoffs || task->exec_launches) && task->startup &&
        task->state == TASK_RUNNING && task->execution.on_cpu &&
        !task->replacement && !task->completion_owner && kthread_is_executing(task->thread);
}

LONG ksched_exec_handoff_query(VADDR output)
{
    KERNEL_TASK *task = execution_cpu.current;
    EXEC_HANDOFF_TEST_STATE result;
    UQUAD number, root;
    if (!ksched_spawn_allowed() || task->nested || task->program != run) return EACCDN;
    if (output >= USER_VA_LIMIT) return EIMBA;
    number = kproc_ref_number(task->process); root = kproc_user_root(run);
    result.process[0] = (ULONG)number; result.process[1] = (ULONG)(number >> 32);
    result.execution[0] = (ULONG)task->execution.identity;
    result.execution[1] = (ULONG)(task->execution.identity >> 32);
    result.root[0] = (ULONG)root; result.root[1] = (ULONG)(root >> 32);
    result.pid = task->pid; result.tid = kthread_tid(task->thread);
    result.handoffs = task->test_handoffs;
    return kproc_copy_to_user_resident(output, &result, sizeof(result)) ? E_OK : EIMBA;
}

LONG ksched_exec_handoff_test(const THREAD_STARTUP_ARGS *args, UWORD failure)
{
    KERNEL_TASK *task = execution_cpu.current;
    KSCHED_EXEC_HANDOFF *tx = NULL;
    LONG error = EACCDN;
    void *launcher = NULL;
    PADDR syscall = 0;
    UQUAD test_identity = 0;
    BOOL identity_injected = FALSE;
    if (!args || failure > EXEC_HANDOFF_FAILURES) return EINVAL;
    if (execution_cpu.leaf_held || execution_cpu.legacy_irq_held) return EACCDN;
#if CONF_WITH_USER_CALLBACKS
    if (execution_cpu.callback.frame) return EACCDN;
#endif
#if CONF_WITH_PTERMGRP
    if (task && task->cancel_delivering) return EACCDN;
#endif
    if (!ksched_spawn_allowed() || task->nested || task->nested_prepared ||
        task->replacement || task->cleanup_depth || task->completion_owner || task->dependents ||
        execution_cpu.pending_exec || !kproc_exec_handoff_allowed(task->process, task->thread, FALSE) ||
        !proc_exec_stack_snapshot(&launcher, &syscall)) return EACCDN;
    if (next_identity == ~(UQUAD)0 || task->test_handoffs == ~(ULONG)0) return EAGAIN;
    if (failure == 1) return ENSMEM;
    tx = kalloc(sizeof(*tx));
    if (!tx) return ENSMEM;
    memset(tx, 0, sizeof(*tx));
    tx->task = task; tx->thread = task->thread; tx->pd = run;
    tx->process = kproc_retain_live(task->process);
    tx->space = kaspace_share(kthread_aspace(task->thread));
    tx->number = kproc_ref_number(task->process); tx->pid = task->pid;
    tx->tid = kthread_tid(task->thread); tx->execution = task->execution.identity;
    tx->root = kproc_user_root(run); tx->launcher = launcher; tx->old_syscall = syscall;
    task->replacement = tx;
    ksched_cleanup_begin(&tx->cleanup);
    if (!tx->process || !tx->space || failure == 2) { error = ENSMEM; goto failed; }
#if CONF_WITH_NATIVE_PREEMPT
    error=interrupt_restart_reserve(&tx->fresh_restart);
    if (error) goto failed;
#endif
    if (!exec_handoff_matches(tx)) goto failed;
    if (failure == 3) { error = ENSMEM; goto failed; }
    error = thread_startup_prepare_handoff(tx->space, args, &tx->startup);
    if (error) goto failed;
    if (failure == 4) { error = ENSMEM; goto failed; }
    error = thread_startup_retain_stack(tx->startup, &tx->hold);
    if (error) goto failed;
    if (failure == 5) { error = ENSMEM; goto failed; }
    error = thread_startup_run_preflight(tx->startup);
    if (error) goto failed;
    tx->state = HANDOFF_PREPARED;
    if (failure == 6) { error = ENSMEM; goto failed; }
    /* A stale identity must fail the real matcher, not a modeled producer. */
    if (failure == 7) {
        tx->execution++;
        if (exec_handoff_matches(tx)) panic("exec handoff: stale execution admitted");
        tx->execution--; error = EACCDN; goto failed;
    }
    if (failure == 8) {
        if (ksched_exec_handoff_test(args, 0) != EACCDN)
            panic("exec handoff: competing preparation admitted");
        error = EACCDN; goto failed;
    }
    /* Preparation protects cancellation but not mapping revocation. Check
     * ownership again after a genuine cooperative continuation handoff. */
    ksched_yield();
    if (!exec_handoff_matches(tx)) { error = EACCDN; goto failed; }
    if (failure == 9) { error = ENSMEM; goto failed; }
    tx->irq_flags = native_sched_irq_save();
    if (!exec_handoff_matches(tx)) { error = EACCDN; goto unmask_failed; }
    if (failure == 11) {
        test_identity = next_identity; next_identity = ~(UQUAD)0; identity_injected = TRUE;
    }
    /* Another task can reserve the final identity while preparation yields.
     * Capacity must be rechecked in this final no-yield publication window. */
    if (next_identity == ~(UQUAD)0) { error = EAGAIN; goto unmask_failed; }
    error = thread_startup_seal(tx->startup);
    if (error) goto unmask_failed;
    tx->state = HANDOFF_SEALED;
    if (failure == 10) { error = ENSMEM; goto unmask_failed; }
    tx->next_execution = ++next_identity;
    /* No-fail boundary: transfer the launcher's retirement right, close only
     * our exact cleanup scope, and abandon both old C continuations. */
    proc_exec_stack_take(tx->launcher, tx->old_syscall);
    ksched_cleanup_end(&tx->cleanup);
    tx->state = HANDOFF_TRANSFERRED;
    execution_cpu.pending_exec = tx;
    task->state = TASK_REPLACING;
    native_sched_save(&task->execution.cpu);
    longjmp(execution_cpu.resume, 1);
unmask_failed:
    if (identity_injected) next_identity = test_identity;
    native_sched_irq_restore(tx->irq_flags);
failed:
    exec_handoff_abort(&tx);
    return error;
}

#if CONF_WITH_EXEC_REPLACEMENT_TEST
static UWORD replace_fail_stage;
void ksched_exec_replace_test_fail_after(UWORD stage) { replace_fail_stage = stage; }
static BOOL replace_injected(UWORD stage)
{
    if (replace_fail_stage != stage) return FALSE;
    replace_fail_stage = 0; return TRUE;
}
#else
#define replace_injected(stage) FALSE
#endif

static LONG exec_replace_nested(KERNEL_TASK *, KPROC_EXEC_REPLACEMENT **, KPROC_EXEC_IMAGE **);
/* Public Pexec(200) commit through the #988 transport. The caller owns the
 * reversible receipt (whose cleanup scope is this execution's only one) and a
 * prepared bundle. Every error leaves both with the caller, which aborts them
 * and still runs the unchanged original program; success never returns. */
LONG ksched_exec_replace(KPROC_EXEC_REPLACEMENT **receipt, KPROC_EXEC_IMAGE **bundle)
{
    KERNEL_TASK *task = execution_cpu.current;
    KSCHED_EXEC_HANDOFF *tx = NULL;
    LONG error = EACCDN;
    void *launcher = NULL;
    PADDR syscall = 0;
    UQUAD saved_identity = 0;
    BOOL identity_injected = FALSE;
    if (!receipt || !*receipt || !bundle || !*bundle) return EINVAL;
    if (execution_cpu.leaf_held || execution_cpu.legacy_irq_held) return EACCDN;
#if CONF_WITH_USER_CALLBACKS
    if (execution_cpu.callback.frame) return EACCDN;
#endif
    /* A synchronous nested PROCESS has no continuation of its own: it is
     * relaunched through its launcher frame instead (exec_replace_nested). */
    if (task && task->nested && scheduler.active) return exec_replace_nested(task, receipt, bundle);
#if CONF_WITH_PTERMGRP
    if (task && task->cancel_delivering) return EACCDN;
#endif
    if (!ksched_spawn_allowed() || task->nested || task->nested_prepared ||
        task->replacement || task->exec_retired || task->cleanup_depth != 1 ||
        task->completion_owner || task->dependents || execution_cpu.pending_exec ||
        !kproc_exec_handoff_allowed(task->process, task->thread, TRUE) ||
        kproc_exec_replacement_validate(*receipt) || !kproc_exec_image_sealable(*bundle) ||
        !proc_exec_stack_snapshot(&launcher, &syscall)) return EACCDN;
    if (next_identity == ~(UQUAD)0 || task->exec_launches == ~(ULONG)0) return EAGAIN;
    if (replace_injected(1)) return ENSMEM;
    tx = kalloc(sizeof(*tx));
    if (!tx) return ENSMEM;
    memset(tx, 0, sizeof(*tx));
    tx->task = task; tx->thread = task->thread; tx->pd = run;
    tx->process = kproc_retain_live(task->process);
    tx->space = kaspace_share(kthread_aspace(task->thread));
    tx->target = kaspace_share(kproc_exec_replacement_selected(*receipt));
    tx->number = kproc_ref_number(task->process); tx->pid = task->pid;
    tx->tid = kthread_tid(task->thread); tx->execution = task->execution.identity;
    tx->root = kproc_user_root(run); tx->launcher = launcher; tx->old_syscall = syscall;
    tx->receipt = *receipt; tx->bundle = *bundle;
    task->replacement = tx;
    ksched_cleanup_begin(&tx->cleanup);
    if (!tx->process || !tx->space || !tx->target || replace_injected(2)) { error = ENSMEM; goto failed; }
#if CONF_WITH_NATIVE_PREEMPT
    error=interrupt_restart_reserve(&tx->fresh_restart);
    if (error) goto failed;
#endif
    if (!exec_handoff_matches(tx)) goto failed;
    tx->startup = kproc_exec_image_take_startup(tx->bundle);
    if (!tx->startup) goto failed;
    if (replace_injected(3)) { error = ENSMEM; goto failed; }
    /* No durable stack hold: like any launched program, the new image runs
     * on its PROCESS's own private stack, not a user heap reservation. The
     * draft's startup lease covers its resident top until user entry. */
    if (replace_injected(4)) { error = ENSMEM; goto failed; }
    error = thread_startup_run_preflight(tx->startup);
    if (error) goto failed;
    tx->state = HANDOFF_PREPARED;
    if (replace_injected(5)) { error = ENSMEM; goto failed; }
    if (replace_injected(6)) {
        /* A stale identity must fail the real matcher. */
        tx->execution++;
        if (exec_handoff_matches(tx)) panic("exec replace: stale execution admitted");
        tx->execution--; error = EACCDN; goto failed;
    }
    if (replace_injected(7)) {
        if (ksched_exec_replace(receipt, bundle) != EACCDN)
            panic("exec replace: competing commit admitted");
        error = EACCDN; goto failed;
    }
    /* Preparation defers cancellation, not revocation: revalidate after a
     * genuine cooperative handoff. */
    ksched_yield();
    if (!exec_handoff_matches(tx)) { error = EACCDN; goto failed; }
    if (replace_injected(8)) { error = ENSMEM; goto failed; }
    tx->irq_flags = native_sched_irq_save();
    if (!exec_handoff_matches(tx)) { error = EACCDN; goto unmask_failed; }
    if (replace_injected(9)) {
        saved_identity = next_identity; next_identity = ~(UQUAD)0; identity_injected = TRUE;
    }
    if (next_identity == ~(UQUAD)0) { error = EAGAIN; goto unmask_failed; }
    error = thread_startup_seal(tx->startup);
    if (error) goto unmask_failed;
    tx->state = HANDOFF_SEALED;
    if (replace_injected(10)) { error = ENSMEM; goto unmask_failed; }
    tx->next_execution = ++next_identity;
    /* No-fail boundary: the handles now belong to this transaction. */
    *receipt = NULL; *bundle = NULL;
    proc_exec_stack_take(tx->launcher, tx->old_syscall);
    ksched_cleanup_end(&tx->cleanup);
    kproc_exec_replacement_transfer(tx->receipt);
    tx->state = HANDOFF_TRANSFERRED;
    execution_cpu.pending_exec = tx;
    task->state = TASK_REPLACING;
    native_sched_save(&task->execution.cpu);
    longjmp(execution_cpu.resume, 1);
unmask_failed:
    if (identity_injected) next_identity = saved_identity;
    native_sched_irq_restore(tx->irq_flags);
failed:
    exec_handoff_abort(&tx);
    return error;
}

/* Pexec(200) gives the PROCESS the new program's basepage: every scheduler
 * reference to the old one moves with it. No allocation or callback. */
static void exec_rekey_scheduler(PD *old, PD *new)
{
    KERNEL_TASK *task;
    struct ksched_nested *frame;
    if (run == old) run = new;
    if (execution_cpu.pending_exec && execution_cpu.pending_exec->pd == old)
        execution_cpu.pending_exec->pd = new;
    for (task = scheduler.tasks; task; task = task->next_task) {
        if (task->program == old) task->program = new;
        if (task->parent == old) task->parent = new;
        if (task->execution.cpu.pd == old) task->execution.cpu.pd = new;
        for (frame = task->nested; frame; frame = frame->outer) {
            if (frame->program == old) frame->program = new;
            if (frame->enclosing == old) frame->enclosing = new;
        }
        if (task->nested_prepared) {
            if (task->nested_prepared->program == old) task->nested_prepared->program = new;
            if (task->nested_prepared->enclosing == old) task->nested_prepared->enclosing = new;
        }
    }
}

/* Called only AFTER cpu_acknowledge_handoff(), on the permanent root stack.
 * No callbacks, allocations, user copies or image policy are used to install
 * the fresh context. All destruction follows installation outside the leaf. */
static void exec_handoff_on_root(void)
{
    uintptr_t marker;
    KSCHED_EXEC_HANDOFF *tx = execution_cpu.pending_exec;
    THREAD_STARTUP_STACK_OWNERS stacks = { {0,0}, {0,0} };
    KERNEL_TASK *task;
#if CONF_WITH_IRQ_RESTART
    IRQ_RESTART retired_restart;
#endif
    if (!tx) return;
    check_root_stack();
    task = tx->task;
    if (tx->state != HANDOFF_TRANSFERRED || execution_cpu.current || execution_cpu.inflight ||
        task->execution.on_cpu || task->replacement != tx || task->state != TASK_REPLACING ||
        task->cleanup_depth || task->cleanup_ticket || execution_cpu.loaded_launcher)
        panic("exec handoff: missing root acknowledgment");
    tx->state = HANDOFF_QUIESCENT;
    if (!thread_startup_take_stacks(tx->startup, &stacks) ||
        stacks.syscall.physical == tx->old_syscall ||
        stacks.scheduler.physical == tx->old_syscall ||
        stacks.syscall.physical == task->execution.stack.physical ||
        stacks.scheduler.physical == task->execution.stack.physical ||
        stacks.syscall.physical == stacks.scheduler.physical)
        panic("exec handoff: fresh stack ownership changed");
    if (tx->bundle) {
        /* Pexec(200) commit: the same THREAD and PROCESS adopt the sealed
         * bundle's root and image. No allocation, callback or failure. */
        KASPACE *space = kaspace_share(tx->target);
        if (!space) panic("exec handoff: committed root reference lost");
        kthread_exec_rebind(tx->thread, tx, &space);
        tx->old_thread_space = space;
        tx->rebound = TRUE;
        {
            PD *old = tx->pd;
            kproc_exec_image_commit(tx->bundle);
            exec_rekey_scheduler(old, kproc_live_pd(tx->process));
        }
        tx->committed = TRUE;
        kproc_exec_replacement_commit(tx->receipt);
    }
    kthread_exec_handoff_stack(tx->thread, tx, &tx->hold, &tx->retired_hold);
    tx->retired_startup = task->startup;
    tx->retired_stack = kthread_stack_detach(&task->execution.stack);
#if CONF_WITH_IRQ_RESTART
    /* Move execution-resource owners before erasing the context. Destructors
     * must wait until the fresh context and startup stores are installed. */
    interrupt_restart_detach(&task->execution,&retired_restart);
#endif
    /* Never copy a jmp_buf, C continuation or old CPU return image. */
    memset(&task->execution, 0, sizeof(task->execution));
#if CONF_WITH_NATIVE_PREEMPT
    task->execution.irq_restart=tx->fresh_restart;
    memset(&tx->fresh_restart,0,sizeof(tx->fresh_restart));
#endif
    task->execution.identity = tx->next_execution;
    task->execution.stack.physical = stacks.scheduler.physical;
    task->execution.stack.top = stacks.scheduler.top;
    task->execution.syscall_stack.physical = stacks.syscall.physical;
    task->execution.syscall_stack.top = stacks.syscall.top;
    task->startup = tx->startup; tx->startup = NULL;
    thread_startup_commit_stores(task->startup, tx->pid, tx->tid);
    thread_startup_publish(task->startup);
    task->ready = NULL; task->argument = NULL;
    memset(&task->observation, 0, sizeof(task->observation));
    tx->state = HANDOFF_INSTALLED;
    native_sched_irq_restore(tx->irq_flags);
    ksched_waiter_retire(tx->process, tx->tid, tx->execution);
    if (kstack_contains(tx->old_syscall, (uintptr_t)&marker) ||
        kstack_contains(tx->retired_stack.physical, (uintptr_t)&marker))
        panic("exec handoff: retiring an active kernel stack");
    kstack_free(tx->old_syscall); tx->old_syscall = 0;
    kthread_stack_release(&tx->retired_stack);
    thread_startup_reclaim(&tx->retired_startup);
    kaspace_lease_release(&tx->retired_hold);
#if CONF_WITH_IRQ_RESTART
    interrupt_restart_retire(&retired_restart);
#endif
    task->replacement = NULL;
    execution_cpu.pending_exec = NULL;
    if (tx->bundle) {
        /* Exactly one committed-exec generation per commit, then one generic
         * readiness publication. Waiters whose predicates did not change go
         * back to sleep; nothing completes, reaps or reports a status. */
        uintptr_t flags = native_sched_irq_save();
        kproc_exec_generation_advance(tx->process);
        ksched_wake_publish(&execution_cpu.wake);
        native_sched_irq_restore(flags);
        task->exec_launches++;
        /* The old root, image container and environment are retired by the
         * relaunched execution on its own fresh stack (exec_replace_retire). */
        task->exec_retired = tx;
    } else {
        kaspace_put(tx->space); tx->space = NULL;
        kproc_release(tx->process); tx->process = NULL;
        task->test_handoffs++;
        kfree(tx);
        /* Deliberately no KPROC exec epoch or readiness publication: this
         * token owns same-image transport only, not an image bundle. */
    }
    native_sched_irq_disable();
    task->state = TASK_NEW;
}

/* Shared off-stack retirement of a committed Pexec(200): the old root and
 * memberships, then the container, which now holds the old program's
 * basepage: its disposal frees the old basepage, TPA, environment and every
 * other block that basepage owns, releases the old program's directory
 * searches and GUI clients, and closes nothing the PROCESS kept (its files
 * moved to the new basepage at commit). A procmem block still pinned by a
 * dying root is reclaimed on its last unpin, so the order of the two
 * retirements is not load-bearing.
 * Cleanup callbacks may yield but must not unwind: an escaping disk error
 * would land in an abandoned syscall frame, so it is fatal, as in
 * proc_finalize_exit(). */
static void exec_retire_common(KPROC_EXEC_REPLACEMENT **receipt, KPROC_EXEC_IMAGE **bundle,
                               KASPACE **old_thread_space, KASPACE **old_space)
{
    KSCHED_CLEANUP_SCOPE scope;
    FS_ERROR_STATE saved_error = *fs_error_current();
    if (setjmp(errbuf)) {
        *fs_error_current() = saved_error;
        panic("exec replace: retirement cleanup escaped");
    }
    memset(&scope, 0, sizeof(scope));
    ksched_cleanup_begin(&scope);
    kproc_exec_image_retire(bundle);
    kproc_exec_replacement_retire(receipt);
    kaspace_put(*old_thread_space); *old_thread_space = NULL;
    kaspace_put(*old_space); *old_space = NULL;
    ksched_cleanup_end(&scope);
    *fs_error_current() = saved_error;
}

/* First action of a relaunched Pexec(200) execution, on its fresh scheduler
 * stack before user entry: nothing here runs on a retired stack or root. */
static void exec_replace_retire(KERNEL_TASK *task)
{
    KSCHED_EXEC_HANDOFF *tx = task->exec_retired;
    if (!tx) return;
    if (!tx->committed || tx->task != task || task->replacement || !task->startup ||
        task->program != tx->pd || kproc_active_space(tx->process) != tx->target)
        panic("exec replace: foreign retirement");
    task->exec_retired = NULL;
    run = tx->pd;
    exec_retire_common(&tx->receipt, &tx->bundle, &tx->old_thread_space, &tx->space);
    kaspace_put(tx->target);
    kproc_release(tx->process);
    kfree(tx);
}
#endif
static KSCHED_WAIT_RESULT wait_observed(BOOL (*ready)(void *), void *argument,
                                        const KSCHED_WAIT_OBSERVATION *observation);
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST
/* Kernel-only managed-ticket assertion over real user continuations.
 * Public background admission is tested separately by the AUTO programs. */
static struct {
    KSCHED_ADMISSION producer;
    KSCHED_WAITER abandoned;
    UQUAD execution;
    BOOL pending, resumed, reaped;
} wait_probe;
static BOOL generation_never_ready(void *owner)
{ return owner!=&generation_probe; }
static void wait_probe_on_root(void)
{
    if (generation_pending) {
        UQUAD before=execution_cpu.wake.sequence;
        UWORD unrelated=0;
        KSCHED_WAIT_SEQUENCE owner={0};
        /* Inject publication after a completed scan but before idle admission.
         * The production handshake must refuse the stale sleep token. */
        ksched_wake_sequence(&owner);
        if (ksched_wake_arm(&execution_cpu.wake,before) || execution_cpu.wake.armed)
            panic("scheduler: wake lost before idle admission");
        before=execution_cpu.wake.sequence;
        irq_wake_probe_pending=TRUE;
        if (!ksched_wake_arm(&execution_cpu.wake,before))
            panic("scheduler: native IRQ wake admission");
        /* Test-only bounded idle at root, even if a peer is runnable. No
         * dispatcher policy changes: production idle remains after full scan. */
        do {
            native_sched_idle();
            /* STOP/WFI/HLT may return for an unrelated interrupt whose entry
             * is not an owner-notification hook (Amiga VBL precedes CIA).
             * Production root rescans after such a spurious wake. Keep this
             * probe blocked until its actual owner IRQ, with a finite bound. */
            if (irq_wake_probe_pending && ++unrelated==64)
                panic("scheduler: owner IRQ missing after unrelated wakes");
        } while (irq_wake_probe_pending);
        if (irq_wake_probe_pending || generation_pending ||
            execution_cpu.wake.sequence==before || execution_cpu.wake.armed)
            panic("scheduler: real IRQ failed to release idle admission");
        ksched_wake_disarm(&execution_cpu.wake);
    }
    if (!wait_probe.pending) return;
    if (!ksched_admission_complete(&wait_probe.producer,1,82))
        panic("scheduler: native wait completion");
    wait_probe.pending=FALSE;
    ksched_admission_release(&wait_probe.producer);
}
/* Independent of the parent-blocked ticket test: x32's kernel memory suite
 * drives real ring-3 yields without a public Pwait parent task. */
static BOOL sequence_probed;
static void sequence_probe_on_yield(void)
{
    KERNEL_TASK *task=execution_cpu.current;
    KSCHED_ADMISSION admission={0};
    KSCHED_WAITER ticket={0};
    KSCHED_WAIT_OBSERVATION observation;
    KTHREAD *thread;
    void *context;
    ULONG tid;
    LONG output=-91;
    if (sequence_probed || !task || task->program!=run ||
        !(thread=ksched_exit_thread(run)) || ksched_cancellation_pending()) return;
    sequence_probed=TRUE; /* the blocked probe must not recurse through a peer */
    tid=kthread_tid(thread);
    if (ksched_admission_create(task->process,1,&admission) ||
        ksched_waiter_attach(&admission,task->process,tid,task->execution.identity,1,&ticket))
        panic("scheduler: native sequence preparation");
    context=ksched_waiter_context(&ticket);
    ksched_waiter_observe(context,&observation);
    if (ksched_waiter_ready(context) || !ksched_admission_complete(&admission,0,91) ||
        wait_observed(ksched_waiter_ready,context,&observation)!=KSCHED_WAIT_READY ||
        execution_cpu.current!=task || task->state!=TASK_RUNNING || task->ready || task->argument ||
        ksched_waiter_take(&ticket,task->process,tid,task->execution.identity,&output,1) || output!=91)
        panic("scheduler: native sequence check/block retry");
    ksched_waiter_detach(&ticket); ksched_admission_release(&admission);
    {
        uintptr_t flags=native_sched_irq_save();
        ksched_sequence_observe(&generation_probe,&observation);
        generation_pending=TRUE;
        if (ksched_wait_cancelable_observed(generation_never_ready,&generation_probe,&observation)!=KSCHED_WAIT_READY ||
            generation_pending || !ksched_sequence_changed(&observation) ||
            generation_never_ready(&generation_probe) || execution_cpu.current!=task ||
            task->state!=TASK_RUNNING || task->observation.source || task->ready || task->argument)
            panic("scheduler: native owner generation wake/detach");
        native_sched_irq_restore(flags);
    }
    sequence_probed=TRUE;
    kcprintf("native completion sequence check/block retry: PASS\n");
    kcprintf("native owner generation wake with false predicate: PASS\n");
    kcprintf("native IRQ wake/idle admission and blocked-owner resume: PASS\n");
}
static void wait_probe_on_yield(void)
{
    KERNEL_TASK *task=execution_cpu.current, *parent;
    KSCHED_ADMISSION admission={0};
    LONG results[2]={-81,-82};
    KTHREAD *thread;
    ULONG tid;
    if (wait_probe.execution || !task || task->nested || !task->asynchronous || task->completion_owner || !task->parent) return;
    for (parent=scheduler.tasks;parent;parent=parent->next_task)
        if (parent->program==task->parent && parent->state==TASK_BLOCKED) break;
    if (!parent || !(thread=ksched_exit_thread(run))) return;
    if (kproc_parent_pid(task->program)!=kproc_pid(parent->program))
        panic("scheduler: logical parent depends on global admission");
    tid=kthread_tid(thread); wait_probe.execution=ksched_context_id();
    if (ksched_admission_create(task->process,2,&admission) ||
        !ksched_admission_complete(&admission,0,81) ||
        !ksched_admission_retain(&admission,&wait_probe.producer))
        panic("scheduler: native waiter preparation");
    wait_probe.pending=TRUE;
    if (ksched_result_wait(&admission,results,2) || results[0]!=81 || results[1]!=82 ||
        execution_cpu.current!=task || ksched_context_id()!=wait_probe.execution ||
        ksched_exit_thread(run)!=thread || kthread_tid(thread)!=tid ||
        parent->state!=TASK_BLOCKED || wait_probe.pending)
        panic("scheduler: native TID waiter resumed wrong execution");
    ksched_admission_release(&admission);
    /* Simulate an abandoned owning handle without a borrowed stack pointer.
     * Root must retire it after this real execution has stopped. */
    if (ksched_admission_create(task->process,1,&admission) ||
        ksched_waiter_attach(&admission,task->process,tid,wait_probe.execution,1,&wait_probe.abandoned))
        panic("scheduler: native abandoned waiter preparation");
    ksched_admission_release(&admission);
    wait_probe.resumed=TRUE;
    kcprintf("native managed TID admission wait/resume: PASS\n");
}
#endif
#if CONF_WITH_X86_64_MEMTEST
static BOOL clone_masked;
BOOL ksched_test_clone_masked(void) { return clone_masked; }
#endif
static void publish_task(void **slot, TASK_STATE state);
static KERNEL_TASK *completion_task(KERNEL_TASK *task)
{
    return task->completion_owner ? task->completion_owner : task;
}
PD *ksched_launch_controller(PD *pd)
{
    return scheduler.active && execution_cpu.current && execution_cpu.current->program==pd ?
        ksched_controller_pd() : NULL;
}
BOOL ksched_sibling_launch(PD *pd)
{
    return ksched_spawn_allowed() && execution_cpu.current->completion_owner && execution_cpu.current->program == pd;
}
THREAD_STARTUP_DRAFT *ksched_thread_startup(PD *pd)
{
    KERNEL_TASK *task = execution_cpu.current;
    return scheduler.active && task && task->program == pd ? task->startup : NULL;
}
BOOL ksched_take_syscall_stack(PD *pd, THREAD_STARTUP_STACK *owner)
{
    KERNEL_TASK *task = execution_cpu.current;
    KTHREAD_STACK stack;
    if (!owner || owner->physical || owner->top ||
        (!ksched_sibling_launch(pd)
#if CONF_WITH_EXEC_REPLACE
         && !ksched_exec_handoff_launch(pd)
#endif
        )) return FALSE;
    stack = kthread_stack_detach(&task->execution.syscall_stack);
    owner->physical = stack.physical;
    owner->top = stack.top;
    return stack.physical != 0;
}


BOOL ksched_spawn_allowed(void)
{
    return !scheduler.stopping && scheduler.active && execution_cpu.current && execution_cpu.current->state == TASK_RUNNING;
}
BOOL ksched_active(void) { return scheduler.active; }
BOOL ksched_dispatching(void) { return execution_cpu.entered; }
static BOOL admission_ready(void *handle);
static LONG enter_root(BOOL pump,BOOL (*ready)(void *),void *argument);
BOOL ksched_async_allowed(void)
{
    KPROC *owner; BOOL allowed;
    if (scheduler.stopping) return FALSE;
    if (ksched_spawn_allowed()) return TRUE;
    if (execution_cpu.entered || execution_cpu.waiter_attached || execution_cpu.waiter.identity) return FALSE;
    owner=kproc_retain(run); allowed=owner && kproc_live_pd(owner)==run;
    kproc_release(owner); return allowed;
}

KTHREAD_CONTEXT *ksched_interrupt_target(PD *pd, struct kaspace **space, ULONG *tid)
{
    KERNEL_TASK *task=execution_cpu.current;
#if CONF_WITH_USER_CALLBACKS
    if (execution_cpu.callback.frame) return NULL;
#endif
    if (scheduler.stopping || !scheduler.active || kowner_locked() || execution_cpu.leaf_held || execution_cpu.legacy_irq_held ||
        !task || execution_cpu.inflight!=task || !task->execution.on_cpu ||
        task->execution.owner_cpu!=execution_cpu.number || task->state!=TASK_RUNNING ||
        task->nested || task->program!=pd || !kthread_is_executing(task->thread)) return NULL;
    *space=kthread_aspace(task->thread);
    if (!*space || *space!=kproc_active_space(task->process)) return NULL;
    *tid=kthread_tid(task->thread);
    return &task->execution;
}

KTHREAD *ksched_exit_thread(PD *pd)
{
    KERNEL_TASK *task = execution_cpu.current;
    KSCHED_NESTED *frame;
    if (!scheduler.active || !task || task->state != TASK_RUNNING)
        return NULL;
    frame = task->nested;
    if (frame) {
        /* Only the innermost associated execution owns this continuation.
         * Every enclosing owner is suspended in its launcher, and a stopped
         * frame authenticates nobody until that launcher restores its owner. */
        if (frame->state != NESTED_ACTIVE || frame->program != pd ||
            !kthread_is_executing(frame->thread))
            return NULL;
        return frame->thread;
    }
    if (task->program != pd || !kthread_is_executing(task->thread))
        return NULL;
    return task->thread;
}
/* Identity and termination-hook state of the validated logical owner. The
 * carrier's own PROCESS fields are never used on behalf of a nested child. */
static KPROC *owner_process(const KERNEL_TASK *task)
{
    return task->nested ? task->nested->process : task->process;
}
static ULONG *owner_cleanup_depth(KERNEL_TASK *task)
{
    return task->nested ? &task->nested->cleanup_depth : &task->cleanup_depth;
}
static UQUAD *owner_cleanup_serial(KERNEL_TASK *task)
{
    return task->nested ? &task->nested->cleanup_serial : &task->cleanup_serial;
}
static UQUAD *owner_cleanup_ticket(KERNEL_TASK *task)
{
    return task->nested ? &task->nested->cleanup_ticket : &task->cleanup_ticket;
}
#if CONF_WITH_PTERMGRP
static BOOL *owner_cancel_delivering(KERNEL_TASK *task)
{
    return task->nested ? &task->nested->cancel_delivering : &task->cancel_delivering;
}
#endif

BOOL ksched_cleanup_active(void)
{
    return ksched_exit_thread(run) && *owner_cleanup_depth(execution_cpu.current) != 0;
}
void ksched_cleanup_begin(KSCHED_CLEANUP_SCOPE *scope)
{
    KTHREAD *thread;
    uintptr_t flags = native_sched_irq_save();
    if (!scope || scope->active) panic("cleanup scope entered twice");
    memset(scope,0,sizeof(*scope));
    scope->caller=run; scope->active=TRUE;
    thread=ksched_exit_thread(run);
    if (thread) {
        KERNEL_TASK *task=execution_cpu.current;
        if (!task->execution.identity || !owner_process(task) || owner_process(task)!=kthread_process(thread) ||
            *owner_cleanup_depth(task)==0xffffffffUL || *owner_cleanup_serial(task)==~(UQUAD)0) panic("cleanup execution ownership invalid");
        scope->thread=thread; scope->process=owner_process(task);
        scope->execution=task->execution.identity; scope->tid=kthread_tid(thread);
        scope->depth=++*owner_cleanup_depth(task);
        scope->parent_ticket=*owner_cleanup_ticket(task);
        scope->ticket=++*owner_cleanup_serial(task);
        *owner_cleanup_ticket(task)=scope->ticket;
    }
    native_sched_irq_restore(flags);
}
void ksched_cleanup_end(KSCHED_CLEANUP_SCOPE *scope)
{
    KTHREAD *thread;
    uintptr_t flags = native_sched_irq_save();
    if (!scope || !scope->active || scope->caller!=run) panic("cleanup scope release ownership invalid");
    thread=ksched_exit_thread(run);
    if (scope->thread) {
        KERNEL_TASK *task=execution_cpu.current;
        if (thread!=scope->thread || !task || owner_process(task)!=scope->process ||
            kthread_process(thread)!=scope->process || kthread_tid(thread)!=scope->tid ||
            task->execution.identity!=scope->execution || *owner_cleanup_depth(task)!=scope->depth ||
            *owner_cleanup_ticket(task)!=scope->ticket || !scope->ticket || !scope->depth) panic("cleanup scope release is not nested in its execution");
        --*owner_cleanup_depth(task);
        *owner_cleanup_ticket(task)=scope->parent_ticket;
    } else if (thread || scope->process || scope->tid || scope->depth || scope->execution ||
               scope->ticket || scope->parent_ticket) panic("unmanaged cleanup changed execution");
    memset(scope,0,sizeof(*scope));
    native_sched_irq_restore(flags);
}

#if CONF_WITH_PTERMGRP
/* This runs on a restored task/syscall stack, never on scheduler root. A
 * nested child owns a different launch chain and must return normally. */
void ksched_cancel_point(void)
{
    KTHREAD *thread = ksched_exit_thread(run);
    UWORD status;
    if (scheduler.stopping) return;
    if (thread && !ksched_cleanup_active() && !*owner_cancel_delivering(execution_cpu.current) &&
        kproc_group_status(kthread_process(thread), &status)) xterm(status);
}
void ksched_enter_exit(PD *pd)
{
    if (ksched_exit_thread(pd)) *owner_cancel_delivering(execution_cpu.current) = TRUE;
}
LONG ksched_termgrp(UWORD status)
{
    KTHREAD *thread = ksched_exit_thread(run);
    uintptr_t flags;
    if (!thread) return EINVFN;
    /* A nested synchronous PROCESS has one THREAD and no scheduler anchor of
     * its own. It keeps the documented unassociated-caller result. */
    if (execution_cpu.current->nested) return EINVFN;
    if (scheduler.stopping || execution_cpu.current->cancel_delivering || ksched_cleanup_active()) return EACCDN;
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

/* ------------------------------------------------------------------------
 * Nested synchronous PROCESS execution (#713).
 *
 * A managed THREAD that calls synchronous Pexec stays suspended in the
 * architecture launcher on its own kernel stack while the child runs. The
 * child therefore has no schedulable continuation of its own; what it needs
 * is an authentic THREAD identity, and an owner that is restored when it has
 * gone. Preparation is fallible and private, entry and exit are no-fail.
 * ---------------------------------------------------------------------- */
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST || CONF_WITH_PRIVATE_CLONE_TEST
/* Native assertions/injection for the regression kernels only. Every counted
 * transition has already passed the production invariant checks below. */
static struct {
    ULONG prepared, entered, stopped, reclaimed, cancelled, refused, untracked;
    ULONG deepest;
    ULONG last_pid, last_tid, last_depth; /* most recently entered execution */
    ULONG completed, cancel_reaped;
    uintptr_t last_pd;
    UWORD fail_stage;
    BOOL untracked_next, reported;
    BOOL nest_synchronous; /* keep synchronous Pexec on the nested carrier (#1015) */
} nested_probe;
static BOOL nested_injected(UWORD stage)
{
    if (nested_probe.fail_stage != stage) return FALSE;
    nested_probe.fail_stage = 0;
    nested_probe.refused++;
    return TRUE;
}
BOOL ksched_test_nested_injected(UWORD stage) { return nested_injected(stage); }
static ULONG nested_depth(const KERNEL_TASK *task)
{
    const KSCHED_NESTED *frame;
    ULONG depth = 0;
    for (frame = task ? task->nested : NULL; frame; frame = frame->outer) depth++;
    return depth;
}
/* The refused-handoff fixtures explicitly select a launch without a THREAD:
 * a managed THREAD may never fall back to in-context teardown. */
static BOOL nested_keep_untracked(void)
{
    if (!nested_probe.untracked_next) return FALSE;
    nested_probe.untracked_next = FALSE;
    nested_probe.untracked++;
    return TRUE;
}
LONG ksched_test_nested(UWORD selector)
{
    switch (selector) {
    case 0: return (LONG)nested_depth(execution_cpu.current);
    case 1: return (LONG)nested_probe.prepared;
    case 2: return (LONG)nested_probe.entered;
    case 3: return (LONG)nested_probe.stopped;
    case 4: return (LONG)nested_probe.reclaimed;
    case 5: return (LONG)nested_probe.cancelled;
    case 6: return (LONG)nested_probe.refused;
    case 7: return (LONG)nested_probe.untracked;
    case 8: return (LONG)nested_probe.deepest;
    case 9: return (LONG)(nested_probe.prepared - nested_probe.reclaimed - nested_probe.cancelled);
    case 10: return (LONG)nested_probe.last_pid;
    case 11: return (LONG)nested_probe.last_tid;
    case 12: return (LONG)nested_probe.last_depth;
    case 13: return (LONG)(nested_probe.last_pd & 0x7fffffffUL);
    case 14: {
        KERNEL_TASK *task;
        ULONG count = 0;
        for (task = scheduler.tasks; task; task = task->next_task)
            if (task->execution.stack.physical) count++;
        return (LONG)count;
    }
    case 15: return (LONG)nested_probe.completed;
    case 0x21: return (LONG)nested_probe.cancel_reaped;
    case 0x20: nested_probe.untracked_next = TRUE; return E_OK;
    case 0x22: nested_probe.nest_synchronous = TRUE; return E_OK;
    case 0x23: nested_probe.nest_synchronous = FALSE; return E_OK;
    default:
        if (selector >= 0x10 && selector <= 0x16) {
            nested_probe.fail_stage = selector - 0x10; return E_OK;
        }
        return EINVFN;
    }
}
void ksched_test_nested_published(KPROC *process)
{
    PROCESS_COMPLETION completion;
    if (!kproc_ref_terminal(process) || kproc_ref_has_basepage(process) ||
        !kproc_ref_completion(process, &completion) || completion.state != PROCESS_COMPLETED ||
        !(completion.order_low || completion.order_high))
        panic("scheduler: nested observer missed stopped resource release");
    nested_probe.completed++;
}
#else
#define nested_injected(stage) FALSE
#define nested_keep_untracked() FALSE
#endif
static void nested_note_stopped(KSCHED_NESTED *frame)
{
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST || CONF_WITH_PRIVATE_CLONE_TEST
    nested_probe.stopped++;
#endif
    UNUSED(frame);
}
/* A nested THREAD owns no scheduler stack. Its syscall stack belongs to the
 * architecture launcher, which released it before this can be reached. */
static void reclaim_nested_execution(void *resource)
{
    KSCHED_NESTED *frame = resource;
    if (frame->state != NESTED_RECLAIMING || frame->carrier->nested == frame ||
        frame->carrier->nested_prepared == frame)
        panic("scheduler: reclaiming an associated nested execution");
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST || CONF_WITH_PRIVATE_CLONE_TEST
    if (kproc_ref_terminal(frame->process))
        panic("scheduler: nested observer ready before THREAD reclamation");
    if (frame->tid && !frame->program) {
        if (!ksched_cleanup_active()) panic("scheduler: unprotected nested reclamation");
#if CONF_WITH_PTERMGRP && CONF_WITH_PRIVATE_CLONE_TEST
        if (ksched_cancellation_pending()) {
            KTHREAD *enclosing = ksched_exit_thread(run);
            /* Exercise a real scheduler handoff in a destructor while the
             * restored owner is already cancelled. No partial release may
             * be interrupted at the resumed GEMDOS checkpoint. */
            ksched_yield();
            ksched_cancel_point();
            if (ksched_exit_thread(run) != enclosing || !ksched_cleanup_active() ||
                !ksched_cancellation_pending())
                panic("scheduler: cancellation interrupted nested reclamation");
            nested_probe.cancel_reaped++;
        }
#endif
    }
#endif
}
static void release_nested_frame(KSCHED_NESTED *frame)
{
    KTHREAD *thread = frame->thread;
    frame->state = NESTED_RECLAIMING;
    frame->thread = NULL;
    if (thread) kthread_destroy(thread); /* stopped or never started */
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST || CONF_WITH_PRIVATE_CLONE_TEST
    if (kproc_ref_terminal(frame->process))
        panic("scheduler: nested observer ready during resource reclamation");
#endif
    kproc_release(frame->process);
    kproc_release(frame->enclosing_process);
    kfree(frame);
}
/* A synchronous Pexec(0) from an authentic managed THREAD with an address
 * space of its own runs its child as an anchor (#1015), as FreeMiNT does:
 * the loader launches it like Pexec(100), as an independent PROCESS with its
 * own scheduler task that can own dependents (Pclone, Pthjoin, Ptermgrp,
 * Pfork/Pvfork), and the calling THREAD waits for that exact child
 * (ksched_wait_exact). Every other launch keeps the nested carrier below or
 * the THREAD-less one: the kernel/ROM, user callbacks, untracked launches. */
BOOL ksched_anchor_launch_allowed(void)
{
    KERNEL_TASK *task = execution_cpu.current;
    if (scheduler.stopping || !scheduler.active || !task || task->state != TASK_RUNNING)
        return FALSE;
#if CONF_WITH_USER_CALLBACKS
    if (execution_cpu.callback.frame) return FALSE;
#endif
    if (!ksched_exit_thread(run) || !kproc_user_aspace(run)) return FALSE;
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST || CONF_WITH_PRIVATE_CLONE_TEST
    /* Only the nested-carrier suites and their explicitly selected subjects. */
    if (nested_probe.nest_synchronous || nested_probe.untracked_next) return FALSE;
#endif
    return ksched_async_allowed();
}
/* E_OK: `pd` now has a private prepared association, consumed by
 * ksched_nested_enter() or ksched_nested_cancel(). KSCHED_NESTED_UNMANAGED:
 * the loaded execution is not an authentic managed THREAD (kernel-code
 * process, user callback, untracked child, shutdown), so the launch keeps its
 * existing THREAD-less carrier contract. A negative result leaves no state. */
LONG ksched_nested_prepare(PD *pd)
{
    KERNEL_TASK *task = execution_cpu.current;
    KSCHED_NESTED *frame;
    KTHREAD *enclosing;
    KSCHED_CLEANUP_SCOPE scope = {0};
    runqueue_unlocked();
    if (scheduler.stopping || !scheduler.active || !task || task->state != TASK_RUNNING)
        return KSCHED_NESTED_UNMANAGED;
#if CONF_WITH_USER_CALLBACKS
    if (execution_cpu.callback.frame) return KSCHED_NESTED_UNMANAGED;
#endif
    enclosing = ksched_exit_thread(run);
    if (!enclosing) return KSCHED_NESTED_UNMANAGED;
    if (nested_keep_untracked()) return KSCHED_NESTED_UNMANAGED;
    /* One private preparation per carrier. Enclosing cleanup scopes stay
     * with the owner that opened them; the child starts with no tickets. */
    if (task->nested_prepared) return EACCDN;
    if (nested_injected(1)) return ENSMEM;
    ksched_cleanup_begin(&scope);
    frame = kalloc(sizeof(*frame));
    if (!frame) { ksched_cleanup_end(&scope); return ENSMEM; }
    memset(frame, 0, sizeof(*frame));
    frame->state = NESTED_PREPARED;
    frame->carrier = task;
    frame->carrier_execution = task->execution.identity;
    frame->program = pd;
    frame->enclosing = run;
    frame->enclosing_thread = enclosing;
    frame->process = nested_injected(2) ? NULL : kproc_retain(pd);
    frame->enclosing_process = kproc_retain(run);
    if (!frame->process || !frame->enclosing_process ||
        frame->enclosing_process != kthread_process(enclosing) || frame->process == frame->enclosing_process)
        goto failed;
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST || CONF_WITH_PRIVATE_CLONE_TEST
    if (nested_injected(3)) kthread_test_fail_attach_after(1);
    if (nested_injected(4)) kproc_test_fail_kernel_stack(TRUE);
    if (nested_injected(5)) kproc_test_fail_thread_space(TRUE);
#endif
    /* Address space, syscall stack and the trusted-factory initial THREAD.
     * It retains the PROCESS and its KASPACE; nothing is runnable. This is
     * the last fallible step, and it precedes the irreversible image move. */
    {
        BOOL prepared = kproc_prepare_user_thread(pd, run, &frame->thread);
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST || CONF_WITH_PRIVATE_CLONE_TEST
        /* An injection that this launch never reached must not hit a peer. */
        kthread_test_fail_attach_after(0);
        kproc_test_fail_kernel_stack(FALSE);
        kproc_test_fail_thread_space(FALSE);
#endif
        if (!prepared) goto failed;
    }
    if (!kthread_set_execution(frame->thread, frame, reclaim_nested_execution) ||
        !kproc_thread_start_allowed(frame->process) ||
        kproc_active_space(frame->process) != kthread_aspace(frame->thread) ||
        kthread_process(frame->thread) != frame->process)
        panic("scheduler: prepared nested THREAD is not startable");
    frame->tid = kthread_tid(frame->thread);
    task->nested_prepared = frame;
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST || CONF_WITH_PRIVATE_CLONE_TEST
    nested_probe.prepared++;
#endif
    ksched_cleanup_end(&scope);
    return E_OK;
failed:
    release_nested_frame(frame);
    ksched_cleanup_end(&scope);
    return ENSMEM;
}
/* Roll back a preparation whose launch was abandoned before entry. The image
 * and PROCESS stay with the caller's existing failure path. */
void ksched_nested_cancel(PD *pd)
{
    KERNEL_TASK *task = execution_cpu.current;
    KSCHED_NESTED *frame;
    KSCHED_CLEANUP_SCOPE scope = {0};
    runqueue_unlocked();
    if (!scheduler.active || !task || !(frame = task->nested_prepared) || frame->program != pd)
        return;
    if (frame->state != NESTED_PREPARED || frame->carrier != task)
        panic("scheduler: cancelling a published nested execution");
    task->nested_prepared = NULL;
    ksched_cleanup_begin(&scope);
    release_nested_frame(frame);
    ksched_cleanup_end(&scope);
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST || CONF_WITH_PRIVATE_CLONE_TEST
    nested_probe.cancelled++;
#endif
}
/* The launcher is about to load `pd`. No allocation, callback or failure:
 * the enclosing owner and its launcher chain are recorded, the THREAD starts,
 * and the frame becomes the carrier's innermost owner in one leaf section. */
void ksched_nested_enter(PD *pd)
{
    KERNEL_TASK *task = execution_cpu.current;
    KSCHED_NESTED *frame;
    RUNQUEUE_GUARD guard;
    if (!scheduler.active || !task || !(frame = task->nested_prepared) || frame->program != pd)
        return; /* task-owned, kernel-code or untracked launch */
    if (task->state != TASK_RUNNING || frame->state != NESTED_PREPARED || frame->carrier != task ||
        frame->carrier_execution != task->execution.identity || frame->enclosing != run ||
        ksched_exit_thread(run) != frame->enclosing_thread ||
        kproc_live_pd(frame->process) != pd || kproc_live_pd(frame->enclosing_process) != run)
        panic("scheduler: nested association changed before entry");
    if (!kthread_start(frame->thread)) panic("scheduler: nested identity cannot start");
    runqueue_enter(&guard);
    frame->enclosing_launcher = execution_cpu.loaded_launcher;
    frame->outer = task->nested;
    task->nested_prepared = NULL;
    frame->state = NESTED_ACTIVE;
    task->nested = frame;
    runqueue_leave(&guard);
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST || CONF_WITH_PRIVATE_CLONE_TEST
    nested_probe.entered++;
    nested_probe.last_pid = (ULONG)kproc_pid(pd);
    nested_probe.last_tid = frame->tid;
    nested_probe.last_depth = nested_depth(task);
    nested_probe.last_pd = (uintptr_t)pd;
    if (nested_depth(task) > nested_probe.deepest) nested_probe.deepest = nested_depth(task);
#endif
}
/* Called by the common PROCESS finalizer, on the enclosing kernel stack, after
 * the launcher has released the nested syscall stack and restored its own
 * address space. Restores the enclosing owner before any destructor runs,
 * then reclaims the stopped THREAD: exactly once, never on its own stack. */
BOOL ksched_nested_leave(KPROC *process)
{
    KERNEL_TASK *task = execution_cpu.current;
    KSCHED_NESTED *frame;
    RUNQUEUE_GUARD guard;
    KSCHED_CLEANUP_SCOPE scope = {0};
    runqueue_unlocked();
    if (!scheduler.active || !task || !(frame = task->nested) || frame->process != process)
        return FALSE; /* task-owned or untracked PROCESS */
    if (task->state != TASK_RUNNING || frame->carrier != task ||
        frame->carrier_execution != task->execution.identity ||
        frame->state != NESTED_STOPPED || !kthread_is_stopped(frame->thread) ||
        task->nested_prepared || *owner_cleanup_depth(task) ||
        run != frame->enclosing || kproc_live_pd(frame->enclosing_process) != run ||
        execution_cpu.loaded_launcher != frame->enclosing_launcher)
        panic("scheduler: nested exit restored the wrong enclosing execution");
    runqueue_enter(&guard);
    task->nested = frame->outer;
    frame->outer = NULL;
    runqueue_leave(&guard);
    if (ksched_exit_thread(run) != frame->enclosing_thread ||
        kthread_process(frame->enclosing_thread) != frame->enclosing_process)
        panic("scheduler: enclosing THREAD lost across nested execution");
    /* Final KASPACE/identity release may invoke a yielding file destructor.
     * Keep enclosing cancellation latched until every nested owner is gone. */
    ksched_cleanup_begin(&scope);
    /* No ticket can name this stopped execution; none may outlive it. */
    ksched_waiter_retire(frame->process, frame->tid, task->execution.identity);
    release_nested_frame(frame);
    ksched_cleanup_end(&scope);
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST || CONF_WITH_PRIVATE_CLONE_TEST
    nested_probe.reclaimed++;
    if (!task->nested && nested_probe.deepest >= 2 && !nested_probe.reported) {
        nested_probe.reported = TRUE;
        kcprintf("native nested THREAD association/exit handoff/enclosing restoration: PASS\n");
    }
#endif
    return TRUE;
}

#if CONF_WITH_EXEC_REPLACE
/* Pexec(200) of a synchronous nested PROCESS (#713). Its user image runs on
 * its own syscall stack, entered from a launcher frame on the enclosing
 * execution's kernel stack. After the fallible preparation below, the no-fail
 * boundary abandons the old syscall continuation by jumping back into that
 * launcher, which commits the image off the old stack, switches to the new
 * root, retires the old one and enters the new program with a fresh stack. */
typedef struct nested_exec {
    KSCHED_NESTED *frame;
    KPROC_EXEC_REPLACEMENT *receipt;
    KPROC_EXEC_IMAGE *bundle;
    KASPACE *target, *old_thread_space;
    PADDR kstack_phys, old_kstack;
    uintptr_t kstack_top;
    uintptr_t irq_flags;
    UQUAD carrier_execution;
    BOOL committed;
} NESTED_EXEC;

static BOOL nested_exec_matches(KERNEL_TASK *task, const NESTED_EXEC *nx,
                                const KPROC_EXEC_REPLACEMENT *receipt, const KPROC_EXEC_IMAGE *bundle)
{
    KSCHED_NESTED *frame = nx->frame;
    return scheduler.active && !scheduler.stopping && task == execution_cpu.current &&
        task == execution_cpu.inflight && task->state == TASK_RUNNING && task->execution.on_cpu &&
        task->execution.identity == nx->carrier_execution &&
        task->nested == frame && !task->nested_prepared && frame->carrier == task &&
        frame->carrier_execution == task->execution.identity && frame->state == NESTED_ACTIVE &&
        frame->program == run && kproc_live_pd(frame->process) == run &&
        ksched_exit_thread(run) == frame->thread && kthread_process(frame->thread) == frame->process &&
        kthread_is_executing(frame->thread) && !frame->cancel_delivering &&
        frame->cleanup_depth == 1 && !ksched_cancellation_pending() &&
        !kproc_exec_replacement_validate(receipt) && kproc_exec_image_sealable(bundle) &&
        kproc_exec_replacement_selected(receipt) == nx->target &&
        kproc_exec_handoff_allowed(frame->process, frame->thread, TRUE) &&
        proc_exec_relaunch_ready(run);
}

static LONG exec_replace_nested(KERNEL_TASK *task, KPROC_EXEC_REPLACEMENT **receipt,
                                KPROC_EXEC_IMAGE **bundle)
{
    NESTED_EXEC *nx;
    THREAD_STARTUP_DRAFT *startup;
    LONG error = EACCDN;
    BOOL masked = FALSE;
    if (execution_cpu.nested_exec || execution_cpu.pending_exec ||
        task->nested->exec_preparing || kproc_exec_replacement_validate(*receipt))
        return EACCDN;
    if (replace_injected(1)) return ENSMEM;
    nx = kalloc(sizeof(*nx));
    if (!nx) return ENSMEM;
    memset(nx, 0, sizeof(*nx));
    nx->frame = task->nested;
    nx->carrier_execution = task->execution.identity;
    nx->target = kaspace_share(kproc_exec_replacement_selected(*receipt));
    nx->frame->exec_preparing = nx;
    if (!nx->target || replace_injected(2)) { error = ENSMEM; goto failed; }
    if (!nested_exec_matches(task, nx, *receipt, *bundle)) goto failed;
    /* The launcher, not the scheduler, enters the new image: it needs only
     * a fresh syscall stack, so the anchor's startup draft is not used. */
    nx->kstack_phys = kstack_alloc(&nx->kstack_top);
    if (!nx->kstack_phys || replace_injected(3)) { error = ENSMEM; goto failed; }
    startup = kproc_exec_image_take_startup(*bundle);
    thread_startup_abort(&startup);
    if (replace_injected(4) || replace_injected(5)) { error = ENSMEM; goto failed; }
    if (replace_injected(6)) {
        nx->carrier_execution++;
        if (nested_exec_matches(task, nx, *receipt, *bundle)) panic("exec replace: stale nested execution admitted");
        nx->carrier_execution--; error = EACCDN; goto failed;
    }
    if (replace_injected(7)) {
        if (ksched_exec_replace(receipt, bundle) != EACCDN) panic("exec replace: competing nested commit admitted");
        error = EACCDN; goto failed;
    }
    ksched_yield();
    if (!nested_exec_matches(task, nx, *receipt, *bundle)) { error = EACCDN; goto failed; }
    if (replace_injected(8)) { error = ENSMEM; goto failed; }
    nx->irq_flags = native_sched_irq_save(); masked = TRUE;
    if (!nested_exec_matches(task, nx, *receipt, *bundle)) { error = EACCDN; goto failed; }
    if (replace_injected(9)) { error = EAGAIN; goto failed; }
    if (replace_injected(10)) { error = ENSMEM; goto failed; }
    /* No-fail boundary: the handles belong to the relaunch from here on. */
    nx->receipt = *receipt; nx->bundle = *bundle;
    *receipt = NULL; *bundle = NULL;
    kproc_exec_replacement_transfer(nx->receipt);
    nx->frame->exec_preparing = NULL;
    execution_cpu.nested_exec = nx;
    proc_exec_relaunch(run);
failed:
    if (masked) native_sched_irq_restore(nx->irq_flags);
    nx->frame->exec_preparing = NULL;
    if (nx->kstack_phys) kstack_free(nx->kstack_phys);
    kaspace_put(nx->target);
    kfree(nx);
    return error;
}

BOOL ksched_exec_nested_committing(const KTHREAD *thread)
{
    NESTED_EXEC *nx = execution_cpu.nested_exec;
    KERNEL_TASK *task = execution_cpu.current;
    return nx && !nx->committed && task && task->nested == nx->frame &&
        nx->frame->thread == thread && task->execution.identity == nx->carrier_execution;
}

/* Called by the launcher after its relaunch longjmp, on its own stack with
 * interrupts still masked: the no-fail commit, then the fresh stack. */
void ksched_exec_relaunch_commit(PD *pd, PADDR *kstack_phys, uintptr_t *kstack_top)
{
    NESTED_EXEC *nx = execution_cpu.nested_exec;
    KERNEL_TASK *task = execution_cpu.current;
    KSCHED_NESTED *frame;
    KASPACE *space;
    uintptr_t marker;
    if (!nx || nx->committed || !task || task->nested != nx->frame || pd != run ||
        nx->frame->program != pd || nx->frame->state != NESTED_ACTIVE ||
        task->execution.identity != nx->carrier_execution || !kstack_phys || !kstack_top ||
        kstack_contains(*kstack_phys, (uintptr_t)&marker))
        panic("exec replace: unauthenticated nested relaunch");
    frame = nx->frame;
    space = kaspace_share(nx->target);
    if (!space) panic("exec replace: committed root reference lost");
    kthread_exec_rebind_nested(frame->thread, &space);
    nx->old_thread_space = space;
    kproc_exec_image_commit(nx->bundle);
    exec_rekey_scheduler(pd, kproc_live_pd(frame->process));
    kproc_exec_replacement_commit(nx->receipt);
    nx->committed = TRUE;
    kproc_exec_generation_advance(frame->process);
    ksched_wake_publish(&execution_cpu.wake);
    nx->old_kstack = *kstack_phys;
    *kstack_phys = nx->kstack_phys; *kstack_top = nx->kstack_top;
    nx->kstack_phys = 0;
}

/* After the launcher switched to the committed root: interrupts back on,
 * then the old syscall stack, root, memberships and container retire. */
void ksched_exec_relaunch_retire(void)
{
    NESTED_EXEC *nx = execution_cpu.nested_exec;
    uintptr_t marker;
    if (!nx || !nx->committed || kstack_contains(nx->old_kstack, (uintptr_t)&marker))
        panic("exec replace: nested retirement without commit");
    execution_cpu.nested_exec = NULL;
    native_sched_irq_restore(nx->irq_flags);
    kstack_free(nx->old_kstack);
    exec_retire_common(&nx->receipt, &nx->bundle, &nx->old_thread_space, &nx->target);
    kfree(nx);
}
#endif

BOOL ksched_finalizing_process(const KPROC *process)
{
    KERNEL_TASK *task=execution_cpu.current;
    /* The THREAD is already stopped by launcher handoff. Its scheduler
     * continuation still owns final PROCESS/wait publication until root. */
    return scheduler.active && task && task->state==TASK_RUNNING && task->process==process;
}

BOOL ksched_finish_exit(KPROC *process, PD *pd, UWORD status)
{
    KTHREAD *thread = ksched_exit_thread(pd);
    KSCHED_NESTED *frame;
    BOOL final;
    if (!thread) return TRUE; /* Untracked legacy child: no THREAD membership to retire. */
    final = kproc_exit_handoff_pending(process);
    frame = execution_cpu.current->nested;
    if (frame) {
        /* The launcher has left this THREAD's syscall stack. Detach its
         * membership exactly once; the frame stays innermost, so nobody is
         * authenticated until the finalizer restores the enclosing owner. */
        if (!final || frame->process != process || frame->thread != thread ||
            kthread_process(thread) != process || !kthread_finish_exit(thread, status))
            panic("scheduler: invalid nested launcher exit handoff");
        frame->program = NULL; /* the basepage is about to be released */
        frame->state = NESTED_STOPPED;
        nested_note_stopped(frame);
        return TRUE;
    }
    /* Fault intent belongs to this execution, never to a surviving sibling.
     * Group exit preserves its first normal status instead of a later fault. */
    if (final && execution_cpu.current->signal
#if CONF_WITH_PTERMGRP
        && !kproc_group_status(process, NULL)
#endif
       ) kproc_note_fault(pd, execution_cpu.current->signal);
    if (kthread_process(thread) != process || !kthread_finish_exit(thread, status))
        panic("scheduler: invalid launcher exit handoff");
    execution_cpu.current->exited = TRUE;
    check_task_thread(execution_cpu.current, 3);
    return final;
}

/* The thread is the sole owner of its backend scheduler stack. Called only
 * for an unpublished task or after root has made its continuation unreachable.
 * The syscall stack has a different owner: the existing gouser launcher. */
static void reclaim_task_execution(void *resource)
{
    KTHREAD_CONTEXT *execution = resource;
    if (execution->on_cpu || (execution_cpu.current && &execution_cpu.current->execution == execution))
        panic("scheduler: reclaiming current execution stack");
#if CONF_WITH_X86_64_MEMTEST
    x86_64_memtest_task_thread(NULL, execution->stack.physical, 7);
#endif
#if CONF_WITH_IRQ_RESTART
    interrupt_restart_dispose(execution);
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
    /* Already-loaded root-group images use the same non-runnable factory
     * as loader/async preparation, retaining the target's selected space.
     * Scheduler storage and execution adoption remain scheduler-owned. */
    task->thread = proc_clone_prepare_loaded_thread(task->program,
                                                     kproc_active_space(task->process));
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
    KERNEL_TASK *task = execution_cpu.current;
    return scheduler.active && task &&
           (task->state == TASK_RUNNING || task->state == TASK_BLOCKED) ? task->execution.identity : 0;
}
#if CONF_WITH_FORK_CORE
static UQUAD fork_cancel_child; /* private regression receipt, never an owner */
static ULONG fork_cancel_survivors; /* monotonic, never reset */
static KERNEL_TASK *fork_blocked_parent(const KPROC *child)
{
    KERNEL_TASK *task;
    for (task=scheduler.tasks;task;task=task->next_task) {
        FORK_PARENT_DEPENDENCY *dep=kthread_fork_dependency(task->thread);
        if (task->state==TASK_BLOCKED && dep && fork_parent_child(dep)==child &&
            fork_parent_state(dep)==FORK_PARENT_PUBLISHED &&
            fork_parent_execution(dep)==task->execution.identity && task->ready==fork_parent_ready &&
            task->argument==dep) return task;
    }
    return NULL;
}
BOOL ksched_test_fork_parent_blocked(const KPROC *child)
{
    return fork_blocked_parent(child)!=NULL;
}
BOOL ksched_test_fork_cancel_parent(KPROC *child)
{
#if CONF_WITH_PTERMGRP
    KERNEL_TASK *parent;
    KTHREAD *thread=ksched_exit_thread(run);
    if (!thread || kthread_process(thread)!=child || fork_cancel_child ||
        !(parent=fork_blocked_parent(child)) || !kproc_request_group_exit(parent->process,82)) return FALSE;
    fork_cancel_child=kproc_ref_number(child);
    ksched_irq_notify();return TRUE;
#else
    UNUSED(child);return FALSE;
#endif
}
BOOL ksched_test_fork_cancel_survived(KPROC *child)
{
    KTHREAD *thread=ksched_exit_thread(run);
    if (!thread || kthread_process(thread)!=child || !fork_cancel_child ||
        fork_cancel_child!=kproc_ref_number(child) || kproc_parent_pid(run)!=0 ||
        fork_blocked_parent(child) || fork_cancel_survivors==~(ULONG)0) return FALSE;
    fork_cancel_child=0; /* consume exact non-recycled receipt, not a counter reset */
    fork_cancel_survivors++;return TRUE;
}
ULONG ksched_test_fork_cancel_survivors(void) { return fork_cancel_survivors; }
#endif

void ksched_context_restore(UQUAD identity)
{
    KERNEL_TASK *task;
    execution_cpu.current = NULL;
    if (!scheduler.active || !identity) return;
    for (task = scheduler.tasks; task; task = task->next_task) {
        if (task->execution.identity == identity &&
            (task->state == TASK_RUNNING || task->state == TASK_BLOCKED)) {
            if (!task->execution.on_cpu || task->execution.owner_cpu!=execution_cpu.number || execution_cpu.inflight!=task)
                panic("scheduler: restore without CPU ownership");
            execution_cpu.current = task;
            return;
        }
    }
}

BOOL ksched_pid_reserved(WORD pid)
{
    KERNEL_TASK *task;
    {
        BOOL found=FALSE;
        KOWNER_GUARD guard; kowner_enter(&guard,KOWNER_RUNQUEUE);
        if(scheduler.active)for(task=scheduler.tasks;task;task=task->next_task)
            if(task->state!=TASK_UNUSED && task->pid==pid) { found=TRUE;break; }
        kowner_leave(&guard);
        return found;
    }
}

/* Called before a process's record/basepage is freed, including nested
 * synchronous children. No orphan keeps a pointer to its former parent. */
void ksched_exiting(PD *pd, UWORD result)
{
    KERNEL_TASK *task;
    KPROC *exiting;
    exiting=kproc_retain(pd);
    ksched_admission_owner_exited(exiting);
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST
    lifetime_probe_exiting(exiting,result);
#endif
    {
        KPROC *owner=NULL;
        KSCHED_ADMISSION waiter={0};
        RUNQUEUE_GUARD guard; runqueue_enter(&guard);
        if (execution_cpu.waiter_attached && exiting && exiting==execution_cpu.entry_owner) {
            execution_cpu.waiter_attached=FALSE;
            memset(execution_cpu.waiter_resume,0,sizeof(execution_cpu.waiter_resume));
            execution_cpu.entry_ready=NULL; execution_cpu.entry_argument=NULL;
            owner=execution_cpu.entry_owner; execution_cpu.entry_owner=NULL;
            waiter=execution_cpu.waiter; execution_cpu.waiter.identity=0;
        }
        runqueue_leave(&guard);
        if (owner) kproc_release(owner);
        ksched_admission_release(&waiter);
    }
    if (!scheduler.active) {
        kproc_release(exiting); return;
    }
    for (task = scheduler.tasks; task; task = task->next_task) {
        if (task->state == TASK_UNUSED || task->state == TASK_RESERVED || task->state == TASK_RETIRING) continue;
        if (task->parent == pd) {
            task->parent = NULL;
            if (task->state == TASK_ZOMBIE) task->state = TASK_DONE;
            ksched_wake_sequence(&process_events);
        }
        if (!task->completion_owner && exiting && task->process == exiting) {
            ULONG usage[8];
            if (task->process_completed) panic("duplicate PROCESS completion");
            task->process_completed = TRUE;
#if CONF_WITH_PTERMGRP
            if (kproc_group_status(exiting, NULL) && execution_cpu.current)
                execution_cpu.current->signal = 0;
#endif
            if (execution_cpu.current && execution_cpu.current->process == task->process)
                task->signal = execution_cpu.current->signal;
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
}

/* Activate only the retained canonical predecessor, after PROCESS cleanup.
 * The caller owns a reference; a reused PD can never admit this successor. */
void ksched_exited_identity(KPROC *process, PD *pd)
{
    KPROC *stable;
    UNUSED(pd);
    if (!scheduler.active || !process) return;
    stable=kproc_retain_identity(process);
    if (!stable) panic("successor activation lost predecessor identity");
    for (;;) {
        KERNEL_TASK *task;
        KPROC *owned=NULL;
        uintptr_t flags=native_sched_irq_save();
        for (task=scheduler.tasks;task;task=task->next_task) {
            if (task->state==TASK_DEFERRED && task->deferred_owner_identity==stable) {
                owned=task->deferred_owner_identity;
                task->deferred_owner_identity=NULL;
                check_task_thread(task,5);
                task->state=TASK_NEW;
                break;
            }
        }
        native_sched_irq_restore(flags);
        if (!owned) break;
        /* Release precisely the task's owner, outside metadata exclusion. A
         * reentrant root can trim records, so restart without a saved cursor. */
        kproc_release(owned);
    }
    kproc_release(stable);
}

void ksched_fault(PD *pd, WORD signal)
{
    KERNEL_TASK *task;
    if (!scheduler.active) {
        kproc_note_fault(pd, signal);
        return;
    }
    /* A foreign callback changes run, but retains the invoking task's
     * continuation. Attribute its fault to the owner, whose exit is deferred.
     * Nested synchronous children have no task entry of their own. */
    for (task = scheduler.tasks; task; task = task->next_task) {
        if (execution_cpu.current && execution_cpu.current->program == pd && task != execution_cpu.current)
            continue;
        if ((task->state == TASK_NEW || task->state == TASK_RUNNING ||
             task->state == TASK_BLOCKED) && task->program == pd) {
            if (!task->signal) task->signal = signal;
            return;
        }
    }
    kproc_note_fault(pd, signal); /* Nested child: no THREAD slot or live siblings. */
}

static void task_entry(void)
{
    KERNEL_TASK *task = execution_cpu.current;
    BOOL refused;
    check_task_thread(task, 2);
#if CONF_WITH_EXEC_REPLACE
    exec_replace_retire(task);
#endif
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST
    lifetime_probe_begin(task);
#endif
#if CONF_WITH_X86_64_MEMTEST
    if (task->startup && kthread_create(task->program))
        panic("clone admitted unvalidated execution after publication");
#endif
    {
        LONG status = proc_start_scheduled(task->program, task->take_ownership);
        if (!task->process_completed) task->result = status;
    }
    if (task->nested || task->nested_prepared)
        panic("scheduler: carrier exited with an associated nested execution");
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
            RUNQUEUE_GUARD guard; runqueue_enter(&guard);
            owner->state = owner->asynchronous && owner->parent ? TASK_ZOMBIE : TASK_DONE;
            ksched_wake_sequence(&process_events);
            runqueue_leave(&guard);
            /* Status and resources are finalized; wait visibility precedes
             * generic observer readiness. A refused launch is abandoned. */
            if (owner->process_completed) kproc_publish_observers(owner->process);
            /* A refused unstarted task may still retain its association.
             * Retirement during root reap publishes abandonment, not status. */
        } else if (owner == task) {
            RUNQUEUE_GUARD guard; runqueue_enter(&guard);
            /* Initial TID has stopped, but PID/resources/children remain live.
             * Its continuation is root-reapable; the PROCESS slot is not. */
            task->state = TASK_DRAINING;
            runqueue_leave(&guard);
        }
        if (owner != task) {
            RUNQUEUE_GUARD guard; runqueue_enter(&guard);
            task->state = TASK_DONE;
            runqueue_leave(&guard);
        }
    }
    native_sched_save(&task->execution.cpu);
    longjmp(execution_cpu.resume, 1);
}

/* Owner exclusion spans snapshot/predicate/admission today. Under SMP, the
 * producer must pair its sequence update with leaf wake publication and the
 * consumer must retain the owner across this check. Never run predicates here. */
static BOOL block_admit(KERNEL_TASK *task, BOOL (*ready)(void *), void *argument,
                        const KSCHED_WAIT_OBSERVATION *observation)
{
    KOWNER_GUARD publication; kowner_enter(&publication,KOWNER_RUNQUEUE);
    RUNQUEUE_GUARD guard; runqueue_enter(&guard);
    if (ksched_sequence_changed(observation)) {
        runqueue_leave(&guard);
        kowner_leave(&publication);
        return FALSE;
    }
    task->ready=ready;
    task->argument=argument;
    if (observation) task->observation=*observation;
    else { task->observation.source=NULL; task->observation.value=0; }
    task->state=TASK_BLOCKED;
    runqueue_leave(&guard);
    kowner_leave(&publication);
    return TRUE;
}
static BOOL block_observed(BOOL (*ready)(void *), void *argument,
                          const KSCHED_WAIT_OBSERVATION *observation)
{
    KERNEL_TASK *task = execution_cpu.current;
    runqueue_unlocked();
    if (!ksched_spawn_allowed()) return FALSE;
    if (!block_admit(task,ready,argument,observation)) return TRUE; /* recheck */
    native_sched_save(&task->execution.cpu);
    if (!setjmp(task->execution.resume)) longjmp(execution_cpu.resume, 1);
    {
        RUNQUEUE_GUARD guard; runqueue_enter(&guard);
        task->ready = NULL;
        task->argument = NULL;
        task->observation.source=NULL;
        task->observation.value=0;
        runqueue_leave(&guard);
    }
    return TRUE;
}
BOOL ksched_block(BOOL (*ready)(void *), void *argument)
{ return block_observed(ready,argument,NULL); }

BOOL ksched_cancellation_pending(void)
{
#if CONF_WITH_PTERMGRP
    KTHREAD *thread = ksched_exit_thread(run);
    return !scheduler.stopping && thread && !*owner_cancel_delivering(execution_cpu.current) &&
        kproc_group_status(kthread_process(thread), NULL);
#else
    return FALSE;
#endif
}

static KSCHED_WAIT_RESULT wait_observed(BOOL (*ready)(void *), void *argument,
                                        const KSCHED_WAIT_OBSERVATION *observation)
{
    KERNEL_TASK *task = execution_cpu.current;
    if (!ksched_spawn_allowed()) return KSCHED_WAIT_UNMANAGED;
#if CONF_WITH_PTERMGRP
    if (ksched_cancellation_pending()) return KSCHED_WAIT_CANCELLED;
    {
        RUNQUEUE_GUARD guard; runqueue_enter(&guard);
        task->cancel_boundary = TRUE;
        runqueue_leave(&guard);
    }
#endif
    block_observed(ready, argument, observation);
#if CONF_WITH_PTERMGRP
    {
        RUNQUEUE_GUARD guard; runqueue_enter(&guard);
        task->cancel_boundary = FALSE;
        runqueue_leave(&guard);
    }
    if (ksched_cancellation_pending()) return KSCHED_WAIT_CANCELLED;
#else
    UNUSED(task);
#endif
    return KSCHED_WAIT_READY;
}
KSCHED_WAIT_RESULT ksched_wait_cancelable(BOOL (*ready)(void *), void *argument)
{ return wait_observed(ready,argument,NULL); }
KSCHED_WAIT_RESULT ksched_wait_cancelable_observed(BOOL (*ready)(void *), void *argument,
                                                 const KSCHED_WAIT_OBSERVATION *observation)
{
    if (!ready || !observation || !observation->source)
        panic("scheduler: invalid owner wait observation");
    return wait_observed(ready,argument,observation);
}

static KSCHED_WAIT_RESULT dependency_wait_observed(BOOL (*ready)(void *),void *argument,
                                                    const KSCHED_WAIT_OBSERVATION *observation)
{
    /* Protected cleanup cannot terminate. Preserve its dependency progress
     * even when group cancellation is already pending; never busy-retry it. */
    if(ksched_cleanup_active())
        return block_observed(ready,argument,observation)?KSCHED_WAIT_READY:KSCHED_WAIT_UNMANAGED;
    return ksched_wait_cancelable_observed(ready,argument,observation);
}
static BOOL cancellation_wait(BOOL (*ready)(void *), void *argument)
{
    /* Cancellation cannot execute while this owner has cleanup tickets.
     * Keep dispatching its dependency instead of retrying CANCELLED forever. */
    if (ksched_cleanup_active()) return ksched_block(ready, argument);
    return ksched_wait_cancelable(ready, argument) != KSCHED_WAIT_UNMANAGED;
}

LONG ksched_result_wait(const KSCHED_ADMISSION *admission,LONG *results,UWORD count)
{
    KTHREAD *thread=ksched_exit_thread(run);
    KSCHED_WAITER waiter={0};
    KPROC *actor;
    ULONG tid;
    UQUAD execution;
    uintptr_t flags;
    LONG error;
    BOOL cancelled=FALSE;
    if (scheduler.stopping) return EACCDN;
    if (!thread) return EINVFN; /* result tickets require a managed execution */
    if (execution_cpu.current->nested) return EINVFN; /* tickets bind the carrier's own THREAD */
    if (!results) return EINVAL;
    actor=kthread_process(thread); tid=kthread_tid(thread); execution=ksched_context_id();
    flags=native_sched_irq_save();
    error=ksched_waiter_attach(admission,actor,tid,execution,count,&waiter);
    if (error) { native_sched_irq_restore(flags); return error; }
    for (;;) {
        void *context=ksched_waiter_context(&waiter);
        KSCHED_WAIT_OBSERVATION observation;
        KSCHED_WAIT_RESULT wait;
        ksched_waiter_observe(context,&observation);
        if (ksched_waiter_ready(context)) break;
        wait=ksched_wait_cancelable_observed(ksched_waiter_ready,context,&observation);
        if (wait==KSCHED_WAIT_CANCELLED) { cancelled=TRUE; break; }
        if (wait==KSCHED_WAIT_UNMANAGED) { error=EINVFN; break; }
    }
    /* Even an already-complete result cannot outrun pending cancellation. */
    if (ksched_cancellation_pending()) cancelled=TRUE;
    /* Revalidate the actual restored execution, not just captured credentials. */
    if (error==E_OK) {
        if (cancelled || ksched_exit_thread(run)!=thread || kthread_process(thread)!=actor ||
            kthread_tid(thread)!=tid || ksched_context_id()!=execution)
            error=EACCDN;
        else error=ksched_waiter_take(&waiter,actor,tid,execution,results,count);
    }
    ksched_waiter_detach(&waiter);
    native_sched_irq_restore(flags);
#if CONF_WITH_PTERMGRP
    if (cancelled) ksched_cancel_point();
#endif
    return error;
}

static BOOL always_ready(void *argument) { (void)argument; return TRUE; }
LONG ksched_yield(void)
{
    uintptr_t flags;
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST
    FS_ERROR_STATE saved_error;
    LONG error_pattern;
#endif
    if (scheduler.stopping) return EACCDN;
    if (!ksched_spawn_allowed()) {
        if (!execution_cpu.entered && scheduler.tasks) return enter_root(TRUE,NULL,NULL);
        return E_OK;
    }
#if CONF_WITH_X86_64_MEMTEST && CONF_WITH_MMAP
    x86_64_memtest_clone_yield(); /* process context, before IRQ exclusion */
#endif
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST
    sequence_probe_on_yield();
    kproc_region_selftest(); /* managed process context, outside runqueue exclusion */
    wait_probe_on_yield();
#endif
    flags = native_sched_irq_save();
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST
    /* Actual kernel continuation handoffs, not a prepared-image inspection.
     * Restore the enclosing syscall's complete error scope before cancellation. */
    saved_error=*fs_error_current();
    error_pattern=(LONG)(ksched_context_id() & 0x3fffffffUL);
    errcode=error_pattern; rwerr=~error_pattern; errdrv=(int)(error_pattern & 15);
#endif
    /* A protected cleanup must yield even with cancellation pending. It cannot
     * advertise a cancellation boundary or spin on an already-CANCELLED wait. */
    if (ksched_cleanup_active()) ksched_block(always_ready, NULL);
    else cancellation_wait(always_ready, NULL);
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST
    if (errcode!=error_pattern || rwerr!=~error_pattern || errdrv!=(int)(error_pattern & 15))
        panic("scheduler: disk-error continuation crossed execution owners");
    *fs_error_current()=saved_error;
#endif
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
    if (scheduler.stopping) return EACCDN;
    if (!caller) return EINVFN;
    if (execution_cpu.current->nested) return EINVFN; /* no sibling THREAD can exist */
    if (address && (address >= USER_VA_LIMIT ||
        (address & (__alignof__(ULONG) - 1)) ||
        !kproc_user_access(run, address, sizeof(output), TRUE))) return EIMBA;
    flags = native_sched_irq_save();
    error = kthread_join_begin(caller, tid);
    if (error) { native_sched_irq_restore(flags); return error; }
    while (!kthread_join_ready(caller)) {
        KSCHED_WAIT_OBSERVATION observation;
        KSCHED_WAIT_RESULT waited;
        kthread_join_observe(caller,&observation);
        if(kthread_join_ready(caller))break;
        waited=dependency_wait_observed(kthread_join_ready,caller,&observation);
        native_sched_irq_restore(flags);
#if CONF_WITH_PTERMGRP
        if(waited==KSCHED_WAIT_CANCELLED)ksched_cancel_point();
#endif
        if(waited==KSCHED_WAIT_UNMANAGED) { kthread_join_cancel(caller);return EINVFN; }
        flags = native_sched_irq_save();
    }
    native_sched_irq_restore(flags);
    if (!kthread_join_status(caller, &status)) {
        kthread_join_cancel(caller); return ESRCH;
    }
    output = status;
    /* No TID status is consumed until the entire copy succeeds. An escaping
     * memory fault stops the caller: root reap cancels its retained claim. */
    if(address) {
        KPROC_USER_COPY copy={0};
#if CONF_WITH_PTERMGRP
        BOOL cancelable=!ksched_cleanup_active();
#endif
        error=kproc_user_copy_begin(&copy,(VADDR)address,sizeof(output),TRUE);
#if CONF_WITH_PTERMGRP
        if(!error && cancelable && ksched_cancellation_pending())error=EACCDN;
#endif
        if(!error && !kproc_user_copy_write(&copy,&output))error=EIMBA;
        kproc_user_copy_end(&copy);
        if(error) {
            kthread_join_cancel(caller);
#if CONF_WITH_PTERMGRP
            if(cancelable && ksched_cancellation_pending())ksched_cancel_point();
#endif
            return error;
        }
    }
    flags = native_sched_irq_save();
    kthread_join_consume(caller);
    native_sched_irq_restore(flags);
    return E_OK;
}
#endif

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
    /* Destruction can yield and trim unrelated records. Restart each search. */
    for (;;) {
        KERNEL_TASK *task;
        for (task=scheduler.tasks;task;task=task->next_task)
            if (task->state==TASK_DEFERRED && task->deferred_owner_identity==owner)
                break;
        if (!task) return;
        discard_process_task(task);
    }
}
static LONG defer_cancel_owned(void)
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
LONG ksched_defer_cancel(void)
{
    KSCHED_CLEANUP_SCOPE scope={0};
    LONG result;
    ksched_cleanup_begin(&scope);
    result=defer_cancel_owned();
    ksched_cleanup_end(&scope);
    return result;
}
LONG ksched_process_prepare(PD *pd, BOOL deferred, void **slot)
{
    KERNEL_TASK *task;
    KPROC *owner = NULL;
    void *reservation = NULL;
    LONG result;
    if (!slot || *slot) return EINVAL;
    if (!ksched_async_allowed()) return EINVFN;
    if (next_identity == ~0ULL) return EAGAIN;
    if (!kproc_unlaunched(pd) || kproc_user_root(pd)) return EIMBA;
    for (task = scheduler.tasks; task; task = task->next_task)
        if (task->program == pd) return EACCDN;
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
    if (!kproc_prepare_async_thread(pd, run, ksched_controller_pd(), &task->thread)) {
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
    KERNEL_TASK *task;
    LONG pid;
    if (!slot || !*slot) return EINVAL;
    task=*slot;
    if (task->state != TASK_RESERVED || !task->process || !task->thread) return EACCDN;
    pid=task->pid;
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
    /* Preparation transferred loader ownership, and predecessor teardown is
     * irreversible. A lost reservation here is internal corruption, not a
     * recoverable loader error (which would invite double disposal). */
    if (result < 0) panic("deferred publication lost prepared task");
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
LONG ksched_spawn(PD *pd)
{
    KSCHED_CLEANUP_SCOPE scope = {0};
    void *slot = NULL;
    LONG result;
    ksched_cleanup_begin(&scope);
    result = ksched_process_prepare(pd, FALSE, &slot);
    if (result == E_OK) result = ksched_process_publish(&slot);
    ksched_cleanup_end(&scope);
    return result;
}

typedef struct { PD *parent; WORD pid; UQUAD owner; ULONG tid; } CHILD_WAIT;
static BOOL process_wait_target(const KERNEL_TASK *task,const CHILD_WAIT *wait)
{
    return task->asynchronous && !task->completion_owner &&
        task->state!=TASK_UNUSED && task->state!=TASK_RESERVED && task->state!=TASK_RETIRING &&
        task->parent==wait->parent && (wait->pid<0 || task->pid==wait->pid);
}
static BOOL process_wait_claim_available(const KERNEL_TASK *task,const CHILD_WAIT *wait)
{
    return !task->result_delivering && (!task->synchronous_wait_process ||
        (task->synchronous_wait_process==wait->owner && task->synchronous_waiter==wait->tid));
}
static void process_wait_release_claim_owned(const CHILD_WAIT *wait)
{
    KERNEL_TASK *task;
    uintptr_t flags=native_sched_irq_save();
    for(task=async_tasks();task;task=task->next_task)
        if(process_wait_target(task,wait) && task->synchronous_wait_process==wait->owner &&
           task->synchronous_waiter==wait->tid) {
            task->synchronous_waiter=0;task->synchronous_wait_process=0;
            ksched_wake_sequence(&process_events);
        }
    native_sched_irq_restore(flags);
}
static void process_wait_release_claim(const CHILD_WAIT *wait)
{
    KOWNER_GUARD owner_guard; kowner_enter(&owner_guard,KOWNER_RUNQUEUE);
    process_wait_release_claim_owned(wait);
    kowner_leave(&owner_guard);
}
static BOOL child_deliverable(const KERNEL_TASK *task)
{
    return task->state==TASK_ZOMBIE && !task->dependents &&
        !task->execution.on_cpu && !task->thread && !task->result_delivering;
}
/* Caller holds the leaf domain. A delivery claim pins the stable zombie slot
 * across fallible output preparation, independently of exact-child policy. */
static BOOL process_wait_pin(KERNEL_TASK *task,const CHILD_WAIT *wait)
{
    if(!process_wait_target(task,wait) || !process_wait_claim_available(task,wait) ||
       !child_deliverable(task))return FALSE;
    task->result_delivering=TRUE;
    return TRUE;
}
static BOOL child_ready_owned(void *argument)
{
    CHILD_WAIT *wait=argument;
    KERNEL_TASK *task;
    BOOL found=FALSE;
    for(task=async_tasks();task;task=task->next_task) {
        if(!process_wait_target(task,wait) || !process_wait_claim_available(task,wait)) continue;
        found=TRUE;
        if(child_deliverable(task)) return TRUE;
    }
    return !found;
}
static BOOL child_ready(void *argument)
{
    KOWNER_GUARD owner_guard; kowner_enter(&owner_guard,KOWNER_RUNQUEUE);
    BOOL result=child_ready_owned(argument);
    kowner_leave(&owner_guard);
    return result;
}

#if CONF_WITH_PRIVATE_CLONE_TEST
LONG ksched_test_process_waiters(WORD pid)
{
    KTHREAD *caller=ksched_exit_thread(run);
    KERNEL_TASK *task;
    KPROC *process;
    LONG count=0;
    RUNQUEUE_GUARD guard;

    if (!caller) return EINVFN;
    process=kthread_process(caller);
    /* Observe all waiters together, while their blocked frames still own
     * the CHILD_WAIT arguments. A runnable THREAD or Syield cannot count. */
    runqueue_enter(&guard);
    for (task=scheduler.tasks; task; task=task->next_task) {
        if (task->process==process && task->state==TASK_BLOCKED && task->ready==child_ready &&
            task->observation.source==&process_events && task->argument) {
            const CHILD_WAIT *wait=task->argument;
            if (wait->parent==run && wait->pid==pid) count++;
        }
    }
    runqueue_leave(&guard);
    return count;
}

LONG ksched_test_process_zombie(WORD pid)
{
    CHILD_WAIT wait={run,pid,0,0};
    KERNEL_TASK *task;
    BOOL zombie=FALSE;
    RUNQUEUE_GUARD guard;

    if (!ksched_exit_thread(run)) return EINVFN;
    runqueue_enter(&guard);
    for (task=async_tasks(); task; task=task->next_task) {
        if (process_wait_target(task,&wait) && child_deliverable(task)) {
            zombie=TRUE;
            break;
        }
    }
    runqueue_leave(&guard);
    return zombie;
}
#endif

static LONG wait_process(WORD pid, WORD options, UQUAD address,PROCESS_COMPLETION *completion,LONG *raw)
{
    CHILD_WAIT wait;
    uintptr_t flags;
    KERNEL_TASK *task;
    ULONG waiter=kthread_tid(ksched_exit_thread(run));
    KPROC *wait_owner=kproc_retain(run);
    UQUAD wait_number=kproc_ref_number(wait_owner);
    kproc_release(wait_owner);
    BOOL exact=completion || raw;
    if (scheduler.stopping) return EACCDN;
    if (options & ~3) return EINVAL;
    /* No process-group or stopped-process model is implemented yet. */
    if (!pid || pid < -1) return EINVFN;
    if (!scheduler.active) return EFILNF;
    if(exact && (!wait_number || pid<1)) return EINVFN;
#if CONF_WITH_PTERMGRP
    if(exact) ksched_cancel_point(); /* caller owns no uncommitted launch resources */
#endif
    wait.parent = run; wait.pid = pid; wait.owner=wait_number;wait.tid=waiter;
    flags = native_sched_irq_save();
    for (;;) {
        BOOL found = FALSE,contended=FALSE;
        KSCHED_WAIT_OBSERVATION observation;
        KOWNER_GUARD scan; kowner_enter(&scan,KOWNER_RUNQUEUE);
        ksched_sequence_observe(&process_events,&observation);
        for (task = async_tasks(); task; task = task->next_task) {
            ULONG status;
            if(!process_wait_target(task,&wait)) continue;
            if(!process_wait_claim_available(task,&wait)) {
                contended=TRUE;
                if(pid>0) {kowner_leave(&scan);native_sched_irq_restore(flags);return EACCDN;}
                continue; /* wait-any can still consume an unrelated child */
            }
            found=TRUE;
            if(exact) { task->synchronous_wait_process=wait_number;task->synchronous_waiter=waiter; }
            if(!process_wait_pin(task,&wait))continue;
            kowner_leave(&scan);
            /* Pin the canonical zombie/slot before fallible copy. Recursive
             * waiters cannot consume it and root cannot retire its identity. */
            native_sched_irq_restore(flags);
            if (address) {
                KPROC_USER_COPY copy={0};
#if CONF_WITH_PTERMGRP
                BOOL cancelable=!ksched_cleanup_active();
#endif
                LONG copied=kproc_user_copy_begin(&copy,(VADDR)address,sizeof(task->usage),TRUE);
#if CONF_WITH_PTERMGRP
                BOOL cancelled=cancelable && ksched_cancellation_pending();
                if(!copied && cancelled)copied=EACCDN;
#endif
                if(!copied && !kproc_user_copy_write(&copy,task->usage))copied=EIMBA;
                kproc_user_copy_end(&copy);
                if(copied) {
                    RUNQUEUE_GUARD guard; runqueue_enter(&guard);
                    task->result_delivering=FALSE;
                    ksched_wake_sequence(&process_events);
                    runqueue_leave(&guard);
#if CONF_WITH_PTERMGRP
                    if(cancelled) {
                        if(exact)process_wait_release_claim(&wait);
                        ksched_cancel_point();
                    }
#endif
                    return copied;
                }
            }
            native_sched_irq_disable();
            if(task->parent!=wait.parent || task->state!=TASK_ZOMBIE) {
                RUNQUEUE_GUARD guard; runqueue_enter(&guard);
                task->result_delivering=FALSE;
                ksched_wake_sequence(&process_events);
                runqueue_leave(&guard);
                native_sched_irq_restore(flags);return EACCDN;
            }
            kproc_add_child_usage(run, task->usage[0]);
#if CONF_WITH_SHARED_VM_PEXEC
            if(completion && (!process_observer_snapshot(task->process,completion) ||
                completion->state!=PROCESS_COMPLETED)) panic("zombie without canonical PROCESS result");
#endif
            task->synchronous_waiter=0;task->synchronous_wait_process=0;
            if(raw) *raw=task->result; /* the child's own exit code, all 16 bits */
            status = ((ULONG)(UWORD)task->pid << 16) |
                     (task->signal ? ((ULONG)task->signal << 8) : (task->result & 255));
            {
                KPROC *retired;
                RUNQUEUE_GUARD guard; runqueue_enter(&guard);
                retired=task->process;
                task->parent=NULL;task->pid=0;task->process=NULL;
                task->result_delivering=FALSE;task->state=TASK_RETIRING;
                ksched_wake_sequence(&process_events);
                runqueue_leave(&guard);
                /* A destructor can reenter: this detached slot stays private. */
                kproc_release(retired);
                clear_task(task);
            }
            if(!execution_cpu.entered && table_empty()) free_task_table();
            native_sched_irq_restore(flags);
            return (LONG)status;
        }
        if (!found || (options & 1)) {
            kowner_leave(&scan);
            native_sched_irq_restore(flags);
            return found ? 0 : contended ? EACCDN : EFILNF;
        }
        kowner_leave(&scan);
        if (!ksched_spawn_allowed()) {
            LONG error;
            native_sched_irq_restore(flags);
            error=enter_root(FALSE,child_ready,&wait);
            if(error) { if(exact) process_wait_release_claim(&wait);return error; }
        } else {
            KSCHED_WAIT_RESULT blocked=dependency_wait_observed(child_ready,&wait,&observation);
            native_sched_irq_restore(flags);
            if(blocked==KSCHED_WAIT_UNMANAGED) {
                if(exact)process_wait_release_claim(&wait);
                return EINVFN;
            }
        }
#if CONF_WITH_PTERMGRP
        if(exact && ksched_cancellation_pending() && !ksched_cleanup_active())
            process_wait_release_claim(&wait);
        ksched_cancel_point();
#endif
        flags = native_sched_irq_save();
    }
}
LONG ksched_wait(WORD pid,WORD options,UQUAD address)
{ return wait_process(pid,options,address,NULL,NULL); }
/* Reap exactly `pid`, a child this THREAD has just published, claiming it
 * first so that no other THREAD's Pwait() and no wait-any can take it; the
 * claim is released if this THREAD is cancelled, and the child then lives on
 * as an orphan, as in FreeMiNT. *result is its 16-bit exit code. */
LONG ksched_wait_exact(WORD pid,LONG *result)
{
    LONG error;
    if(!result) return EINVAL;
    error=wait_process(pid,0,0,NULL,result);
    return error<0 ? error : E_OK;
}
LONG ksched_wait_completion(WORD pid,PROCESS_COMPLETION *completion)
{
#if CONF_WITH_SHARED_VM_PEXEC
    if(!completion) return EINVAL;
    return wait_process(pid,0,0,completion,NULL);
#else
    UNUSED(pid);UNUSED(completion);return EINVFN;
#endif
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
    runqueue_unlocked();
#if CONF_WITH_X86_64_MEMTEST
    if (!execution_cpu.current && task->state == TASK_RETIRING) retiring_root_passes++;
#endif
    if (task->result_delivering || execution_cpu.current || task->execution.on_cpu || (task->state != TASK_DONE && task->state != TASK_ZOMBIE && task->state != TASK_DRAINING))
        return FALSE;
    if (task->nested || task->nested_prepared)
        panic("scheduler: stopped carrier retains a nested execution");
    /* Terminal task predicates must be unpublished before dropping the
     * kernel operation that supplied their argument or reclaiming its stack. */
    task->ready=NULL;
    task->argument=NULL;
    if (task->thread) {
        ksched_waiter_retire(task->process,kthread_tid(task->thread),task->execution.identity);
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST
        if (task->execution.identity==wait_probe.execution && wait_probe.resumed) {
            if (ksched_waiter_context(&wait_probe.abandoned))
                panic("scheduler: quiescent waiter survived execution");
            ksched_waiter_detach(&wait_probe.abandoned); /* stale handle is harmless */
            if (!wait_probe.reaped) kcprintf("native quiescent waiter retirement: PASS\n");
            wait_probe.reaped=TRUE;
        }
#endif
        check_task_thread(task, 4);
        if (!kthread_reap(task->thread))
            panic("scheduler: root cannot reap unfinished thread");
        {
            RUNQUEUE_GUARD guard; runqueue_enter(&guard);
            task->thread = NULL;
            ksched_wake_sequence(&process_events);
            runqueue_leave(&guard);
        }
    }
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST
    lifetime_probe_reap(task);
#endif
    if (task->completion_owner && !task->thread) {
        RUNQUEUE_GUARD guard; runqueue_enter(&guard);
        KERNEL_TASK *anchor=task->completion_owner;
        if (!anchor->dependents) panic("scheduler: anchor reference underflow");
        task->completion_owner=NULL;
        anchor->dependents--;
        ksched_wake_sequence(&process_events);
        runqueue_leave(&guard);
    }
    if (task->state == TASK_DONE && !task->dependents && task->process) {
        kproc_release(task->process);
        task->process = NULL;
    }
    if (!task->dependents && task->completion.identity && (task->state==TASK_DONE || task->state==TASK_ZOMBIE)) {
        if (task->result_published ||
            !ksched_admission_complete(&task->completion,task->result_slot,task->result))
            panic("scheduler: duplicate internal completion");
        task->result_published=TRUE;
        ksched_admission_release(&task->completion);
    }
    if (!task->dependents && task->state == TASK_DONE && !task->thread && !task->process && !task->completion.identity)
        clear_task(task);
    return TRUE;
}

static LONG dispatch_group(UWORD count, LONG *results);

static LONG run_group(PD * const *programs,UWORD count,LONG *results,BOOL take_ownership,BOOL prepare)
{
    KERNEL_TASK *slots[KSCHED_LIMIT]={NULL};
    UWORD i,j;
    if(scheduler.stopping) return EACCDN;
    if(execution_cpu.entered || execution_cpu.waiter_attached || execution_cpu.waiter.identity) return EAGAIN;
    if(!programs || !results || !count || count>KSCHED_LIMIT || (prepare && count!=1)) return EINVAL;
    if(next_identity>~0ULL-count) return EAGAIN;
    for(i=0;i<count;i++) {
        if(!kproc_unlaunched(programs[i]) || (!prepare && !kproc_user_root(programs[i]))) return EIMBA;
        for(j=0;j<i;j++) if(programs[i]==programs[j]) return EINVAL;
    }
    if(!prepare_batch(count,slots)) {
        if(scheduler.tasks && table_empty()) free_task_table();
        return ENSMEM;
    }
    for(i=0;i<count;i++) {
        KERNEL_TASK *task=slots[i];
        if(!task || task->state!=TASK_RESERVED)
            panic("scheduler: incomplete prepared batch");
        task->program=programs[i]; task->pid=kproc_pid(programs[i]);
        task->execution.identity=++next_identity; task->take_ownership=take_ownership;
        task->process=kproc_retain(programs[i]);
        if(!task->process || !kthread_stack_allocate(&task->execution.stack)) goto failed;
    }
    if(prepare && !kproc_prepare_async_thread(programs[0],run,ksched_controller_pd(),&slots[0]->thread)) goto failed;
    for(i=0;i<count;i++) {
        if(prepare) adopt_task_execution(slots[i]);
        else if(!attach_task_thread(slots[i])) goto failed;
    }
    for(i=0;i<count;i++) slots[i]->state=TASK_NEW; /* no-fail serialized publication */
    return dispatch_group(count,results);
failed:
    for(i=0;i<count;i++) {
        if(!slots[i]) continue; /* only acquired slots have rollback ownership */
        discard_task_execution(slots[i]);
        kproc_release(slots[i]->process); slots[i]->process=NULL;
        clear_task(slots[i]);
    }
    abandon_preparation();
    if(table_empty()) free_task_table();
    return ENSMEM;
}

static BOOL cancel_wait_ready(const KERNEL_TASK *task)
{
#if CONF_WITH_PTERMGRP
    /* A carrier hosting a nested execution is not at its own PROCESS's
     * boundary: the request stays latched until that launcher has returned. */
    return !task->nested && task->cancel_boundary && !task->cancel_delivering &&
        task->execution.cpu.pd == task->program && kproc_group_status(task->process, NULL);
#else
    UNUSED(task);
    return FALSE;
#endif
}

/* One permanent root frame. All mutable locals are assigned after setjmp;
 * none carries caller-owned state across a park/resume or task handoff. */
static void scheduler_root_entry(void) NORETURN;
static void scheduler_root_entry(void)
{
    if (setjmp(execution_cpu.resume)) native_sched_restore(&execution_cpu.root);
    if (!execution_cpu.entered || execution_cpu.parked)
        panic("scheduler: root resumed without activation owner");
    check_root_stack();
    if (run!=ksched_controller_pd() || execution_cpu.root.pd!=run || execution_cpu.root.launch)
        panic("scheduler: borrowed root controller");
    /* A root cleanup callback must never escape into the launcher's errbuf.
     * Recoverable close retries stay local; an escaping disk error is fatal,
     * with no retry of partially committed retirement/publication. */
    if (setjmp(errbuf)) panic("scheduler root: cleanup callback escaped");
    execution_cpu.root.disk_error = *fs_error_current();
    if (execution_cpu.pending_clone) {
        PROC_CLONE_TRANSACTION *tx = execution_cpu.pending_clone;
        execution_cpu.pending_clone = NULL;
        proc_clone_commit_prepared(tx, execution_cpu.pending_slot);
        execution_cpu.pending_slot=NULL;
    }
    for (;;) {
        BOOL pending = FALSE;
        KERNEL_TASK *task, *first;
        UQUAD wake_observed;
        /* Root owns its mask until the backend's atomic idle sequence. This
         * also makes 64-bit owner stamps indivisible on current 32-bit CPUs. */
        native_sched_irq_disable();
        wake_observed=execution_cpu.wake.sequence;
        check_root_stack();
        cpu_acknowledge_handoff();
#if CONF_WITH_EXEC_REPLACE
        exec_handoff_on_root();
#endif
        if (scheduler.stopping) panic("scheduler: dispatch during shutdown");
        runqueue_unlocked(); /* predicates and notifications are outside leaf exclusion */
        check_root_stack();
#if CONF_WITH_NATIVE_PREEMPT && CONF_WITH_PRIVATE_CLONE_TEST
        if (execution_cpu.preempt_patterns==3 && !execution_cpu.preempt_patterns_reported) {
            execution_cpu.preempt_patterns_reported=TRUE;
            kprintf("native timer original/alternate GPR/FPU delayed restoration: PASS\r\n");
        }
#endif
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST
        wait_probe_on_root();
#if ARCH_X86_64
        x86_64_irq_capture_report();
#elif ARCH_ARM && CONF_WITH_EXIT_HANDOFF_TEST
        arm_irq_capture_report();
#elif ARCH_M68K && CONF_WITH_EXIT_HANDOFF_TEST
        m68k_irq_capture_report();
#endif
#endif
        /* Reclaim only from root. Referenced slots remain stable across entries. */
        for (task = scheduler.tasks; task; task = task->next_task) {
#if CONF_WITH_NATIVE_PREEMPT
            interrupt_restart_reap(&task->execution);
#endif
            reap_finished_on_root(task);
        }
        /* A leader can precede its last worker in the list. Publish/retire
         * anchors only after all stopped dependents have dropped ownership. */
        for (task = scheduler.tasks; task; task = task->next_task)
            reap_finished_on_root(task);
        trim_unused_records();
        if (execution_cpu.waiter_attached &&
            ((execution_cpu.pump && !execution_cpu.budget) ||
             (!execution_cpu.pump && execution_cpu.entry_ready(execution_cpu.entry_argument)))) break;
        first = scheduler.next;
        task = first;
        do {
            if (task->state == TASK_UNUSED || task->state == TASK_RESERVED || task->state == TASK_RETIRING || task->state == TASK_DONE || task->state == TASK_ZOMBIE) continue;
            if (task->state == TASK_DRAINING) { pending = TRUE; continue; }
            pending = TRUE;
            if (task->state == TASK_DEFERRED) continue;
            if (task->state == TASK_BLOCKED &&
                !(task->observation.source && ksched_sequence_changed(&task->observation)) &&
                !task->ready(task->argument) && !cancel_wait_ready(task)) continue;
            scheduler.next = task->next_task ? task->next_task : scheduler.tasks;
            cpu_claim_task(task);
            if(execution_cpu.pump && execution_cpu.budget) execution_cpu.budget--;
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
        } while ((task = task->next_task ? task->next_task : scheduler.tasks) != first);
        if (execution_cpu.waiter_attached && execution_cpu.pump) break; /* no runnable work: return, never spin */
        if (!pending && execution_cpu.waiter_attached) panic("scheduler: missing synchronous completion");
        runqueue_unlocked();
        native_sched_irq_disable();
        if (!ksched_wake_arm(&execution_cpu.wake,wake_observed)) continue;
        native_sched_idle(); /* pending IRQ + idle is indivisible in each backend */
        ksched_wake_disarm(&execution_cpu.wake);
    }
    ksched_wake_disarm(&execution_cpu.wake);
    if(table_empty()) free_task_table();
    {
        KPROC *owner;
        RUNQUEUE_GUARD guard; runqueue_enter(&guard);
        execution_cpu.current=NULL;
        memset(&execution_cpu.root,0,sizeof(execution_cpu.root));
        execution_cpu.parked=TRUE; execution_cpu.entered=FALSE;
        execution_cpu.entry_ready=NULL; execution_cpu.entry_argument=NULL;
        owner=execution_cpu.entry_owner; execution_cpu.entry_owner=NULL;
        runqueue_leave(&guard);
        kproc_release(owner);
    }
    if(execution_cpu.waiter_attached) longjmp(execution_cpu.waiter_resume,1);
    panic("scheduler: detached root has no live return target");
}

static LONG enter_root(BOOL pump,BOOL (*ready)(void *),void *argument)
{
    NATIVE_SCHED_CONTEXT caller;
    KERNEL_TASK *task;
    runqueue_unlocked();
    if(scheduler.stopping) return EACCDN;
    if(execution_cpu.inflight || execution_cpu.entered || execution_cpu.waiter_attached || (execution_cpu.root_started && !execution_cpu.parked) ||
        (execution_cpu.waiter.identity && (pump || ready!=admission_ready))) return EAGAIN;
    if(!pump && !ready) return EINVAL;
    if(!scheduler.tasks) return E_OK;
    if(!execution_cpu.entry_owner) {
        execution_cpu.entry_owner=kproc_retain(run);
        if(run && kproc_pid(run) && !execution_cpu.entry_owner) return EACCDN;
    }
    {
    RUNQUEUE_GUARD guard; runqueue_enter(&guard);
    execution_cpu.pump=pump; execution_cpu.budget=0;
    for(task=scheduler.tasks;task;task=task->next_task)
        if(task->state!=TASK_UNUSED && task->state!=TASK_RESERVED && task->state!=TASK_DONE && task->state!=TASK_ZOMBIE)
            execution_cpu.budget++;
    execution_cpu.entry_ready=ready; execution_cpu.entry_argument=argument;
    execution_cpu.entered=TRUE; execution_cpu.waiter_attached=TRUE; execution_cpu.current=NULL;
    runqueue_leave(&guard);
    }
    native_sched_save(&caller); native_sched_save(&execution_cpu.root);
    native_sched_root_context(&execution_cpu.root,ksched_controller_pd(),
                             native_sched_root_stack_top((uintptr_t)execution_cpu.stack,sizeof(execution_cpu.stack)));
    execution_cpu.parked=FALSE;
    if(!setjmp(execution_cpu.waiter_resume)) {
        native_sched_restore(&execution_cpu.root);
        if(execution_cpu.root_started) longjmp(execution_cpu.resume,1);
        execution_cpu.root_started=TRUE; execution_cpu.stack[0]=ROOT_STACK_GUARD;
        native_sched_enter(native_sched_root_stack_top((uintptr_t)execution_cpu.stack,sizeof(execution_cpu.stack)),scheduler_root_entry);
    }
    if(!execution_cpu.parked || execution_cpu.entered || execution_cpu.current || execution_cpu.inflight || execution_cpu.entry_argument || execution_cpu.entry_ready || execution_cpu.entry_owner)
        panic("scheduler: returning with retained caller attachment");
    {
        RUNQUEUE_GUARD guard; runqueue_enter(&guard);
        execution_cpu.waiter_attached=FALSE;
        memset(execution_cpu.waiter_resume,0,sizeof(execution_cpu.waiter_resume));
        runqueue_leave(&guard);
    }
    native_sched_restore(&caller);
    return E_OK;
}
static BOOL admission_ready(void *handle) { return ksched_admission_ready(handle); }
static LONG dispatch_group(UWORD count,LONG *results)
{
    LONG error=enter_root(FALSE,admission_ready,&execution_cpu.waiter);
    if(error) panic("scheduler: prepared attachment cannot enter");
    error=ksched_admission_take(&execution_cpu.waiter,results,count);
    ksched_admission_release(&execution_cpu.waiter);
    if(error) panic("scheduler: invalid internal result delivery");
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
LONG ksched_task_reserve(BOOL root,void **slot)
{
    KERNEL_TASK *task=NULL;
    runqueue_unlocked();
    if(!slot || *slot) return EINVAL;
    if(scheduler.stopping) return EACCDN;
    if(root) {
        if(execution_cpu.entered || execution_cpu.waiter_attached || execution_cpu.waiter.identity) return EAGAIN;
    } else if(!ksched_async_allowed()) return EINVFN;
#if CONF_WITH_X86_64_MEMTEST
    if(fail_clone_slot) { fail_clone_slot=FALSE; return ENSMEM; }
#endif
    if(next_identity==~0ULL) return EAGAIN;
    if(root) {
        if(!prepare_batch(1,&task)) { if(scheduler.tasks && table_empty()) free_task_table(); return ENSMEM; }
    } else {
        task=reserve_record(); if(!task) return ENSMEM;
    }
    task->execution.identity=++next_identity;
    *slot=task; return E_OK;
}
void ksched_task_cancel(void **slot)
{
    KERNEL_TASK *task;
    if (!slot || !*slot) return;
    task = *slot;
    if (!scheduler.tasks || task->state != TASK_RESERVED)
        panic("task cancellation outside private reservation");
    *slot = NULL;
    if(task->completion.identity) {
        abandon_preparation();
    }
    clear_task(task);
    if(!execution_cpu.entered && table_empty()) free_task_table();
}
static void publish_task(void **slot, TASK_STATE state)
{
    RUNQUEUE_GUARD guard; runqueue_enter(&guard);
    KERNEL_TASK *task = *slot;
    if(scheduler.stopping) panic("scheduler: publication after shutdown");
    *slot = NULL;
    __asm__ volatile ("" ::: "memory");
    task->state = state;
    ksched_wake_sequence(&process_events);
    runqueue_leave(&guard);
}
void ksched_task_publish(void **slot) { publish_task(slot, TASK_NEW); }
BOOL ksched_startup_reclaimable(const THREAD_STARTUP_DRAFT *startup)
{
    KERNEL_TASK *task;
    if (!scheduler.active || execution_cpu.current || !startup) return FALSE;
    for (task = scheduler.tasks; task; task = task->next_task) {
        if (task->startup == startup)
            return !task->program && task->thread && !kthread_is_executing(task->thread) &&
                (task->state == TASK_DONE || task->state == TASK_ZOMBIE || task->state == TASK_DRAINING);
    }
    return FALSE;
}
BOOL ksched_clone_context(KPROC *source, PD **controller)
{
    if (scheduler.stopping) return FALSE;
#if CONF_WITH_PTERMGRP
    if (kproc_group_status(source, NULL)) return FALSE;
#endif
    /* A nested execution has no anchor to own dependents (see ksched_nested_prepare). */
    if (!ksched_spawn_allowed() || execution_cpu.current->nested ||
        completion_task(execution_cpu.current)->dependents==~(ULONG)0 ||
        execution_cpu.current->process != source ||
        execution_cpu.current->program != run || kproc_live_pd(source) != run) return FALSE;
    *controller = ksched_controller_pd(); return *controller!=NULL;
}
#if CONF_WITH_EXEC_REPLACE
/* Executable replacement creates no dependent, so unlike Pclone it is open
 * to the innermost nested owner as well as to an anchor execution. */
BOOL ksched_exec_context(KPROC *source)
{
    KERNEL_TASK *task = execution_cpu.current;
    if (!ksched_spawn_allowed() || !source || kproc_live_pd(source) != run) return FALSE;
#if CONF_WITH_PTERMGRP
    if (kproc_group_status(source, NULL)) return FALSE;
#endif
    if (task->nested)
        return !task->nested_prepared && task->nested->process == source &&
            task->nested->program == run && task->nested->state == NESTED_ACTIVE;
    return task->process == source && task->program == run;
}
#endif
BOOL ksched_loaded_clone_context(KPROC *source,PD **controller)
{
    KTHREAD *owner;
    if(!source || !ksched_async_allowed() || kproc_live_pd(source)!=run ||
#if CONF_WITH_PTERMGRP
        kproc_group_status(source,NULL) ||
#endif
        !kproc_active_space(source)) return FALSE;
    /* A nested source owns its logical THREAD; the carrier continues to own
     * its original anchor. THREAD-less legacy callers keep their old path. */
    owner=ksched_exit_thread(run);
    if(owner && kthread_process(owner)!=source) return FALSE;
    *controller=ksched_controller_pd();return *controller!=NULL;
}
BOOL ksched_loaded_clone_begin(KPROC *source,PD **controller,KSCHED_PUBLICATION_GUARD *guard)
{
    uintptr_t flags=native_sched_irq_save();
    KOWNER_GUARD owner; kowner_enter(&owner,KOWNER_PROCESS);
    if(!ksched_loaded_clone_context(source,controller)) {kowner_leave(&owner);native_sched_irq_restore(flags);return FALSE;}
#if CONF_WITH_X86_64_MEMTEST
    if(clone_masked) panic("nested loaded clone publication");
    clone_masked=TRUE;
#endif
    guard->flags=flags;guard->owner=owner;return TRUE;
}
BOOL ksched_clone_begin(KPROC *source, PD **controller, KSCHED_PUBLICATION_GUARD *guard)
{
    uintptr_t flags;
    KOWNER_GUARD owner;
    flags = native_sched_irq_save();
    kowner_enter(&owner,KOWNER_PROCESS);
    if (!ksched_clone_context(source, controller)) {
        kowner_leave(&owner);
        native_sched_irq_restore(flags);
        return FALSE;
    }
#if CONF_WITH_X86_64_MEMTEST
    if (clone_masked) panic("nested clone publication exclusion");
    clone_masked = TRUE;
#endif
    guard->flags = flags;guard->owner=owner;return TRUE;
}
void ksched_clone_end(const KSCHED_PUBLICATION_GUARD *guard)
{
#if CONF_WITH_X86_64_MEMTEST
    if (!clone_masked) panic("clone publication exclusion already released");
    clone_masked = FALSE;
#endif
    kowner_leave(&guard->owner);
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
        if (!ksched_spawn_allowed() || execution_cpu.current->process != process || !syscall_stack || !syscall_stack->physical)
            panic("sibling publication context changed");
        {
        RUNQUEUE_GUARD guard; runqueue_enter(&guard);
        task->completion_owner = completion_task(execution_cpu.current);
        if (task->completion_owner->dependents==~(ULONG)0)
            panic("scheduler: unprepared anchor reference");
        task->completion_owner->dependents++;
        runqueue_leave(&guard);
        }
        task->asynchronous = TRUE; /* root recycles TID slots, never zombies */
        task->execution.syscall_stack.physical = syscall_stack->physical;
        task->execution.syscall_stack.top = syscall_stack->top;
        syscall_stack->physical = syscall_stack->top = 0;
    } else if (logical_parent) {
        KTHREAD *owner=ksched_exit_thread(run);
        if ((execution_cpu.current && execution_cpu.current->process != logical_parent &&
             !(kproc_shared_exec_committed(process) &&
               (!owner || kthread_process(owner)==logical_parent))) ||
            !ksched_async_allowed() || kproc_live_pd(logical_parent) != run)
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
    execution_cpu.pending_slot=*slot;
    *slot = NULL; /* consumed by the root commit, no fallible work remains */
    execution_cpu.pending_clone = transaction;
    return dispatch_group(1, status);
}
