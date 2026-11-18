/* Existing execution cleanup ownership, shared by kernel operations. */
#ifndef KSCHED_CLEANUP_H
#define KSCHED_CLEANUP_H
#include "portab.h"
#include "bdosdefs.h"
/* Execution-owned cooperative cleanup scope. Zero-initialize before enter.
 * Pending queries/latches stay visible; only delivery is deferred. End validates
 * LIFO/execution ownership and never terminates. Checkpoint only after the
 * outer operation has settled all local resources. Not a fault/longjmp guard. */
typedef struct {
    struct kthread *thread;
    struct kproc *process;
    PD *caller;
    UQUAD execution;
    ULONG tid, depth;
    UQUAD ticket, parent_ticket;
    BOOL active;
} KSCHED_CLEANUP_SCOPE;
void ksched_cleanup_begin(KSCHED_CLEANUP_SCOPE *empty);
void ksched_cleanup_end(KSCHED_CLEANUP_SCOPE *scope);
BOOL ksched_cleanup_active(void);
#endif
