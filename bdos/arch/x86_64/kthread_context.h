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
    UQUAD physical, top;
} KTHREAD_STACK;

typedef struct {
    UQUAD identity;
    KTHREAD_STACK stack;
    X86_64_PROCESS_CONTEXT cpu;
    jmp_buf resume;
} KTHREAD_CONTEXT;

/* The context owns this scheduler stack after successful allocation. Release
 * is only permitted from another stack, after its continuation cannot run. */
BOOL kthread_stack_allocate(KTHREAD_STACK *stack);
void kthread_stack_release(KTHREAD_STACK *stack);
/* Move ownership out before clearing a context; the detached stack must later
 * be released from a different execution stack. */
KTHREAD_STACK kthread_stack_detach(KTHREAD_STACK *stack);

#endif
