/* Kernel-owned startup planning; execution requires the trusted publication transaction. */
#ifndef THREAD_STARTUP_H
#define THREAD_STARTUP_H
#include "portab.h"
#include <aspace_types.h>
struct kaspace;
struct kaspace_lease;
typedef struct thread_startup_draft THREAD_STARTUP_DRAFT;
typedef struct thread_startup_stack { PADDR physical; uintptr_t top; } THREAD_STARTUP_STACK;
typedef struct { THREAD_STARTUP_STACK syscall, scheduler; } THREAD_STARTUP_STACK_OWNERS;
typedef struct {
    VADDR entry, argument, stack_base;
    ULONG stack_size;
} THREAD_STARTUP_ARGS;
/* All arguments are normalized kernel values, not a user structure. Requires
 * resident mappings; backing/COW must be completed before prepare. It never
 * faults in or writes user memory. Retains ASPACE
 * lifetime and revocable mapping leases. Preparation alone cannot authorize
 * execution or output writes. Return code identity is selected/owned entirely
 * by the backend, not supplied in THREAD_STARTUP_ARGS. Requires *draft == NULL.
 * Calls are serialized/no-yield. Unsupported backends return EINVFN. */
LONG thread_startup_prepare(struct kaspace *space, const THREAD_STARTUP_ARGS *args,
                            THREAD_STARTUP_DRAFT **draft);
#if CONF_WITH_SHARED_VM_PEXEC_TEST
/* Trusted loader-only standard ELF entry. initial_sp addresses argc in the
 * already-built resident stack; publication never inserts a C return frame.
 * This is not a public clone option or arbitrary saved CPU-state restore. */
LONG thread_startup_prepare_elf(struct kaspace *,const THREAD_STARTUP_ARGS *,
                              VADDR initial_sp,THREAD_STARTUP_DRAFT **);
#endif
/* Public void entry returns through the kernel-selected zero-status stub. */
LONG thread_startup_prepare_public(struct kaspace *, const THREAD_STARTUP_ARGS *, THREAD_STARTUP_DRAFT **);
#if CONF_WITH_PRIVATE_CLONE
LONG thread_startup_prepare_cloned(struct kaspace *, struct kaspace *, const THREAD_STARTUP_ARGS *,
                                  BOOL public_return, THREAD_STARTUP_DRAFT **);
#endif
/* A revoked lease stays invalid even if the VA/permissions are restored. */
LONG thread_startup_validate(const THREAD_STARTUP_DRAFT *draft);
typedef enum {
    THREAD_STARTUP_CONSTRUCTED, /* transaction has identities; no draft yet */
    THREAD_STARTUP_PREPARED,    /* image, stacks and startup leases */
    THREAD_STARTUP_OBSERVED,    /* reversible lifecycle registration */
    THREAD_STARTUP_SEALED,      /* protected, no-callback publication window */
    THREAD_STARTUP_COMMITTED,   /* private NEW_PROCESS runner only */
    THREAD_STARTUP_RUNNABLE,    /* private NEW_PROCESS runner only */
    THREAD_STARTUP_RECLAIMABLE /* stopped continuation, verified scheduler root */
} THREAD_STARTUP_STATE;
/* Up to two optional 32-bit identity outputs. Each nonzero address acquires
 * its own writable lease. No writes; all-or-nothing, before observe. Stack/code
 * overlap and overlapping outputs are rejected. Repeated preparation fails. */
LONG thread_startup_prepare_outputs(THREAD_STARTUP_DRAFT *draft, VADDR pid, VADDR tid);
typedef struct {
    /* Kernel callbacks must not yield, unwind, reenter or cancel this draft.
     * Failed acquire unwinds its own partial registration. Successful acquire
     * transfers one registration, released once on abort or stopped-root reclaim. */
    LONG (*acquire)(void *context);
    void (*release)(void *context);
} THREAD_STARTUP_OBSERVER;
LONG thread_startup_observe(THREAD_STARTUP_DRAFT *draft,
                            const THREAD_STARTUP_OBSERVER *observer, void *context);
/* Revalidate and seal every lease together, including prepared outputs.
 * No allocation/callback/user write. Abort leaves the window before callbacks.
 * Sealing alone is not an executable/publication capability. */
LONG thread_startup_seal(THREAD_STARTUP_DRAFT *draft);
/* Acquire a separate durable user-stack hold before publication. Transfer it
 * to KTHREAD; the short startup seal remains independent. No execution grant. */
LONG thread_startup_retain_stack(THREAD_STARTUP_DRAFT *draft, struct kaspace_lease **hold);
THREAD_STARTUP_STATE thread_startup_state(const THREAD_STARTUP_DRAFT *draft);
/* EIMBA for stale mappings, EINVFN otherwise: the bridge currently supports
 * sole-THREAD PROCESS exit only. This identity-only check cannot authorize the
 * private NEW_PROCESS runner; no arbitrary return PC input. */
LONG thread_startup_publication_check(const THREAD_STARTUP_DRAFT *draft);
#if CONF_WITH_NATIVE_SCHED
/* Internal NEW_PROCESS runner only. Preflight caches sealed-write aliases;
 * commit/publish have no allocations, callbacks, yields or failure return. */
LONG thread_startup_run_preflight(THREAD_STARTUP_DRAFT *draft);
void thread_startup_commit_stores(THREAD_STARTUP_DRAFT *, WORD pid, ULONG tid);
void thread_startup_publish(THREAD_STARTUP_DRAFT *);
void thread_startup_entering(THREAD_STARTUP_DRAFT *);
void thread_startup_reclaim(THREAD_STARTUP_DRAFT **);
BOOL thread_startup_take_stacks(THREAD_STARTUP_DRAFT *, THREAD_STARTUP_STACK_OWNERS *);
#endif
/* Unpublished cancellation only; published objects reject abort. Final
 * execution cleanup uses root-verified reclaim. Cleared handles are idempotent. */
void thread_startup_abort(THREAD_STARTUP_DRAFT **draft);
/* Borrowed immutable backend image. Reading it does not grant execution;
 * only the trusted publication transaction can transfer its execution ownership. */
const void *thread_startup_image(const THREAD_STARTUP_DRAFT *draft);
#endif
