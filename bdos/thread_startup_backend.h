#ifndef THREAD_STARTUP_BACKEND_H
#define THREAD_STARTUP_BACKEND_H
#include "thread_startup.h"
struct aspace;
/* Backend-owned trusted bridge capability; no architecture type leaks into
 * the generic transaction or caller-provided startup arguments. */
LONG thread_startup_arch_bridge_acquire(struct kaspace *space, void **bridge);
BOOL thread_startup_arch_bridge_valid(const void *bridge);
VADDR thread_startup_arch_bridge_address(const void *bridge);
void thread_startup_arch_bridge_release(void **bridge);
/* Success transfers an unpublished backend object. Failure keeps *image empty.
 * return_bridge is a borrowed kernel-created backend capability, not a VA;
 * it must match this ASPACE and outlive the image. No backend may write user
 * memory or install the object in a running THREAD. */
LONG thread_startup_arch_prepare(struct aspace *space, const THREAD_STARTUP_ARGS *args, const void *return_bridge,
                                 void **image);
LONG thread_startup_arch_validate(const struct aspace *space, const THREAD_STARTUP_ARGS *args);
void thread_startup_arch_abort(void *image);
#endif
