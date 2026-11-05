/* Trusted, kernel-owned clone construction. No GEMDOS ABI or user pointers. */
#ifndef PROC_CLONE_H
#define PROC_CLONE_H

#include "portab.h"
struct kproc;
struct kthread;
typedef struct proc_clone_transaction PROC_CLONE_TRANSACTION;

typedef enum {
    PROC_CLONE_SAME_PROCESS,
    PROC_CLONE_PREPARED_PROCESS,
    PROC_CLONE_NEW_PROCESS
} PROC_CLONE_PROCESS_POLICY;
typedef enum { PROC_CLONE_SHARE_VM, PROC_CLONE_PRIVATE_VM } PROC_CLONE_VM_POLICY;
#define PROC_CLONE_COPY_STANDARD_HANDLES 1U
#define PROC_CLONE_COPY_CWD              2U
#define PROC_CLONE_RESOURCE_MASK         3U

typedef struct {
    /* Borrowed live identities; caller retains them throughout prepare.
     * SAME_PROCESS shares the source's existing process-wide resources.
     * PREPARED_PROCESS requires a distinct, empty, caller-owned PROCESS with
     * independently initialized descriptors/CWD/limits. The transaction does
     * not allocate a public basepage, inherit resources or consume the PROCESS.
     * The caller's existing loader/resource factory remains its release owner. */
    struct kproc *source;
    struct kproc *prepared_process; /* NULL for SAME_PROCESS and NEW_PROCESS */
    PROC_CLONE_PROCESS_POLICY process_policy;
    PROC_CLONE_VM_POLICY vm_policy;
    /* NEW_PROCESS only: independently select Pexec-style standard-handle and
     * CWD inheritance. Explicit/nonstandard SFT handles are not duplicated.
     * Environment is initially empty; loader/image/startup remain separate. */
    UWORD resources;
} PROC_CLONE_ARGS;

typedef struct {
    struct kthread *thread; /* owning, permanently non-runnable in this slice */
    WORD pid;
    ULONG tid;
    /* NEW_PROCESS only: owns identity reference plus disposal responsibility.
     * Use proc_clone_release(), not just kthread_destroy(), for this result. */
    struct kproc *owned_process;
} PROC_CLONE_RESULT;

/* Prepare only: *transaction must be NULL. A nonempty handle returns EINVAL
 * without acquisitions or replacement. Success owns a transaction, retaining identities, shared VM
 * and an unpublished THREAD. Failure leaves *transaction unchanged. Unknown
 * policies and same-PROCESS/private-VM fail EINVAL; independent private VM
 * fails EINVFN. No outputs or scheduler publication.
 * All stages require cooperative, serialized process context without yields
 * or user code. Teardown callbacks obey the existing no-unwind/bounded-close
 * contract. Commit revalidates live state. */
LONG proc_clone(const PROC_CLONE_ARGS *args, PROC_CLONE_TRANSACTION **transaction);
/* Commit transfers the non-runnable THREAD to result. A new or prepared target
 * also adopts an owning VM reference; NEW_PROCESS transfers factory ownership. No CPU context/stack/startup is installed.
 * Failure leaves result and transaction unchanged and rollback remains valid. */
LONG proc_clone_commit(PROC_CLONE_TRANSACTION **transaction, PROC_CLONE_RESULT *result);
/* Abort is idempotent through this handle and never completes a PROCESS.
 * Caller-prepared PROCESS resources remain with their original release owner;
 * NEW_PROCESS resources are disposed by rollback. */
void proc_clone_abort(PROC_CLONE_TRANSACTION **transaction);
/* Consume a non-runnable result once. Requests disposal only for a factory-
 * owned PROCESS, then releases THREAD and identity. Other THREADs retain live
 * resources until their own release; no exit status is invented. */
void proc_clone_release(PROC_CLONE_RESULT *result);
/* Existing allocator/resource factory, without VM/THREAD/loader/startup. Source
 * must be the current live PROCESS; result must be empty. Success transfers a
 * retained identity plus disposal obligation. Unknown policies fail EINVAL. */
LONG proc_create_clone_process(struct kproc *source, UWORD resources, struct kproc **result);

#if CONF_WITH_X86_64_MEMTEST
/* Inject failure after source, target, VM or THREAD acquisition (1..4). */
void proc_clone_test_fail_after(UWORD stage);
/* Environment, PD, KPROC, handles, CWD acquisition (1..5). */
void proc_clone_factory_test_fail_after(UWORD stage);
#endif
#endif
