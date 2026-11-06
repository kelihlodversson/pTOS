/* Trusted, kernel-owned clone construction. No GEMDOS ABI or user pointers. */
#ifndef PROC_CLONE_H
#define PROC_CLONE_H

#include "portab.h"
#include "thread_startup.h"
struct kproc;
struct kthread;
typedef struct proc_clone_transaction PROC_CLONE_TRANSACTION;
typedef struct proc_clone_observer PROC_CLONE_OBSERVER;
typedef struct { WORD pid; ULONG tid; } PROC_CLONE_IDENTITIES;
typedef struct { WORD pid; ULONG tid; LONG status; } PROC_CLONE_COMPLETION;
typedef enum { PROC_CLONE_OBSERVER_PREPARED, PROC_CLONE_OBSERVER_PUBLISHED,
               PROC_CLONE_OBSERVER_REAPED, PROC_CLONE_OBSERVER_CANCELLED } PROC_CLONE_OBSERVER_STATE;

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
    /* Must be zero except for NEW_PROCESS. Independently select Pexec-style standard-handle and
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
/* Initialize before first commit. Release before reuse; commit never replaces
 * any live ownership or nonzero identity. */
#define PROC_CLONE_RESULT_INIT { NULL, 0, 0, NULL }

/* Prepare only: *transaction must be NULL. A nonempty handle returns EINVAL
 * without acquisitions or replacement. Success owns a transaction, retaining identities, shared VM
 * and an unpublished THREAD. Failure leaves *transaction unchanged. Unknown
 * policies and same-PROCESS/private-VM fail EINVAL; independent private VM
 * fails EINVFN. No outputs or scheduler publication.
 * All stages require cooperative, serialized process context without yields
 * or user code. Teardown callbacks obey the existing no-unwind/bounded-close
 * contract. Commit revalidates live state. */
LONG proc_clone(const PROC_CLONE_ARGS *args, PROC_CLONE_TRANSACTION **transaction);
/* Commit requires an initialized, empty result (PROC_CLONE_RESULT_INIT).
 * A nonempty result returns EINVAL before any ownership transfer.
 * Commit transfers the non-runnable THREAD to result. A new or prepared target
 * also adopts an owning VM reference; NEW_PROCESS transfers factory ownership. No CPU context/stack/startup is installed.
 * Failure leaves result and transaction unchanged and rollback remains valid. */
LONG proc_clone_commit(PROC_CLONE_TRANSACTION **transaction, PROC_CLONE_RESULT *result);
/* Startup preparation acquires context/stacks, never execution ownership.
 * Identity-only commit still returns EINVFN with a draft attached. Only the
 * trusted NEW_PROCESS publication paths below can publish prepared execution. */
LONG proc_clone_prepare_startup(PROC_CLONE_TRANSACTION *transaction, const THREAD_STARTUP_ARGS *args);
LONG proc_clone_prepare_outputs(PROC_CLONE_TRANSACTION *transaction, VADDR pid, VADDR tid);
/* Heap-owned asynchronous observation: one caller reference plus one startup
 * registration. Release either order; completion is read from retained KPROC,
 * not published a second time. No callback borrows creator stack storage.
 * Abort marks CANCELLED with no invented status; root detach marks REAPED.
 * These are nonblocking snapshots, not another PROCESS wait/reap mechanism. */
LONG proc_clone_observe_async(PROC_CLONE_TRANSACTION *, PROC_CLONE_OBSERVER **);
void proc_clone_observer_release(PROC_CLONE_OBSERVER **);
PROC_CLONE_OBSERVER_STATE proc_clone_observer_state(const PROC_CLONE_OBSERVER *);
BOOL proc_clone_observer_result(const PROC_CLONE_OBSERVER *, PROC_CLONE_COMPLETION *);
LONG proc_clone_observe_startup(PROC_CLONE_TRANSACTION *transaction,
                                 const THREAD_STARTUP_OBSERVER *observer, void *context);
LONG proc_clone_seal_startup(PROC_CLONE_TRANSACTION *transaction);
LONG proc_clone_retain_startup_stack(PROC_CLONE_TRANSACTION *transaction);
#if CONF_WITH_NATIVE_SCHED
/* Kernel-only first execution: NEW_PROCESS/SHARE_VM, root group only. Failure
 * before publication preserves transaction and outputs; success consumes it
 * and waits using the existing cooperative scheduler, returning value ids. */
LONG proc_clone_exec(PROC_CLONE_TRANSACTION **transaction, PROC_CLONE_COMPLETION *completion);
/* Owned observer required. Success consumes transaction and returns identities
 * immediately into an existing group; failures preserve it for abort except
 * explicit prepublication cancellation. No new waiter or public syscall. */
LONG proc_clone_spawn(PROC_CLONE_TRANSACTION **transaction, PROC_CLONE_IDENTITIES *created);
/* Private no-fail publication half, shared by root and active-group callers. */
void proc_clone_commit_prepared(PROC_CLONE_TRANSACTION *, void *slot);
#endif
const THREAD_STARTUP_DRAFT *proc_clone_startup(const PROC_CLONE_TRANSACTION *transaction);
/* Borrowed until transaction consumption. Observers must retain identity if
 * they need it across publication/completion; no public PD exposure. */
struct kproc *proc_clone_process(const PROC_CLONE_TRANSACTION *transaction);
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
/* Inject failure after source, target, VM, THREAD, or owned-observer identity acquisition (1..5). */
void proc_clone_test_fail_after(UWORD stage);
void proc_clone_test_cancel_publication(void);
/* Environment, PD, KPROC, handles, CWD acquisition (1..5). */
void proc_clone_factory_test_fail_after(UWORD stage);
#endif
#endif
