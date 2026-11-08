/* Per-execution-context state used by the cooperative scheduler. */
#ifndef KTHREAD_CONTEXT_H
#define KTHREAD_CONTEXT_H

#include "emutos.h"
#include "native_sched_backend.h"
#include "procmem.h"
#if ARCH_X86_64 && CONF_WITH_USER_ASPACE
#include "arch/x86_64/irqframe.h"
#elif ARCH_ARM && CONF_WITH_USER_ASPACE
#include "arch/arm/irqframe.h"
#endif

/* Embedded backend for a generic KTHREAD. Its private continuation token is
 * neither PID nor TID. The KTHREAD owns this dispatch stack through a backend
 * reclaimer; the scheduler invokes reap from root after the task stops. The
 * syscall stack remains separately owned and reclaimed by the gouser launcher. */
typedef struct {
    PADDR physical;
    uintptr_t top;
} KTHREAD_STACK;

typedef struct {
    UQUAD identity;
    BOOL on_cpu; /* cleared only after verified scheduler-root handoff */
    UWORD owner_cpu;
    KTHREAD_STACK stack;
    KTHREAD_STACK syscall_stack; /* transferred once to the existing launcher */
    NATIVE_SCHED_CONTEXT cpu;
    jmp_buf resume;
#if ARCH_X86_64 && CONF_WITH_USER_ASPACE
    X86_USER_IRQ_CAPTURE interrupted_user; /* preallocated; no detached IRQ lifetime */
#elif ARCH_ARM && CONF_WITH_USER_ASPACE
    ARM_USER_IRQ_CAPTURE interrupted_user;
#endif
} KTHREAD_CONTEXT;

struct kaspace;
/* Borrowed only while the matching execution is pinned on this CPU. No refcount
 * changes, callbacks or allocation. A nested different PROCESS is ineligible. */
KTHREAD_CONTEXT *ksched_interrupt_target(struct _pd *pd, struct kaspace **space, ULONG *tid);

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
