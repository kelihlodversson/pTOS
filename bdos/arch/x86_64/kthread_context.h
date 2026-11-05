/* Per-execution-context state used by the cooperative scheduler. */
#ifndef X32_KTHREAD_CONTEXT_H
#define X32_KTHREAD_CONTEXT_H

#include "emutos.h"
#include "process.h"

/* Embedded backend for a generic KTHREAD. Its private continuation token is
 * neither PID nor TID. The KTHREAD owns this dispatch stack through a backend
 * reclaimer; the scheduler invokes reap from root after the task stops. The
 * syscall stack remains separately owned and reclaimed by the gouser launcher. */
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

#if CONF_WITH_X86_64_MEMTEST
/* Fail the nth subsequent scheduler-stack allocation; zero disables. */
void kthread_stack_test_fail_after(UWORD count);
#endif

#endif
