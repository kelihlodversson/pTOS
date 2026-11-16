/* Private same-image execution transport prerequisite for #970.
 * No loader/image adoption right and no committed-exec event. */
#ifndef EXEC_HANDOFF_H
#define EXEC_HANDOFF_H
#include "thread_startup.h"
struct kproc;
struct kthread;
struct _pd;
struct kaspace;
struct kaspace_lease;
typedef struct ksched_exec_handoff KSCHED_EXEC_HANDOFF;
#if CONF_WITH_EXEC_REPLACE
/* Trusted normalized arguments only. This experiment keeps the active root.
 * Success never returns; every error precedes ownership transfer. */
LONG ksched_exec_handoff_test(const THREAD_STARTUP_ARGS *, UWORD fail_after);
LONG ksched_exec_handoff_query(VADDR output);
BOOL kproc_exec_handoff_allowed(struct kproc *, struct kthread *);
/* Launcher-owned syscall stack: snapshot is borrowed; take is no-fail after
 * exact-launcher revalidation. Neither operation frees the executing stack. */
BOOL proc_exec_stack_snapshot(void **launcher, PADDR *physical);
void proc_exec_stack_take(void *launcher, PADDR physical);
/* Opaque root acknowledgment, checked against the scheduler's unique pending
 * handoff. Not an association bypass or a THREAD stop/completion capability. */
BOOL ksched_exec_handoff_quiescent(const KSCHED_EXEC_HANDOFF *,
                                  const struct kthread *, const void *execution);
BOOL ksched_exec_handoff_retiring(const THREAD_STARTUP_DRAFT *);
BOOL kthread_exec_handoff_ready(const struct kthread *, const void *execution);
void kthread_exec_handoff_stack(struct kthread *, const KSCHED_EXEC_HANDOFF *,
                               struct kaspace_lease **new_hold,
                               struct kaspace_lease **old_hold);
LONG thread_startup_prepare_handoff(struct kaspace *, const THREAD_STARTUP_ARGS *,
                                    THREAD_STARTUP_DRAFT **);
BOOL ksched_exec_handoff_launch(struct _pd *);
#endif
#endif
