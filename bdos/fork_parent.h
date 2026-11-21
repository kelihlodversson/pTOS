/* Exact-child COW-vfork dependency. Not a PROCESS waiter or reaper. */
#ifndef FORK_PARENT_H
#define FORK_PARENT_H
#include "portab.h"
struct kproc;
struct kthread;
typedef struct fork_parent_dependency FORK_PARENT_DEPENDENCY;
typedef enum { FORK_PARENT_PREPARED, FORK_PARENT_PUBLISHED,
               FORK_PARENT_RELEASED, FORK_PARENT_CANCELLED } FORK_PARENT_STATE;
typedef enum { FORK_PARENT_NONE, FORK_PARENT_EXIT, FORK_PARENT_EXEC,
               FORK_PARENT_ABORT, FORK_PARENT_CANCELLATION,
               FORK_PARENT_RETIRED, FORK_PARENT_INVALID } FORK_PARENT_REASON;
#if CONF_WITH_FORK_CORE
/* Current sole executing THREAD only. Installs one THREAD-owned dependency;
 * handle is borrowed until release. All acquisition precedes publication. */
LONG fork_parent_prepare(struct kthread *,struct kproc *,FORK_PARENT_DEPENDENCY **);
BOOL fork_parent_validate(const FORK_PARENT_DEPENDENCY *);
/* Same no-fail guard as child publication, before TASK_NEW. */
void fork_parent_publish(FORK_PARENT_DEPENDENCY *);
void fork_parent_cancel(FORK_PARENT_DEPENDENCY *,FORK_PARENT_REASON);
/* Cancellable blocking, no public zombie consumption or callbacks. Caller
 * releases dependency/other local owners BEFORE cancellation checkpoint. */
LONG fork_parent_wait(FORK_PARENT_DEPENDENCY *,FORK_PARENT_REASON *);
void fork_parent_release(FORK_PARENT_DEPENDENCY **);
/* Existing KTHREAD reap, after continuation/predicate unpublication. */
void fork_parent_retire(FORK_PARENT_DEPENDENCY **);
BOOL fork_parent_ready(void *);
FORK_PARENT_STATE fork_parent_state(const FORK_PARENT_DEPENDENCY *);
FORK_PARENT_REASON fork_parent_reason(const FORK_PARENT_DEPENDENCY *);
struct kproc *fork_parent_child(const FORK_PARENT_DEPENDENCY *);
UQUAD fork_parent_execution(const FORK_PARENT_DEPENDENCY *);
ULONG fork_parent_test_live(void);
#endif
#endif
