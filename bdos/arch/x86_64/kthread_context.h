/* Per-execution-context state used by the cooperative scheduler. */
#ifndef X32_KTHREAD_CONTEXT_H
#define X32_KTHREAD_CONTEXT_H

#include "emutos.h"
#include "process.h"

/* This is an embedded compatibility bridge, not a public TID or independently
 * owned thread yet. Its token is never a PID. The scheduler owns its dispatch
 * stack and must reclaim it from the root context after the task stops. The
 * process syscall stack remains separate and is reclaimed by its launcher. */
typedef struct {
    UQUAD identity;
    UQUAD stack_phys, stack_top;
    X86_64_PROCESS_CONTEXT cpu;
    jmp_buf resume;
} KTHREAD_CONTEXT;

/* The context owns this scheduler stack after successful allocation. Release
 * is only permitted from another stack, after its continuation cannot run. */
BOOL kthread_stack_allocate(KTHREAD_CONTEXT *thread);
void kthread_stack_release(KTHREAD_CONTEXT *thread);

#endif
