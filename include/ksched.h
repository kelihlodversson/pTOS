/* Generic cooperative native scheduling, independent of GUI policy. */
#ifndef KSCHED_H
#define KSCHED_H
#include "portab.h"
#include "bdosdefs.h"
/* Initial arena and maximum synchronous bootstrap batch, not a runnable-task
 * limit. Active publication grows stable slots on demand; ENSMEM is retryable. */
#define KSCHED_LIMIT 8U
/* Dispatch/idle uses permanent kernel stack/context storage. An unmanaged
 * synchronous attachment returns when its own retained results are complete;
 * independent background tasks remain on the same queue. No caller result
 * buffer/CPU continuation is retained after that attachment returns.
 * Programs must be prepared, unlaunched and distinct. Success consumes them
 * and writes their final PROCESS statuses. Failure before launch leaves every
 * program with its caller. Bounded unmanaged Syield pumps pending work. */
LONG ksched_run(PD * const *programs, UWORD count, LONG *results);
/* Bootstrap an unprepared ordinary synchronous launch. Allocations precede
 * image preparation. A retained PE_GO image keeps
 * its caller's ownership; other launches transfer ownership at user entry.
 * Success consumes the launch and returns its full synchronous exit status. */
LONG ksched_exec(PD *program, BOOL take_ownership, LONG *result);
BOOL ksched_active(void); /* persistent runqueue exists */
BOOL ksched_dispatching(void); /* attached root execution, not queue lifetime */
BOOL ksched_async_allowed(void);
/* Irreversible system shutdown, not PROCESS cancellation. No resources are
 * destroyed here; callers must never resume dispatch after shutting files down. */
void ksched_shutdown_begin(void);
PD *ksched_launch_controller(PD *pd); /* current task only, never a nested execution */
#if CONF_WITH_NATIVE_SCHED
/* IRQ entry/owner publication only: no callback, allocation or dispatch.
 * The owner publishes durable readiness before ending its masked IRQ/owner
 * section. A registered wait retains the source; notification retains no
 * caller/driver pointer. Not an SMP wake/IPI interface. */
void ksched_irq_notify(void);
struct ksched_wait_sequence;
void ksched_wake_sequence(struct ksched_wait_sequence *source);
/* Architecture backend only. Borrowed chain points into still-live launcher
 * continuations pinned to this CPU, never into a retired/unattached caller.
 * Saved/restored with NATIVE_SCHED_CONTEXT; no allocation/reference changes.
 * CPU selection remains boot-CPU-only until SMP admission/locks exist. */
void *ksched_loaded_launcher(void);
void ksched_set_loaded_launcher(void *launcher);
/* Backend assertion only: never switch a leaf or callback continuation. */
void ksched_verify_switch_boundary(void);
struct thread_startup_draft;
struct thread_startup_stack;
struct proc_clone_transaction;
struct kproc;
struct kthread;
/* Execution-owned cooperative cleanup scope. Zero-initialize before enter.
 * Pending queries/latches stay visible; only delivery is deferred. End validates
 * LIFO/execution ownership and never terminates. Checkpoint only after the
 * outer operation has settled all local resources. Not a fault/longjmp guard. */
typedef struct {
    struct kthread *thread;
    struct kproc *process;
    PD *caller;
    UQUAD execution;
    ULONG tid, depth;
    UQUAD ticket, parent_ticket;
    BOOL active;
} KSCHED_CLEANUP_SCOPE;
void ksched_cleanup_begin(KSCHED_CLEANUP_SCOPE *empty);
void ksched_cleanup_end(KSCHED_CLEANUP_SCOPE *scope);
BOOL ksched_cleanup_active(void);
/* Shared publication storage for legacy process launches and trusted clone.
 * root=TRUE prepares a synchronous admission; FALSE reserves global async
 * storage from a live caller or the current managed task.
 * *slot must be NULL. TASK_RESERVED is neither runnable nor wait-visible and
 * cannot be reused by preparation callbacks. Calls are serialized/no-yield;
 * future preemption/SMP exclusion belongs to #713. This grants no THREAD start.
 * Caller rolls back resource owners before cancel, which consumes the handle.
 * All validation/ownership transfers precede publish: it consumes the handle
 * with no allocation, callback, yield or failure, then makes TASK_NEW visible.
 * Never cancel or dereference a consumed handle. */
LONG ksched_task_reserve(BOOL root, void **slot);
#if CONF_WITH_X86_64_MEMTEST
void ksched_test_fail_clone_slot(void);
ULONG ksched_test_retiring_root_passes(void);
BOOL ksched_test_clone_masked(void);
#endif
void ksched_task_cancel(void **slot);
void ksched_task_publish(void **slot);
BOOL ksched_startup_reclaimable(const struct thread_startup_draft *);
/* Backends mask dispatch/IRQs and validate that source owns the current task.
 * The compatibility controller is a pinned kernel-only basepage with no
 * PROCESS/PID; logical parent and synchronous result owners are separate. */
typedef struct { uintptr_t flags; } KSCHED_PUBLICATION_GUARD;
/* Nonmasking query during serialized preparation; begin revalidates it. */
BOOL ksched_clone_context(struct kproc *source, PD **controller);
BOOL ksched_clone_begin(struct kproc *source, PD **controller, KSCHED_PUBLICATION_GUARD *guard);
void ksched_clone_end(const KSCHED_PUBLICATION_GUARD *guard);
void ksched_clone_commit(void *slot, struct kproc *, struct kthread *, struct thread_startup_draft *,
                         struct thread_startup_stack *scheduler_stack, struct kproc *logical_parent,
                         BOOL same_process, struct thread_startup_stack *syscall_stack);
struct thread_startup_draft *ksched_thread_startup(PD *pd);
BOOL ksched_sibling_launch(PD *pd);
BOOL ksched_take_syscall_stack(PD *pd, struct thread_startup_stack *owner);
LONG ksched_clone_dispatch(void **slot, struct proc_clone_transaction *, LONG *status);
#endif
struct kthread;
struct kproc;
/* The THREAD that authentically executes `pd` on this CPU: the current task's
 * own, or the innermost associated nested execution's. Enclosing owners
 * suspended in a launcher, and stopped executions, authenticate nobody.
 * Finish is called off the child syscall stack, before the common PROCESS
 * finalizer; task-stack reclamation remains root-only. */
struct kthread *ksched_exit_thread(PD *pd);
#if CONF_WITH_NATIVE_SCHED
/* Synchronous nested PROCESS execution on the calling THREAD's carrier
 * (doc/native-scheduler-plan.txt, "Nested continuation ownership").
 * prepare: fallible and private; constructs the nested PROCESS's address
 *   space, syscall stack and trusted-factory initial THREAD. Returns E_OK,
 *   KSCHED_NESTED_UNMANAGED when the caller is no managed THREAD (the launch
 *   then keeps kproc_prepare_user()), or a negative error with nothing kept.
 * cancel: rolls a prepared, unentered association back.
 * enter: no-fail publication immediately before the launcher loads `pd`.
 * leave: no-fail restoration of the enclosing owner and reclamation of the
 *   stopped THREAD, from the common finalizer on the enclosing kernel stack.
 * Calls for a PROCESS without such an association are no-ops. */
#define KSCHED_NESTED_UNMANAGED 1L
LONG ksched_nested_prepare(PD *pd);
void ksched_nested_cancel(PD *pd);
void ksched_nested_enter(PD *pd);
void ksched_nested_leave(struct kproc *process);
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST || CONF_WITH_PRIVATE_CLONE_TEST
LONG ksched_test_nested(UWORD selector); /* counters 0-13, arm stage 0x10+n, 0x20 untracked */
BOOL ksched_test_nested_injected(UWORD stage);
void ksched_test_nested_published(struct kproc *process);
#endif
#endif
#if CONF_WITH_PTERMGRP
/* Only terminates at an owned syscall continuation; nested children defer it. */
void ksched_cancel_point(void);
void ksched_enter_exit(PD *pd); /* suppress recursive delivery through termination hooks */
LONG ksched_termgrp(UWORD status);
#endif
/* Finalizer ownership survives THREAD stop; nested foreign PROCESSes do not
 * use this task's public-wait publication boundary. No reference transfer. */
BOOL ksched_finalizing_process(const struct kproc *process);
BOOL ksched_finish_exit(struct kproc *process, PD *pd, UWORD status);
/* Saved with kernel process contexts, including legacy AES coroutines.
 * Zero is unmanaged; identities are never reused, even across groups. */
UQUAD ksched_context_id(void);
void ksched_context_restore(UQUAD identity);
/* Called with IRQs masked. Predicates run with IRQs masked and must not
 * allocate, block or access user memory. FALSE means no managed task. */
BOOL ksched_block(BOOL (*ready)(void *), void *argument);
/* The same wait for callers that do not already hold an IRQ mask. */
/* Explicit opt-in boundary: returns with the existing IRQ mask intact. No
 * cleanup callback or termination runs here. CANCELLED wins over readiness;
 * caller releases local state, restores IRQs, then checkpoints. Nested child
 * PROCESSes are never cancelled through their carrier task's group request. */
typedef enum { KSCHED_WAIT_UNMANAGED, KSCHED_WAIT_READY, KSCHED_WAIT_CANCELLED } KSCHED_WAIT_RESULT;
BOOL ksched_cancellation_pending(void); /* query only; never exits or consumes events */
KSCHED_WAIT_RESULT ksched_wait_cancelable(BOOL (*ready)(void *), void *argument);
#if CONF_WITH_NATIVE_SCHED
struct ksched_wait_observation;
/* Owner exclusion must span observe/check/admit; retain source and argument
 * through return. A generation change is a wake hint even if a competing
 * consumer made the predicate false. Revalidate under the owner before commit.
 * Current serialization is local IRQ exclusion, NOT an SMP owner lock. */
KSCHED_WAIT_RESULT ksched_wait_cancelable_observed(BOOL (*ready)(void *), void *argument,
                                                 const struct ksched_wait_observation *observation);
#endif
BOOL ksched_wait_ready(BOOL (*ready)(void *), void *argument);
/* Standard async process calls are admitted only within a managed group.
 * Spawn consumes an unprepared, loaded basepage only on success. */
BOOL ksched_spawn_allowed(void);
LONG ksched_spawn(PD *pd);
/* Successful preparation consumes a loaded unstarted image into private
 * TASK_RESERVED ownership, including its THREAD and execution stack. Nothing
 * is runnable/wait-visible. Failure leaves image ownership with the caller.
 * Cancel consumes a prepared slot and disposes off-stack, outside exclusion.
 * Publish consumes it exactly once, without allocation/copy/callback/yield.
 * These are serialized owner operations, not a new scheduler or runqueue. */
/* deferred retains the canonical predecessor before image transfer. */
LONG ksched_process_prepare(PD *pd, BOOL deferred, void **slot);
LONG ksched_process_publish(void **slot);
/* Commit preflight can reject reentrant selection without consuming the slot.
 * Deferred cleanup precedes no-fail publication; no callback runs in publish. */
LONG ksched_process_commit(void **slot);
void ksched_process_cancel(void **slot);
/* Idempotent cancellation of only the current PROCESS's selected held successor.
 * Uncommitted preparations and already runnable children are unaffected. */
LONG ksched_defer_cancel(void);
/* Internal observed launch, available only with CUIS. */
LONG ksched_spawn_observed(PD *pd, ULONG session, ULONG token);
LONG ksched_defer(PD *pd, ULONG session);
#if CONF_WITH_USER_ASPACE
struct kproc;
void proc_dispose_process(struct kproc *process);
#endif
LONG ksched_yield(void);
#if CONF_WITH_PTHJOIN
LONG ksched_join(ULONG tid, UQUAD status);
#endif
LONG ksched_wait(WORD pid, WORD options, UQUAD usage);
BOOL ksched_pid_reserved(WORD pid);
void ksched_exiting(PD *pd, UWORD status);
/* Generic successor activation after complete teardown; identity is retained. */
#if CONF_WITH_USER_ASPACE
void ksched_exited_identity(struct kproc *process, PD *pd);
#endif
void ksched_fault(PD *pd, WORD signal);
LONG proc_prepare_scheduled(PD *pd);
LONG proc_start_scheduled(PD *pd, BOOL take_ownership);
void proc_discard_scheduled(PD *pd);
#endif
