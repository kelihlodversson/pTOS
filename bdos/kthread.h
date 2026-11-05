/* Generic execution identity and safe-context resource ownership. */
#ifndef KTHREAD_H
#define KTHREAD_H

#include "portab.h"

typedef struct kthread KTHREAD;
struct kproc;
struct kaspace;
struct _pd;

/* Create a detached-from-scheduler identity associated with a prepared process.
 * The caller owns the result. Detached-process disposal waits for its release.
 * Execution may be published only by a backend that owns its continuation.
 * All operations require serialized process context. */
KTHREAD *kthread_create(struct _pd *pd);
/* Transfer a backend execution resource to a non-runnable thread. The
 * reclaimer must not block or exit non-locally and runs only during reap.
 * Failure leaves ownership with the caller. */
BOOL kthread_set_execution(KTHREAD *thread, void *resource, void (*reclaim)(void *));
/* Publish a prepared backend execution context. Scheduling/blocking state is
 * separate from this execution lifetime. A started thread cannot be destroyed
 * until its backend stops its continuation. */
BOOL kthread_start(KTHREAD *thread);
BOOL kthread_is_executing(const KTHREAD *thread);
/* On the resumed launcher stack, after abandoning the child syscall stack:
 * detach the sole thread from a reserved PROCESS exit handoff, retaining its
 * identity/ASPACE/execution resource until safe scheduler-root reap. This does
 * not publish completion; proc_finalize_exit remains its single owner. */
BOOL kthread_finish_handoff(KTHREAD *thread);
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

/* KPROC's borrowed membership links; internal to the ownership layer. */
KTHREAD **kthread_next_link(KTHREAD *thread);
#if CONF_WITH_X86_64_MEMTEST
/* Fail after association to exercise the complete constructor rollback. */
void kthread_test_fail_after_attach(void);
void kthread_test_fail_attach_after(UWORD count);
ULONG kthread_test_set_last_tid(ULONG tid);
#endif

#endif
