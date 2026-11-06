#ifndef THREAD_RETURN_H
#define THREAD_RETURN_H
#include "portab.h"
#include <aspace_types.h>
struct kaspace;
struct aspace;
struct kproc;
struct kthread;
typedef struct thread_return_bridge THREAD_RETURN_BRIDGE;
/* Kernel-only immutable image. Return uses Pterm and the existing thread-only
 * launcher handoff; only the final THREAD tears down PROCESS resources.
 * A bridge never grants clone publication capability. */
LONG thread_return_bridge_acquire(struct kaspace *space, THREAD_RETURN_BRIDGE **result);
VADDR thread_return_bridge_zero_address(const THREAD_RETURN_BRIDGE *bridge);
VADDR thread_return_bridge_address(const THREAD_RETURN_BRIDGE *bridge);
BOOL thread_return_bridge_valid(const THREAD_RETURN_BRIDGE *bridge);
BOOL thread_return_bridge_matches(const THREAD_RETURN_BRIDGE *bridge, const struct aspace *space);
/* No sibling PROCESS resource teardown is permitted by this bridge. */
BOOL thread_return_bridge_process_eligible(struct kproc *process, struct kthread *thread);
void thread_return_bridge_release(THREAD_RETURN_BRIDGE **bridge);
#endif
