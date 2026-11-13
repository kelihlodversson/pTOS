/* Generic execution identity and safe-context resource ownership. */
#ifndef KTHREAD_H
#define KTHREAD_H

#include "portab.h"

typedef struct kthread KTHREAD;
struct kproc;
struct kaspace;
struct kaspace_lease;
struct _pd;

/* Create a detached-from-scheduler identity associated with a prepared process.
 * The caller owns the result. Detached-process disposal waits for its release.
 * Execution may be published only by a backend that owns its continuation.
 * All operations require serialized process context. */
KTHREAD *kthread_create(struct _pd *pd);
/* Loader transaction: retain its explicit space without publishing it as the
 * PROCESS's active VM. No execution backend is installed by construction. */
KTHREAD *kthread_prepare(struct _pd *pd, struct kaspace *space);
/* Clone construction rejects the ordinary execution setter/start. Only the
 * validated clone publication transaction can activate it. */
KTHREAD *kthread_construct_identity(struct kproc *process, struct kaspace *space);
/* Transfer a backend execution resource to a non-runnable thread. The
 * reclaimer must not block or exit non-locally and runs only during reap.
 * Failure leaves ownership with the caller. */
BOOL kthread_set_execution(KTHREAD *thread, void *resource, void (*reclaim)(void *));
/* Publish a prepared backend execution context. Scheduling/blocking state is
 * separate from this execution lifetime. A started thread cannot be destroyed
 * until its backend stops its continuation. */
BOOL kthread_start(KTHREAD *thread);
#if CONF_WITH_NATIVE_SCHED
BOOL kthread_clone_run_ready(const KTHREAD *thread);
/* Only the validated clone publication transaction may call this. */
void kthread_clone_commit(KTHREAD *thread, void *resource, void (*reclaim)(void *));
#endif
/* Transfer a durable user-stack hold, including for a non-runnable identity.
 * Failure leaves *hold owned by caller. Reap releases it only after backend
 * execution resources are safely reclaimed. No start capability is granted. */
BOOL kthread_take_stack_hold(KTHREAD *thread, struct kaspace_lease **hold);
BOOL kthread_is_executing(const KTHREAD *thread);
/* On the resumed launcher stack, after abandoning the child syscall stack:
 * detach this thread, retaining its identity/ASPACE/execution resource until
 * safe scheduler-root reap. Only the final thread reserves PROCESS cleanup;
 * proc_finalize_exit remains the sole PROCESS completion producer. */
BOOL kthread_finish_handoff(KTHREAD *thread);
/* Safe launcher completion snapshot; no PROCESS completion is fabricated. */
BOOL kthread_finish_exit(KTHREAD *thread, UWORD status);
BOOL kthread_exit_status(const KTHREAD *thread, UWORD *status);
/* Stop preserves membership and all references until safe-context reap.
 * Repeated stop returns FALSE. The backend must prevent further resumption. */
BOOL kthread_stop(KTHREAD *thread);
/* Borrowed state for process completion preflight. */
BOOL kthread_is_stopped(const KTHREAD *thread);
/* Reclaim a stopped thread after its continuation cannot resume and the
 * caller has switched away from every stack owned by that thread. Returns
 * FALSE if it has not stopped; success consumes the thread. */
BOOL kthread_reap(KTHREAD *thread);
/* From a safe stack, finish the sole remaining stopped thread of a started
 * process. A tracked parent is retained as an identity; legacy untracked
 * kernel parents must remain valid through notification. Native scheduler
 * integration is not yet supported. Failure leaves the thread and its
 * resources owned by the caller. */
BOOL kthread_complete(KTHREAD *thread, UWORD status);
/* Convenience for a non-runnable thread, from a safe execution stack. */
void kthread_destroy(KTHREAD *thread);
ULONG kthread_tid(const KTHREAD *thread);
struct kproc *kthread_process(const KTHREAD *thread); /* borrowed */
struct kaspace *kthread_aspace(const KTHREAD *thread); /* borrowed */

#if CONF_WITH_PTHJOIN
/* One claim per TID; cancellation preserves the target for another joiner.
 * Caller owns an execution reference. Ready means backend/root reap finished.
 * A live caller/process retains resources; these records retain no heavy state. */
LONG kthread_join_begin(KTHREAD *caller, ULONG tid);
BOOL kthread_join_ready(void *caller);
BOOL kthread_join_status(KTHREAD *caller, UWORD *status);
void kthread_join_cancel(KTHREAD *caller);
void kthread_join_consume(KTHREAD *caller);
void kthread_forget_completions(struct kproc *process);
#endif

/* KPROC's borrowed membership links; internal to the ownership layer. */
KTHREAD **kthread_next_link(KTHREAD *thread);
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST || CONF_WITH_PRIVATE_CLONE_TEST
/* Fail after association to exercise the complete constructor rollback. */
void kthread_test_fail_attach_after(UWORD count);
ULONG kthread_test_live(void); /* constructed and not yet reaped */
#endif
#if CONF_WITH_X86_64_MEMTEST
void kthread_test_fail_after_attach(void);
ULONG kthread_test_set_last_tid(ULONG tid);
#endif

#endif
