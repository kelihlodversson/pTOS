/* Exercise the scheduler's real deferred cancellation and ownership order. */
#include <stdlib.h>
#include <stdio.h>
#include "emutos.h"
#include "string.h"
#include "../../bdos/arch/x86_64/native_sched.c"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "deferred reference:%d: %s\n", __LINE__, #c); abort(); } } while (0)

static PD owner, successor, other;
static KERNEL_TASK tasks[KSCHED_LIMIT];
static unsigned phase;
PD *run;

KTHREAD_STACK kthread_stack_detach(KTHREAD_STACK *stack)
{
    KTHREAD_STACK result = *stack;
    CHECK(phase == 0 && result.physical == 123);
    stack->physical = stack->top = 0;
    phase++;
    return result;
}

#if CONF_WITH_USER_ASPACE
void proc_dispose_process(KPROC *process)
{
    CHECK(phase == 1 && process == (KPROC *)&successor);
    CHECK(tasks[1].state == TASK_UNUSED && !tasks[1].program && !tasks[1].process);
    phase++;
}
#else
void proc_dispose_deferred(PD *pd)
{
    CHECK(phase == 1 && pd == &successor);
    CHECK(tasks[1].state == TASK_UNUSED && !tasks[1].program && !tasks[1].process);
    phase++;
}
#endif

void kproc_release(KPROC *process)
{
    CHECK(phase == 2 && process == (KPROC *)&successor);
    phase++;
}

void kthread_stack_release(KTHREAD_STACK *stack)
{
    CHECK(phase == 3 && stack->physical == 123);
    phase++;
}

int main(void)
{
    run = &owner;
    scheduler.active = TRUE;
    scheduler.initial = 1;
    scheduler.tasks = tasks;
    scheduler.current = &tasks[0];
    tasks[0].state = TASK_RUNNING;
    tasks[1].state = TASK_DEFERRED;
    tasks[1].program = &successor;
    tasks[1].process = (KPROC *)&successor;
    tasks[1].deferred_owner = &owner;
    tasks[1].execution.stack.physical = 123;
    tasks[2].state = TASK_DEFERRED;
    tasks[2].deferred_owner = &other;
    CHECK(ksched_defer_cancel() == E_OK && phase == 4);
    CHECK(tasks[2].state == TASK_DEFERRED);
    CHECK(ksched_defer_cancel() == E_OK && phase == 4);
    puts("Deferred cancellation releases its process reference and stack once: PASS");
    return 0;
}
