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
struct kthread;
struct kproc;
/* Borrow the current task identity only for its own process, never a nested
 * synchronous child. Finish is called off the child syscall stack, before
 * the common PROCESS finalizer; task-stack reclamation remains root-only. */
struct kthread *ksched_exit_thread(PD *pd);
void ksched_finish_exit(struct kproc *process, PD *pd);
/* Saved with kernel process contexts, including legacy AES coroutines.
 * Zero is unmanaged; identities are never reused, even across groups. */
UQUAD ksched_context_id(void);
void ksched_context_restore(UQUAD identity);
/* Called with IRQs masked. Predicates run with IRQs masked and must not
 * allocate, block or access user memory. FALSE means no managed task. */
BOOL ksched_block(BOOL (*ready)(void *), void *argument);
/* The same wait for callers that do not already hold an IRQ mask. */
BOOL ksched_wait_ready(BOOL (*ready)(void *), void *argument);
/* Standard async process calls are admitted only within a managed group.
 * Spawn consumes an unprepared, loaded basepage only on success. */
BOOL ksched_spawn_allowed(void);
LONG ksched_spawn(PD *pd);
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
#endif
