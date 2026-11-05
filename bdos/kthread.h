/* Non-runnable thread identities.  Execution startup belongs to #892. */
#ifndef KTHREAD_H
#define KTHREAD_H

#include "portab.h"

typedef struct kthread KTHREAD;
struct kproc;
struct kaspace;
struct _pd;

/* Create a detached-from-scheduler identity associated with a prepared process.
 * The caller owns the result and must destroy it before process cleanup starts.
 * It cannot execute, own an execution stack, or defer process teardown yet.
 * All operations require serialized process context. */
KTHREAD *kthread_create(struct _pd *pd);
void kthread_destroy(KTHREAD *thread);
ULONG kthread_tid(const KTHREAD *thread);
struct kproc *kthread_process(const KTHREAD *thread); /* borrowed */
struct kaspace *kthread_aspace(const KTHREAD *thread); /* borrowed */

/* KPROC's borrowed membership links; internal to the ownership layer. */
KTHREAD **kthread_next_link(KTHREAD *thread);
#if CONF_WITH_X86_64_MEMTEST
/* Fail after association to exercise the complete constructor rollback. */
void kthread_test_fail_after_attach(void);
#endif

#endif
