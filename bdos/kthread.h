/* Non-runnable thread identities.  Execution startup belongs to #892. */
#ifndef KTHREAD_H
#define KTHREAD_H

#include "portab.h"

typedef struct kthread KTHREAD;
struct kproc;
struct kaspace;
struct _pd;

/* Create a detached-from-scheduler identity associated with a prepared process.
 * The caller owns the result. Detached-process disposal waits for its release.
 * It cannot execute; normal xterm remains guarded.
 * All operations require serialized process context. */
KTHREAD *kthread_create(struct _pd *pd);
/* Transfer a backend execution resource to a non-runnable thread. The
 * reclaimer must not block or exit non-locally and runs only during reap.
 * Failure leaves ownership with the caller. */
BOOL kthread_set_execution(KTHREAD *thread, void *resource, void (*reclaim)(void *));
/* Stop preserves membership and all references until safe-context reap.
 * Repeated stop returns FALSE. No user thread startup is provided here. */
BOOL kthread_stop(KTHREAD *thread);
/* Borrowed state for process completion preflight. */
BOOL kthread_is_stopped(const KTHREAD *thread);
/* Reclaim a stopped thread after its continuation cannot resume and the
 * caller has switched away from every stack owned by that thread. Returns
 * FALSE if it has not stopped; success consumes the thread. */
BOOL kthread_reap(KTHREAD *thread);
/* From a safe stack, finish the sole remaining stopped thread of a started
 * process. The recorded parent must remain valid through notification; native
 * scheduler integration is not yet supported. Failure leaves the thread and
 * its resources owned by the caller. */
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
ULONG kthread_test_set_last_tid(ULONG tid);
#endif

#endif
