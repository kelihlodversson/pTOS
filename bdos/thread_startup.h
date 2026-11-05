/* Kernel-owned startup planning. Drafts are never scheduler/publication tokens. */
#ifndef THREAD_STARTUP_H
#define THREAD_STARTUP_H
#include "portab.h"
#include <aspace_types.h>
struct kaspace;
typedef struct thread_startup_draft THREAD_STARTUP_DRAFT;
typedef struct {
    VADDR entry, argument, stack_base;
    ULONG stack_size;
    /* Kernel-selected user return bridge. The draft validates its mapping,
     * not thread-exit semantics; #893 integration is still a publication gate. */
    VADDR return_entry;
} THREAD_STARTUP_ARGS;
/* All arguments are normalized kernel values, not a user structure. Requires
 * resident mappings; backing/COW must be completed before prepare. It never
 * faults in or writes user memory. Retains ASPACE
 * lifetime and revocable mapping leases. This draft still cannot authorize
 * execution or output writes. Prepare requires *draft == NULL.
 * Calls are serialized/no-yield. Unsupported backends return EINVFN. */
LONG thread_startup_prepare(struct kaspace *space, const THREAD_STARTUP_ARGS *args,
                            THREAD_STARTUP_DRAFT **draft);
/* A revoked lease stays invalid even if the VA/permissions are restored. */
LONG thread_startup_validate(const THREAD_STARTUP_DRAFT *draft);
typedef enum {
    THREAD_STARTUP_CONSTRUCTED, /* transaction has identities; no draft yet */
    THREAD_STARTUP_PREPARED,    /* image, stacks and startup leases */
    THREAD_STARTUP_OBSERVED,    /* reversible lifecycle registration */
    THREAD_STARTUP_SEALED,      /* protected, no-callback publication window */
    THREAD_STARTUP_COMMITTED,   /* reserved: ownership/output commit, NOT reached */
    THREAD_STARTUP_RUNNABLE     /* reserved: scheduler publication, NOT reached */
} THREAD_STARTUP_STATE;
/* Up to two optional 32-bit identity outputs. Each nonzero address acquires
 * its own writable lease. No writes; all-or-nothing, before observe. Stack/code
 * overlap and overlapping outputs are rejected. Repeated preparation fails. */
LONG thread_startup_prepare_outputs(THREAD_STARTUP_DRAFT *draft, VADDR pid, VADDR tid);
typedef struct {
    /* Kernel callbacks must not yield, unwind, reenter or cancel this draft.
     * Failed acquire unwinds its own partial registration. Successful acquire
     * transfers one registration to the draft, released exactly once on abort. */
    LONG (*acquire)(void *context);
    void (*release)(void *context);
} THREAD_STARTUP_OBSERVER;
LONG thread_startup_observe(THREAD_STARTUP_DRAFT *draft,
                            const THREAD_STARTUP_OBSERVER *observer, void *context);
/* Revalidate and seal every lease together, including prepared outputs.
 * No allocation/callback/user write. Abort leaves the window before callbacks.
 * Still NOT a trusted return bridge or an executable/publication capability. */
LONG thread_startup_seal(THREAD_STARTUP_DRAFT *draft);
THREAD_STARTUP_STATE thread_startup_state(const THREAD_STARTUP_DRAFT *draft);
/* EIMBA for stale mappings, EINVFN otherwise: the kernel-selected #893 return
 * bridge and no-fail ownership/scheduler transition are not installed. An
 * arbitrary executable return_entry never authorizes execution. */
LONG thread_startup_publication_check(const THREAD_STARTUP_DRAFT *draft);
/* Never-executed stacks/context only. Idempotent through the cleared handle. */
void thread_startup_abort(THREAD_STARTUP_DRAFT **draft);
/* Borrowed immutable backend image, for internal architecture tests/planning.
 * It has no executable continuation or scheduler entry operation. */
const void *thread_startup_image(const THREAD_STARTUP_DRAFT *draft);
#endif
