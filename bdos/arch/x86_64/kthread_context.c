/* Cooperative execution-context stack ownership. */
#include "emutos.h"
#include "procmem.h"
#include "kthread_context.h"

#if CONF_WITH_X86_64_MEMTEST
static UWORD fail_stack_after;
void kthread_stack_test_fail_after(UWORD count) { fail_stack_after = count; }
#endif

BOOL kthread_stack_allocate(KTHREAD_STACK *stack)
{
    PADDR physical;
    uintptr_t top;
    if (!stack || stack->physical) return FALSE;
#if CONF_WITH_X86_64_MEMTEST
    if (fail_stack_after && !--fail_stack_after) return FALSE;
#endif
    physical = kstack_alloc(&top);
    if (!physical) return FALSE;
    stack->physical = physical;
    stack->top = top;
    return TRUE;
}

void kthread_stack_release(KTHREAD_STACK *stack)
{
    PADDR physical;
    if (!stack) return;
    physical = stack->physical;
    stack->physical = 0;
    stack->top = 0;
    if (physical) kstack_free(physical);
}

KTHREAD_STACK kthread_stack_detach(KTHREAD_STACK *stack)
{
    KTHREAD_STACK detached = { 0, 0 };
    if (stack) {
        detached = *stack;
        stack->physical = 0;
        stack->top = 0;
    }
    return detached;
}
