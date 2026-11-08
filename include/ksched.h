/* Generic cooperative native scheduling; initially selected by CUIS. */
#ifndef KSCHED_H
#define KSCHED_H
#include "portab.h"
#include "bdosdefs.h"
#define KSCHED_LIMIT 8U
/* All programs must be prepared, unlaunched and distinct. Success consumes
 * them, writes their exit statuses, and returns after all tasks finish.
 * Failure before launch leaves every program with the caller. */
LONG ksched_run(PD * const *programs, UWORD count, LONG *results);
/* Bootstrap an unprepared ordinary synchronous launch. Allocations precede
 * image preparation. A retained PE_GO image keeps
 * its caller's ownership; other launches transfer ownership at user entry.
 * Success consumes the launch and returns its full synchronous exit status. */
LONG ksched_exec(PD *program, BOOL take_ownership, LONG *result);
BOOL ksched_active(void);
#if CONF_WITH_NATIVE_SCHED
struct thread_startup_draft;
struct thread_startup_stack;
struct proc_clone_transaction;
struct kproc;
struct kthread;
/* Shared publication storage for legacy process launches and trusted clone.
 * root=TRUE reserves a fresh group; FALSE requires the current managed task.
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
BOOL ksched_test_clone_masked(void);
#endif
void ksched_task_cancel(void **slot);
void ksched_task_publish(void **slot);
BOOL ksched_startup_reclaimable(const struct thread_startup_draft *);
/* Backends mask dispatch/IRQs and validate that source owns the current task.
 * Root is borrowed only while its blocked continuation owns the group. */
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
/* Borrow the current task identity only for its own process, never a nested
 * synchronous child. Finish is called off the child syscall stack, before
 * the common PROCESS finalizer; task-stack reclamation remains root-only. */
struct kthread *ksched_exit_thread(PD *pd);
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
LONG ksched_process_prepare(PD *pd, void **slot);
LONG ksched_process_publish(void **slot);
void ksched_process_cancel(void **slot);
/* Internal observed launch, available only with CUIS. */
LONG ksched_spawn_observed(PD *pd, ULONG session, ULONG token);
LONG ksched_defer(PD *pd, ULONG session);
LONG ksched_defer_cancel(void);
void proc_dispose_deferred(PD *program);
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
BOOL ksched_parent_pid(PD *pd, WORD *pid);
void ksched_exiting(PD *pd, UWORD status);
/* CUIS successor activation after complete teardown; pd is identity only. */
void ksched_exited(PD *pd);
#if CONF_WITH_USER_ASPACE
void ksched_exited_identity(struct kproc *process, PD *pd);
#endif
void ksched_fault(PD *pd, WORD signal);
LONG proc_prepare_scheduled(PD *pd);
LONG proc_start_scheduled(PD *pd, BOOL take_ownership);
void proc_discard_scheduled(PD *pd);
#endif
