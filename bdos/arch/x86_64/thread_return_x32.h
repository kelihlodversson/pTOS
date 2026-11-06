#ifndef THREAD_RETURN_X32_H
#define THREAD_RETURN_X32_H
#include "portab.h"
#include <aspace_types.h>
struct kaspace;
struct aspace;
struct kproc;
struct kthread;
typedef struct x32_return_bridge X32_RETURN_BRIDGE;
/* Kernel-only immutable image. This revision terminates a sole-THREAD PROCESS
 * through existing Pterm/xterm + launcher finalization. It cannot terminate a
 * SAME_PROCESS sibling and never grants clone publication capability. */
LONG x32_return_bridge_acquire(struct kaspace *space, X32_RETURN_BRIDGE **result);
VADDR x32_return_bridge_zero_address(const X32_RETURN_BRIDGE *bridge);
VADDR x32_return_bridge_address(const X32_RETURN_BRIDGE *bridge);
BOOL x32_return_bridge_valid(const X32_RETURN_BRIDGE *bridge);
BOOL x32_return_bridge_matches(const X32_RETURN_BRIDGE *bridge, const struct aspace *space);
/* No sibling PROCESS resource teardown is permitted by this bridge. */
BOOL x32_return_bridge_process_eligible(struct kproc *process, struct kthread *thread);
void x32_return_bridge_release(X32_RETURN_BRIDGE **bridge);
#endif
