/* Cooperative kernel tasks, independent of the legacy AES scheduler.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "emutos.h"
#include "biosext.h"
#include "ksched.h"
#include "ksched_lifetime.h"
#include "ksched_wake.h"
#include "kproc.h"
#include "native_sched_backend.h"
#include "kthread_context.h"
#include "kthread.h"
#include "thread_startup.h"
#include "proc_clone.h"
#include "fs.h"
#include "callback_cpu.h"
#include "ksched_legacy_irq.h"
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

typedef enum { TASK_UNUSED, TASK_RESERVED, TASK_NEW, TASK_RUNNING, TASK_BLOCKED, TASK_DRAINING, TASK_DONE, TASK_ZOMBIE
#if CONF_WITH_CUIS
    , TASK_DEFERRED
#endif
} TASK_STATE;
typedef struct kernel_task {
    struct kernel_task *next_task; /* Stable arena link; never a lifetime reference. */
    PD *program, *parent;
    struct kernel_task *completion_owner; /* owns one dependent reference; NULL is anchor */
    ULONG dependents; /* prevents anchor retirement/reuse until worker root reap */
    BOOL process_completed, result_published;
    KSCHED_ADMISSION completion; /* producer ownership, independent of arena position */
    UWORD result_slot;          /* cell in that admission, never a task-table index */
#if CONF_WITH_PTERMGRP
    BOOL cancel_boundary; /* only waits whose callers explicitly checkpoint */
    BOOL cancel_delivering; /* termination hooks must not recursively cancel */
#endif
    KPROC *process;              /* observer identity, including after exit */
    KTHREAD *thread;            /* owns execution until root-safe reap */
#if CONF_WITH_X86_64_MEMTEST
    ULONG prepared_tid;
#endif
#if CONF_WITH_CUIS
    PD *deferred_owner;
    KPROC *deferred_owner_identity; /* owning identity, not parent resources */
#endif
    WORD pid, signal;
    BOOL asynchronous, take_ownership, exited;
    THREAD_STARTUP_DRAFT *startup;
    KTHREAD_CONTEXT execution;
    TASK_STATE state;
    BOOL (*ready)(void *);
    void *argument;
    KSCHED_WAIT_OBSERVATION observation; /* owner storage retained until resume */
    LONG result;
    ULONG usage[2];
} KERNEL_TASK;
/* Shared queue/policy. Execution state is owned by the selected CPU below. */
static struct {
    BOOL active; /* queue exists, including while boot CPU root is parked */
    BOOL stopping; /* irreversible shutdown closes admission and dispatch */
    KERNEL_TASK *tasks, *next;
} scheduler;
#define ROOT_STACK_BYTES (16UL * 1024)
#define ROOT_STACK_GUARD 0x713a2001UL
typedef struct {
    PD *loaded_process; /* first: frozen run ABI aliases this CPU-owned slot */
    UWORD number; /* logical boot CPU 0; no additional CPU may dispatch yet */
    BOOL leaf_held, legacy_irq_held;
    uintptr_t legacy_irq_flags;
#if CONF_WITH_USER_CALLBACKS
    USER_CALLBACK_CPU callback; /* frame chain includes watchdog deadlines */
#endif
    FS_ERROR_STATE loaded_error; /* includes code/drive, not only longjmp registers */
    void *loaded_launcher; /* CPU-loaded nested chain, saved in each execution context. */
    KERNEL_TASK *current, *inflight;
    KSCHED_WAKE_STATE wake; /* permanent CPU-owned IRQ/idle handshake */
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
    ULONG stack[ROOT_STACK_BYTES/sizeof(ULONG)] __attribute__((aligned(16)));
} NATIVE_SCHED_CPU;
static NATIVE_SCHED_CPU boot_cpu;
/* TOS exports &run in OSHEADER. Keep that ABI without a second mutable mirror.
 * Only boot CPU dispatch exists; a future CPU selector must not export its
 * private slot as another system-wide TOS pointer or activate SMP here. */
typedef char loaded_process_abi_offset[offsetof(NATIVE_SCHED_CPU,loaded_process)==0 ? 1:-1];
#define KSCHED_STRING_(x) #x
#define KSCHED_STRING(x) KSCHED_STRING_(x)
/* An assembler alias leaves one complete storage object for instrumentation.
 * A C variable alias of pointer size registers a second, overlapping ASan
 * global and poisons the remaining CPU fields as the pointer's redzone. */
__asm__(".globl " KSCHED_STRING(__USER_LABEL_PREFIX__) "run\n"
        ".set " KSCHED_STRING(__USER_LABEL_PREFIX__) "run,"
                KSCHED_STRING(__USER_LABEL_PREFIX__) "boot_cpu");
#undef KSCHED_STRING
#undef KSCHED_STRING_
/* Deliberately boot-CPU-only. B1c/B2 must add architecture CPU-local selection;
 * callers must not infer SMP or asynchronous dispatch from this accessor. */
static NATIVE_SCHED_CPU *native_sched_cpu(void) { return &boot_cpu; }
#define execution_cpu (*native_sched_cpu())
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
typedef struct { uintptr_t flags; } RUNQUEUE_GUARD;
static RUNQUEUE_GUARD runqueue_enter(void)
{
    RUNQUEUE_GUARD guard;
    guard.flags=native_sched_irq_save();
    if (execution_cpu.leaf_held) panic("scheduler: recursive leaf exclusion");
    execution_cpu.leaf_held=TRUE;
    return guard;
}
static void runqueue_leave(RUNQUEUE_GUARD guard)
{
    if (!execution_cpu.leaf_held) panic("scheduler: unbalanced leaf exclusion");
    execution_cpu.leaf_held=FALSE;
    native_sched_irq_restore(guard.flags);
}
static void runqueue_unlocked(void)
{
    if (execution_cpu.leaf_held) panic("scheduler: operation under leaf exclusion");
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
    RUNQUEUE_GUARD guard=runqueue_enter();
    task=execution_cpu.inflight;
    if (task) {
        if (!task->execution.on_cpu || task->execution.owner_cpu!=execution_cpu.number)
            panic("scheduler: foreign CPU handoff");
#if CONF_WITH_USER_ASPACE
        task->execution.interrupted_user.valid=FALSE;
#endif
        task->execution.on_cpu=FALSE;
        execution_cpu.inflight=NULL;
    }
    execution_cpu.current=NULL;
    runqueue_leave(guard);
}
static void cpu_claim_task(KERNEL_TASK *task)
{
    RUNQUEUE_GUARD guard=runqueue_enter();
    if (execution_cpu.current || execution_cpu.inflight || task->execution.on_cpu)
        panic("scheduler: duplicate CPU ownership");
    task->execution.owner_cpu=execution_cpu.number;
    task->execution.on_cpu=TRUE;
    execution_cpu.current=execution_cpu.inflight=task;
    runqueue_leave(guard);
}
void ksched_shutdown_begin(void)
{
    RUNQUEUE_GUARD guard=runqueue_enter();
    scheduler.stopping=TRUE;
    runqueue_leave(guard);
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
    RUNQUEUE_GUARD guard=runqueue_enter();
    next=task->next_task;
    if (task->dependents || task->completion_owner || task->execution.on_cpu)
        panic("scheduler: clearing referenced task");
    completion=task->completion;
    task->completion.identity=0;
    task->state=TASK_RESERVED; /* do not reuse while a destructor can reenter */
    runqueue_leave(guard);
    ksched_admission_release(&completion);
    guard=runqueue_enter();
    memset(task, 0, sizeof(*task));
    task->next_task = next;
    runqueue_leave(guard);
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
    return task;
}
/* Transfer public ownership before any callback/last-reference release.
 * Reentrant admission cannot inherit or be erased by the old caller's handle. */
static void abandon_preparation(void)
{
    KPROC *owner;
    KSCHED_ADMISSION waiter;
    RUNQUEUE_GUARD guard=runqueue_enter();
    owner=execution_cpu.entry_owner; execution_cpu.entry_owner=NULL;
    waiter=execution_cpu.waiter; execution_cpu.waiter.identity=0;
    runqueue_leave(guard);
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
    RUNQUEUE_GUARD guard=runqueue_enter();
    if(!scheduler.tasks || !table_empty()) { runqueue_leave(guard); return; }
    base=scheduler.tasks;
    scheduler.tasks=scheduler.next=NULL;
    scheduler.active=FALSE;
    if (!execution_cpu.waiter_attached) {
        waiter=execution_cpu.waiter; execution_cpu.waiter.identity=0;
        owner=execution_cpu.entry_owner; execution_cpu.entry_owner=NULL;
    }
    runqueue_leave(guard);
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
        RUNQUEUE_GUARD guard=runqueue_enter();
        KERNEL_TASK *task=*link;
        if(task->state==TASK_UNUSED && !task->thread && !task->process && !task->dependents &&
            !task->completion_owner && !task->completion.identity && !task->execution.on_cpu) {
            if(scheduler.next==task) scheduler.next=task->next_task ? task->next_task : scheduler.tasks;
            *link=task->next_task;
            runqueue_leave(guard);
            kfree(task);
        } else {
            link=&task->next_task;
            runqueue_leave(guard);
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
    if (wait_probe.execution || !task || !task->asynchronous || task->completion_owner || !task->parent) return;
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
    if (!owner || owner->physical || owner->top || !ksched_sibling_launch(pd)) return FALSE;
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
    if (scheduler.stopping || !scheduler.active || execution_cpu.leaf_held || execution_cpu.legacy_irq_held ||
        !task || execution_cpu.inflight!=task || !task->execution.on_cpu ||
        task->execution.owner_cpu!=execution_cpu.number || task->state!=TASK_RUNNING ||
        task->program!=pd || !kthread_is_executing(task->thread)) return NULL;
    *space=kthread_aspace(task->thread);
    if (!*space || *space!=kproc_active_space(task->process)) return NULL;
    *tid=kthread_tid(task->thread);
    return &task->execution;
}

KTHREAD *ksched_exit_thread(PD *pd)
{
    KERNEL_TASK *task = execution_cpu.current;
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
    if (scheduler.stopping) return;
    if (thread && !execution_cpu.current->cancel_delivering &&
        kproc_group_status(kthread_process(thread), &status)) xterm(status);
}
void ksched_enter_exit(PD *pd)
{
    if (ksched_exit_thread(pd)) execution_cpu.current->cancel_delivering = TRUE;
}
LONG ksched_termgrp(UWORD status)
{
    KTHREAD *thread = ksched_exit_thread(run);
    uintptr_t flags;
    if (!thread) return EINVFN;
    if (scheduler.stopping || execution_cpu.current->cancel_delivering) return EACCDN;
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
    KERNEL_TASK *task=execution_cpu.current;
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
    KERNEL_TASK *task = execution_cpu.current;
    return scheduler.active && task &&
           (task->state == TASK_RUNNING || task->state == TASK_BLOCKED) ? task->execution.identity : 0;
}

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
#if CONF_WITH_CUIS
    if (cuis_child_pid_reserved(pid)) return TRUE;
#endif
    if (!scheduler.active) return FALSE;
    for (task = scheduler.tasks; task; task = task->next_task)
        if (task->state != TASK_UNUSED && task->pid == pid) return TRUE;
    return FALSE;
}

/* Called before a process's record/basepage is freed, including nested
 * synchronous children. No orphan keeps a pointer to its former parent. */
void ksched_exiting(PD *pd, UWORD result)
{
    KERNEL_TASK *task;
    KPROC *exiting;
#if CONF_WITH_CUIS
    BOOL asynchronous = FALSE;
#endif
    exiting=kproc_retain(pd);
    ksched_admission_owner_exited(exiting);
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST
    lifetime_probe_exiting(exiting,result);
#endif
    {
        KPROC *owner=NULL;
        KSCHED_ADMISSION waiter={0};
        RUNQUEUE_GUARD guard=runqueue_enter();
        if (execution_cpu.waiter_attached && exiting && exiting==execution_cpu.entry_owner) {
            execution_cpu.waiter_attached=FALSE;
            memset(execution_cpu.waiter_resume,0,sizeof(execution_cpu.waiter_resume));
            execution_cpu.entry_ready=NULL; execution_cpu.entry_argument=NULL;
            owner=execution_cpu.entry_owner; execution_cpu.entry_owner=NULL;
            waiter=execution_cpu.waiter; execution_cpu.waiter.identity=0;
        }
        runqueue_leave(guard);
        if (owner) kproc_release(owner);
        ksched_admission_release(&waiter);
    }
    if (!scheduler.active) {
#if CONF_WITH_CUIS
        PD *parent=kproc_notification_parent(pd);
        if (parent) cuis_child_completed(parent,kproc_pid(pd),result);
#endif
        kproc_release(exiting); return;
    }
    for (task = scheduler.tasks; task; task = task->next_task) {
        if (task->state == TASK_UNUSED || task->state == TASK_RESERVED) continue;
        if (task->parent == pd) {
            task->parent = NULL;
            if (task->state == TASK_ZOMBIE) task->state = TASK_DONE;
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

#if CONF_WITH_CUIS
/* The record and allocations have gone. Compare the old identity only;
 * neither the exiting process nor its cleanup can resume after this hook. */
static void activate_successors(KPROC *process, PD *pd, BOOL use_identity)
{
    KERNEL_TASK *task;
    if (!scheduler.active) return;
    for (task = scheduler.tasks; task; task = task->next_task) {
        if (task->state == TASK_DEFERRED && task->deferred_owner == pd &&
            (!use_identity || !task->deferred_owner_identity ||
             task->deferred_owner_identity == process)) {
            if (task->deferred_owner_identity)
                kproc_release(task->deferred_owner_identity);
            task->deferred_owner_identity = NULL;
            task->deferred_owner = NULL;
            check_task_thread(task, 5);
            task->state = TASK_NEW;
        }
    }
}

void ksched_exited(PD *pd) { activate_successors(NULL, pd, FALSE); }

void ksched_exited_identity(KPROC *process, PD *pd)
{
    activate_successors(process, pd, TRUE);
}
#endif

void ksched_fault(PD *pd, WORD signal)
{
    KERNEL_TASK *task;
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
    for (task = scheduler.tasks; task; task = task->next_task) {
        if (execution_cpu.current && execution_cpu.current->program == pd && task != execution_cpu.current)
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
    KERNEL_TASK *task = execution_cpu.current;
    BOOL refused;
    check_task_thread(task, 2);
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
    longjmp(execution_cpu.resume, 1);
}

/* Owner exclusion spans snapshot/predicate/admission today. Under SMP, the
 * producer must pair its sequence update with leaf wake publication and the
 * consumer must retain the owner across this check. Never run predicates here. */
static BOOL block_admit(KERNEL_TASK *task, BOOL (*ready)(void *), void *argument,
                        const KSCHED_WAIT_OBSERVATION *observation)
{
    RUNQUEUE_GUARD guard=runqueue_enter();
    if (ksched_sequence_changed(observation)) {
        runqueue_leave(guard);
        return FALSE;
    }
    task->ready=ready;
    task->argument=argument;
    if (observation) task->observation=*observation;
    else { task->observation.source=NULL; task->observation.value=0; }
    task->state=TASK_BLOCKED;
    runqueue_leave(guard);
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
        RUNQUEUE_GUARD guard=runqueue_enter();
        task->ready = NULL;
        task->argument = NULL;
        task->observation.source=NULL;
        task->observation.value=0;
        runqueue_leave(guard);
    }
    return TRUE;
}
BOOL ksched_block(BOOL (*ready)(void *), void *argument)
{ return block_observed(ready,argument,NULL); }

BOOL ksched_cancellation_pending(void)
{
#if CONF_WITH_PTERMGRP
    KTHREAD *thread = ksched_exit_thread(run);
    return !scheduler.stopping && thread && !execution_cpu.current->cancel_delivering &&
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
        RUNQUEUE_GUARD guard=runqueue_enter();
        task->cancel_boundary = TRUE;
        runqueue_leave(guard);
    }
#endif
    block_observed(ready, argument, observation);
#if CONF_WITH_PTERMGRP
    {
        RUNQUEUE_GUARD guard=runqueue_enter();
        task->cancel_boundary = FALSE;
        runqueue_leave(guard);
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

static BOOL cancellation_wait(BOOL (*ready)(void *), void *argument)
{
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
    cancellation_wait(always_ready, NULL);
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

#if CONF_WITH_CUIS
static LONG spawn_configured(PD *pd, ULONG session, ULONG token, BOOL deferred);
static void discard_deferred(KERNEL_TASK *task)
{
#if !CONF_WITH_USER_ASPACE
    PD *program = task->program;
#endif
    KPROC *process = task->process;
    KPROC *owner = task->deferred_owner_identity;
    KTHREAD *thread = task->thread;
    check_task_thread(task, 6);
    /* The thread's backend pointer refers into task: keep that storage intact
     * until its reclaimer finishes. Neither this task nor its stack ever ran. */
    /* Unpublish before callbacks without erasing the backend storage needed
     * by the reclaimer. DONE also prevents reentrant spawn from reusing it. */
    task->state = TASK_DONE;
    task->program = NULL;
    task->thread = NULL;
    task->deferred_owner = NULL;
    task->deferred_owner_identity = NULL;
    /* It never entered user mode; resource ownership is already the child's.
     * Do not read its basepage after revoking the loader's borrowed mapping. */
#if CONF_WITH_USER_ASPACE
    proc_dispose_process(process);
#else
    proc_dispose_deferred(program);
#endif
    if (thread) kthread_destroy(thread);
    kproc_release(process);
    if (owner) kproc_release(owner);
    clear_task(task);
}
LONG ksched_defer_cancel(void)
{
    KERNEL_TASK *task;
    if (!ksched_async_allowed()) return EINVFN;
    for (task = async_tasks(); task; task = task->next_task)
        if (task->state == TASK_DEFERRED && task->deferred_owner == run)
            discard_deferred(task);
    return E_OK;
}
LONG ksched_spawn(PD *pd) { return spawn_configured(pd,0,0,FALSE); }
LONG ksched_spawn_observed(PD *pd, ULONG session, ULONG token)
{ return spawn_configured(pd,session,token,FALSE); }
LONG ksched_defer(PD *pd, ULONG session)
{ return spawn_configured(pd,session,0,TRUE); }
static LONG spawn_configured(PD *pd, ULONG session, ULONG token, BOOL deferred)
#else
LONG ksched_spawn(PD *pd)
#endif
{
    KERNEL_TASK *task;
    void *reservation = NULL;
    LONG reserved;
    if (!ksched_async_allowed()) return EINVFN;
#if CONF_WITH_CUIS
    if (deferred && cuis_service(CUIS_SESSION_QUERY,session) != E_OK) return EACCDN;
#endif
    if (next_identity == ~0ULL) return EAGAIN;
    if (!kproc_unlaunched(pd) || kproc_user_root(pd)) return EIMBA;
    for (task = scheduler.tasks; task; task = task->next_task)
        if (task->program == pd) return EACCDN;
    reserved = ksched_task_reserve(FALSE, &reservation);
    if (reserved != E_OK) return reserved;
    task = reservation;
    /* Reserve the image identity too: recursive preparation must not start
     * a second construction for the same PD in a different reserved slot. */
    task->program = pd;
    if (!kthread_stack_allocate(&task->execution.stack)) {
        ksched_task_cancel(&reservation);
        return ENSMEM;
    }
    task->process = kproc_retain(pd);
    if (!task->process) {
        kthread_stack_release(&task->execution.stack);
        ksched_task_cancel(&reservation);
        return ENSMEM;
    }
#if CONF_WITH_CUIS
    if (!deferred && (session || token)) {
        LONG result = cuis_child_bind_spawn(session,token,pd);
        if (result != E_OK) {
            kproc_release(task->process);
            kthread_stack_release(&task->execution.stack);
            ksched_task_cancel(&reservation);
            return result;
        }
    }
#endif
#if CONF_WITH_CUIS
    /* Acquire the predecessor identity before image preparation can consume
     * a staged image. No fallible acquisitions may follow that transfer. */
    if (deferred) {
        task->deferred_owner_identity = kproc_retain(run);
        if (!task->deferred_owner_identity && kproc_pid(run)) {
            kproc_release(task->process);
            kthread_stack_release(&task->execution.stack);
            ksched_task_cancel(&reservation);
            return ENSMEM;
        }
    }
#endif
    /* The optional THREAD is constructed inside preparation, before its
     * irreversible image move; publication and stack transfer cannot fail. */
    if (!kproc_prepare_async_thread(pd, run, ksched_controller_pd(), &task->thread)) {
#if CONF_WITH_CUIS
        if (task->deferred_owner_identity) kproc_release(task->deferred_owner_identity);
#endif
        kproc_release(task->process);
        kthread_stack_release(&task->execution.stack);
        ksched_task_cancel(&reservation);
        return ENSMEM;
    }
    adopt_task_execution(task);
    task->pid = kproc_pid(pd);
    task->parent = run;
    task->asynchronous = TRUE;
    task->take_ownership = TRUE;
    kproc_commit_async(pd);
#if CONF_WITH_CUIS
    if (deferred) {
        /* Publish only after every fallible preparation. Requests are detached
         * shell handoffs, not children the exiting caller must later reap. */
        ksched_defer_cancel();
        task->parent = NULL;
        task->deferred_owner = run;
        publish_task(&reservation, TASK_DEFERRED);
        return E_OK;
    }
#endif
    ksched_task_publish(&reservation);
    return task->pid;
}

typedef struct { PD *parent; WORD pid; } CHILD_WAIT;
static BOOL child_ready(void *argument)
{
    CHILD_WAIT *wait = argument;
    KERNEL_TASK *task;
    BOOL found = FALSE;
    for (task = async_tasks(); task; task = task->next_task) {
        if (!task->asynchronous || task->completion_owner || task->state == TASK_UNUSED || task->state == TASK_RESERVED || task->parent != wait->parent ||
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
    KERNEL_TASK *task;
    if (scheduler.stopping) return EACCDN;
    if (options & ~3) return EINVAL;
    /* No process-group or stopped-process model is implemented yet. */
    if (!pid || pid < -1) return EINVFN;
    if (!scheduler.active) return EFILNF;
    wait.parent = run; wait.pid = pid;
    flags = native_sched_irq_save();
    for (;;) {
        BOOL found = FALSE;
        for (task = async_tasks(); task; task = task->next_task) {
            ULONG status;
            if (!task->asynchronous || task->completion_owner || task->state == TASK_UNUSED || task->state == TASK_RESERVED || task->parent != run ||
                (pid > 0 && task->pid != pid)) continue;
            found = TRUE;
            if (task->state != TASK_ZOMBIE || task->dependents) continue;
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
            if(!execution_cpu.entered && table_empty()) free_task_table();
            native_sched_irq_restore(flags);
            return (LONG)status;
        }
        if (!found || (options & 1)) {
            native_sched_irq_restore(flags);
            return found ? 0 : EFILNF;
        }
        if (!ksched_spawn_allowed()) {
            LONG error;
            native_sched_irq_restore(flags);
            error=enter_root(FALSE,child_ready,&wait);
            if(error) return error;
        } else {
            cancellation_wait(child_ready, &wait);
            native_sched_irq_restore(flags);
        }
#if CONF_WITH_PTERMGRP
        ksched_cancel_point();
#endif
        flags = native_sched_irq_save();
    }
}

/* Called only after the scheduler has switched back to its root context.
 * A zombie keeps its process identity for waiters, but its continuation and
 * stack can never run again. Repeated passes leave already-reaped stacks alone. */
static BOOL reap_finished_on_root(KERNEL_TASK *task)
{
    runqueue_unlocked();
    if (execution_cpu.current || task->execution.on_cpu || (task->state != TASK_DONE && task->state != TASK_ZOMBIE && task->state != TASK_DRAINING))
        return FALSE;
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
        task->thread = NULL;
    }
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST
    lifetime_probe_reap(task);
#endif
    if (task->completion_owner && !task->thread) {
        RUNQUEUE_GUARD guard=runqueue_enter();
        KERNEL_TASK *anchor=task->completion_owner;
        if (!anchor->dependents) panic("scheduler: anchor reference underflow");
        task->completion_owner=NULL;
        anchor->dependents--;
        runqueue_leave(guard);
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
    return task->cancel_boundary && !task->cancel_delivering &&
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
        if (scheduler.stopping) panic("scheduler: dispatch during shutdown");
        runqueue_unlocked(); /* predicates and notifications are outside leaf exclusion */
        check_root_stack();
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
        for (task = scheduler.tasks; task; task = task->next_task)
            reap_finished_on_root(task);
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
            if (task->state == TASK_UNUSED || task->state == TASK_RESERVED || task->state == TASK_DONE || task->state == TASK_ZOMBIE) continue;
            if (task->state == TASK_DRAINING) { pending = TRUE; continue; }
            pending = TRUE;
#if CONF_WITH_CUIS
            if (task->state == TASK_DEFERRED) continue;
#endif
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
        RUNQUEUE_GUARD guard=runqueue_enter();
        execution_cpu.current=NULL;
        memset(&execution_cpu.root,0,sizeof(execution_cpu.root));
        execution_cpu.parked=TRUE; execution_cpu.entered=FALSE;
        execution_cpu.entry_ready=NULL; execution_cpu.entry_argument=NULL;
        owner=execution_cpu.entry_owner; execution_cpu.entry_owner=NULL;
        runqueue_leave(guard);
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
    RUNQUEUE_GUARD guard=runqueue_enter();
    execution_cpu.pump=pump; execution_cpu.budget=0;
    for(task=scheduler.tasks;task;task=task->next_task)
        if(task->state!=TASK_UNUSED && task->state!=TASK_RESERVED && task->state!=TASK_DONE && task->state!=TASK_ZOMBIE)
            execution_cpu.budget++;
    execution_cpu.entry_ready=ready; execution_cpu.entry_argument=argument;
    execution_cpu.entered=TRUE; execution_cpu.waiter_attached=TRUE; execution_cpu.current=NULL;
    runqueue_leave(guard);
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
        RUNQUEUE_GUARD guard=runqueue_enter();
        execution_cpu.waiter_attached=FALSE;
        memset(execution_cpu.waiter_resume,0,sizeof(execution_cpu.waiter_resume));
        runqueue_leave(guard);
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
    RUNQUEUE_GUARD guard=runqueue_enter();
    KERNEL_TASK *task = *slot;
    if(scheduler.stopping) panic("scheduler: publication after shutdown");
    *slot = NULL;
    __asm__ volatile ("" ::: "memory");
    task->state = state;
    runqueue_leave(guard);
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
    if (!ksched_spawn_allowed() || completion_task(execution_cpu.current)->dependents==~(ULONG)0 ||
        execution_cpu.current->process != source ||
        execution_cpu.current->program != run || kproc_live_pd(source) != run) return FALSE;
    *controller = ksched_controller_pd(); return *controller!=NULL;
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
        if (!ksched_spawn_allowed() || execution_cpu.current->process != process || !syscall_stack || !syscall_stack->physical)
            panic("sibling publication context changed");
        {
        RUNQUEUE_GUARD guard=runqueue_enter();
        task->completion_owner = completion_task(execution_cpu.current);
        if (task->completion_owner->dependents==~(ULONG)0)
            panic("scheduler: unprepared anchor reference");
        task->completion_owner->dependents++;
        runqueue_leave(guard);
        }
        task->asynchronous = TRUE; /* root recycles TID slots, never zombies */
        task->execution.syscall_stack.physical = syscall_stack->physical;
        task->execution.syscall_stack.top = syscall_stack->top;
        syscall_stack->physical = syscall_stack->top = 0;
    } else if (logical_parent) {
        if (execution_cpu.current->process != logical_parent || kproc_live_pd(logical_parent) != run)
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
