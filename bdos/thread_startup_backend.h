#ifndef THREAD_STARTUP_BACKEND_H
#define THREAD_STARTUP_BACKEND_H
#include "thread_startup.h"
struct aspace;
/* Success transfers an unpublished backend object. Failure keeps *image empty.
 * No backend may write user memory or install the object in a running THREAD. */
LONG thread_startup_arch_prepare(struct aspace *space, const THREAD_STARTUP_ARGS *args,
                                 void **image);
LONG thread_startup_arch_validate(const struct aspace *space, const THREAD_STARTUP_ARGS *args);
void thread_startup_arch_abort(void *image);
#endif
