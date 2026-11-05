/* Trusted, kernel-owned clone construction. No GEMDOS ABI or user pointers. */
#ifndef PROC_CLONE_H
#define PROC_CLONE_H

#include "portab.h"
struct kproc;
struct kthread;
typedef struct proc_clone_transaction PROC_CLONE_TRANSACTION;

typedef enum {
    PROC_CLONE_SAME_PROCESS,
    PROC_CLONE_PREPARED_PROCESS
} PROC_CLONE_PROCESS_POLICY;
typedef enum { PROC_CLONE_SHARE_VM, PROC_CLONE_PRIVATE_VM } PROC_CLONE_VM_POLICY;

typedef struct {
    /* Borrowed live identities; caller retains them throughout prepare.
     * SAME_PROCESS shares the source's existing process-wide resources.
     * PREPARED_PROCESS requires a distinct, empty, caller-owned PROCESS with
     * independently initialized descriptors/CWD/limits. The transaction does
     * not allocate a public basepage, inherit resources or consume the PROCESS.
     * The caller's existing loader/resource factory remains its release owner. */
    struct kproc *source;
    struct kproc *prepared_process; /* NULL for SAME_PROCESS */
    PROC_CLONE_PROCESS_POLICY process_policy;
    PROC_CLONE_VM_POLICY vm_policy;
} PROC_CLONE_ARGS;

typedef struct {
    struct kthread *thread; /* owning, permanently non-runnable in this slice */
    WORD pid;
    ULONG tid;
} PROC_CLONE_RESULT;

/* Prepare only. Success owns a transaction, retaining identities, shared VM
 * and an unpublished THREAD. Failure leaves *transaction unchanged. Unknown
 * policies and same-PROCESS/private-VM fail EINVAL; independent private VM
 * fails EINVFN. No outputs or scheduler publication.
 * All stages require cooperative, serialized process context without yields
 * or user code. Teardown callbacks obey the existing no-unwind/bounded-close
 * contract. Commit revalidates live state. */
LONG proc_clone(const PROC_CLONE_ARGS *args, PROC_CLONE_TRANSACTION **transaction);
/* Commit transfers the non-runnable THREAD to result. A prepared target also
 * adopts an owning VM reference. No CPU context/stack/startup is installed.
 * Failure leaves result and transaction unchanged and rollback remains valid. */
LONG proc_clone_commit(PROC_CLONE_TRANSACTION **transaction, PROC_CLONE_RESULT *result);
/* Abort is idempotent through this handle and never completes a PROCESS.
 * Caller-prepared PROCESS resources remain with their original release owner. */
void proc_clone_abort(PROC_CLONE_TRANSACTION **transaction);

#if CONF_WITH_X86_64_MEMTEST
/* Inject failure after source, target, VM or THREAD acquisition (1..4). */
void proc_clone_test_fail_after(UWORD stage);
#endif
#endif
