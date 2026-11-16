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
/* replacing: a Pexec(200) receipt holds the admission ticket (validated by
 * its owner); otherwise no replacement may be admitted at all. */
BOOL kproc_exec_handoff_allowed(struct kproc *, struct kthread *, BOOL replacing);
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
void kthread_exec_rebind(struct kthread *, const KSCHED_EXEC_HANDOFF *, struct kaspace **space);
LONG thread_startup_prepare_handoff(struct kaspace *, const THREAD_STARTUP_ARGS *,
                                    THREAD_STARTUP_DRAFT **);
BOOL ksched_exec_handoff_launch(struct _pd *);
/* Pexec(200) of a synchronous nested PROCESS. The architecture launcher:
 * ready checks that the loaded launch is this PD's own; relaunch jumps back
 * into it (no return). The launcher then calls commit (interrupts masked,
 * no failure) and, once on the committed root, retire. */
BOOL proc_exec_relaunch_ready(struct _pd *);
void proc_exec_relaunch(struct _pd *) NORETURN;
BOOL ksched_exec_nested_committing(const struct kthread *);
void kthread_exec_rebind_nested(struct kthread *, struct kaspace **space);
void ksched_exec_relaunch_commit(struct _pd *, PADDR *kstack_phys, uintptr_t *kstack_top);
void ksched_exec_relaunch_retire(void);
#endif
#endif
