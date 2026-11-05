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
 * resident mappings; it never faults in or writes user memory. Retains ASPACE
 * lifetime only. Individual mappings are NOT leased/pinned yet, so this draft
 * cannot authorize execution or output writes. Prepare requires *draft == NULL.
 * Calls are serialized/no-yield. Unsupported backends return EINVFN. */
LONG thread_startup_prepare(struct kaspace *space, const THREAD_STARTUP_ARGS *args,
                            THREAD_STARTUP_DRAFT **draft);
/* Current permission recheck, not a mapping-lease or publication guarantee. */
LONG thread_startup_validate(const THREAD_STARTUP_DRAFT *draft);
/* Never-executed stacks/context only. Idempotent through the cleared handle. */
void thread_startup_abort(THREAD_STARTUP_DRAFT **draft);
/* Borrowed immutable backend image, for internal architecture tests/planning.
 * It has no executable continuation or scheduler entry operation. */
const void *thread_startup_image(const THREAD_STARTUP_DRAFT *draft);
#endif
