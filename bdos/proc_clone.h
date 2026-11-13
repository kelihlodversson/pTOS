/* Trusted, kernel-owned clone construction. No GEMDOS ABI or user pointers. */
#ifndef PROC_CLONE_H
#define PROC_CLONE_H

#include "portab.h"
#include "thread_startup.h"
#include "process_completion.h"
struct _pd;
struct kaspace;
struct kproc;
struct kthread;
typedef struct proc_clone_transaction PROC_CLONE_TRANSACTION;
typedef struct proc_clone_observer PROC_CLONE_OBSERVER;
struct syscall_return;
typedef struct { WORD pid; ULONG tid; } PROC_CLONE_IDENTITIES;
typedef struct { WORD pid; ULONG tid; LONG status; } PROC_CLONE_COMPLETION;
typedef enum { PROC_CLONE_OBSERVER_PREPARED, PROC_CLONE_OBSERVER_PUBLISHED,
               PROC_CLONE_OBSERVER_REAPED, PROC_CLONE_OBSERVER_CANCELLED } PROC_CLONE_OBSERVER_STATE;

typedef enum {
    PROC_CLONE_SAME_PROCESS,
    PROC_CLONE_PREPARED_PROCESS,
    PROC_CLONE_NEW_PROCESS,
    PROC_CLONE_LOADED_PROCESS
} PROC_CLONE_PROCESS_POLICY;
typedef enum { PROC_CLONE_SHARE_VM, PROC_CLONE_PRIVATE_VM,
               PROC_CLONE_PREPARED_VM } PROC_CLONE_VM_POLICY;
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
    /* LOADED_PROCESS/PREPARED_VM only. source is the loader-created target,
     * not its parent. The loader retains this construction space and owns
     * image/basepage/environment preparation. Ignored for other policies. */
    struct kaspace *prepared_space;
} PROC_CLONE_ARGS;

typedef struct {
    struct kthread *thread; /* owning non-runnable identity result; no execution publication */
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
 * policies and same-PROCESS/private-VM fail EINVAL. Independent PRIVATE_VM
 * requires the experimental backend and its CPU/mapping runtime eligibility;
 * an unavailable backend fails EINVFN. This does not enable public fork. No outputs or scheduler publication.
 * All stages require cooperative, serialized process context without yields
 * or user code. Teardown callbacks obey the existing no-unwind/bounded-close
 * contract. Commit revalidates live state. */
LONG proc_clone(const PROC_CLONE_ARGS *args, PROC_CLONE_TRANSACTION **transaction);
#if CONF_WITH_NATIVE_SCHED
/* Authenticated loader target only: NEW_PROCESS+SHARE_VM without a duplicate
 * PID/basepage factory. Abort preserves caller's staged PROCESS; no user ABI. */
LONG proc_clone_loaded_shared(struct kproc *,struct kproc *,PROC_CLONE_TRANSACTION **);
#endif
/* Existing loader initial THREAD, non-runnable. Uses the same transaction,
 * does not adopt/publish the PROCESS space or consume loader resources. */
struct kthread *proc_clone_prepare_loaded_thread(struct _pd *pd, struct kaspace *space);
/* Commit requires an initialized, empty result (PROC_CLONE_RESULT_INIT).
 * A nonempty result returns EINVAL before any ownership transfer.
 * Commit transfers the non-runnable THREAD to result. A new or prepared target
 * also adopts an owning VM reference; NEW_PROCESS transfers factory ownership. No CPU context/stack/startup is installed.
 * Failure leaves result and transaction unchanged and rollback remains valid. */
LONG proc_clone_commit(PROC_CLONE_TRANSACTION **transaction, PROC_CLONE_RESULT *result);
/* Startup preparation acquires context/stacks, never execution ownership.
 * Identity-only commit still returns EINVFN with a draft attached. Only the
 * trusted NEW_PROCESS/SAME_PROCESS publication paths below can publish prepared execution. */
LONG proc_clone_prepare_startup(PROC_CLONE_TRANSACTION *transaction, const THREAD_STARTUP_ARGS *args);
LONG proc_clone_prepare_outputs(PROC_CLONE_TRANSACTION *transaction, VADDR pid, VADDR tid);
/* Heap-owned asynchronous observation: one caller reference plus one startup
 * registration. Release either order; completion is read from retained KPROC,
 * not published a second time. No callback borrows creator stack storage.
 * Abort marks CANCELLED with no invented status; root detach marks REAPED.
 * NEW_PROCESS snapshots its existing PROCESS completion (possibly later than
 * leader reap). SAME_PROCESS snapshots this TID's stop status before THREAD
 * destruction, independently of PROCESS completion. These neither consume a
 * PID zombie nor implement the deferred public Pthjoin/wait mechanisms. */
LONG proc_clone_observe_async(PROC_CLONE_TRANSACTION *, PROC_CLONE_OBSERVER **);
void proc_clone_observer_release(PROC_CLONE_OBSERVER **);
PROC_CLONE_OBSERVER_STATE proc_clone_observer_state(const PROC_CLONE_OBSERVER *);
BOOL proc_clone_observer_result(const PROC_CLONE_OBSERVER *, PROC_CLONE_COMPLETION *);
/* NEW_PROCESS only: authoritative rich PROCESS snapshot; SAME_PROCESS TID
 * results must never be mistaken for CH_EXIT/PROCESS completion. */
BOOL proc_clone_observer_process_snapshot(const PROC_CLONE_OBSERVER *, PROCESS_COMPLETION *);
LONG proc_clone_observe_startup(PROC_CLONE_TRANSACTION *transaction,
                                 const THREAD_STARTUP_OBSERVER *observer, void *context);
LONG proc_clone_seal_startup(PROC_CLONE_TRANSACTION *transaction);
LONG proc_clone_retain_startup_stack(PROC_CLONE_TRANSACTION *transaction);
#if CONF_WITH_NATIVE_SCHED
/* Kernel-only first execution: NEW_PROCESS/SHARE_VM, root group only. Failure
 * before publication preserves transaction and outputs; success consumes it
 * and waits using the existing cooperative scheduler, returning value ids. */
LONG proc_clone_exec(PROC_CLONE_TRANSACTION **transaction, PROC_CLONE_COMPLETION *completion);
/* Normalized public NEW_PROCESS or SAME_PROCESS/SHARE_VM request. Constructs and owns the
 * asynchronous observer, returns positive signed TID or an error, and retains
 * no caller-stack data after return. Requires the current managed task. */
LONG proc_clone_start_public(const PROC_CLONE_ARGS *, const THREAD_STARTUP_ARGS *, VADDR pid, VADDR tid);
#if CONF_WITH_PRIVATE_CLONE_TEST
LONG proc_clone_start_private_test(const PROC_CLONE_ARGS *, const THREAD_STARTUP_ARGS *);
#endif
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

#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_PRIVATE_CLONE_TEST
/* Inject failure after source, target, VM, THREAD, or owned-observer identity acquisition (1..5). */
/* Function-entry transaction stages; loader allocation faults use allocator hooks. */
void proc_clone_test_fail_after(UWORD stage);
void proc_clone_test_cancel_publication(void);
/* Environment, PD, KPROC, handles, CWD acquisition (1..5). */
void proc_clone_factory_test_fail_after(UWORD stage);
#endif
#endif
