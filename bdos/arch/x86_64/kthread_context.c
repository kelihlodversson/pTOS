/* Cooperative execution-context stack ownership. */
#include "emutos.h"
#include "procmem.h"
#include "kthread_context.h"

BOOL kthread_stack_allocate(KTHREAD_CONTEXT *thread)
{
    PADDR physical;
    uintptr_t top;
    if (!thread || thread->stack_phys) return FALSE;
    physical = kstack_alloc(&top);
    if (!physical) return FALSE;
    thread->stack_phys = physical;
    thread->stack_top = top;
    return TRUE;
}

void kthread_stack_release(KTHREAD_CONTEXT *thread)
{
    PADDR physical;
    if (!thread) return;
    physical = thread->stack_phys;
    thread->stack_phys = 0;
    thread->stack_top = 0;
    if (physical) kstack_free(physical);
}
