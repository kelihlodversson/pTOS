/* Exercise the scheduler's real deferred cancellation and ownership order. */
#include <stdlib.h>
#include <stdio.h>
#include "emutos.h"
#include "string.h"
#include "../../bdos/native_sched.c"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "deferred reference:%d: %s\n", __LINE__, #c); abort(); } } while (0)

static PD owner, successor, other;
static KERNEL_TASK tasks[KSCHED_LIMIT];
static unsigned phase;
PD *run;

#if CONF_WITH_USER_ASPACE
void proc_dispose_process(KPROC *process)
{
    CHECK(phase == 0 && process == (KPROC *)&successor);
    CHECK(tasks[1].state == TASK_DONE && !tasks[1].program && !tasks[1].thread);
    phase++;
}
#else
void proc_dispose_deferred(PD *pd)
{
    CHECK(phase == 0 && pd == &successor);
    CHECK(tasks[1].state == TASK_DONE && !tasks[1].program && !tasks[1].thread);
    phase++;
}
#endif

void kproc_release(KPROC *process)
{
    CHECK(phase == 2 && process == (KPROC *)&successor);
    phase++;
}

void kthread_destroy(KTHREAD *thread)
{
    CHECK(phase == 1 && thread == (KTHREAD *)&successor);
    phase++;
    kthread_stack_release(&tasks[1].execution.stack);
}

void kthread_stack_release(KTHREAD_STACK *stack)
{
    CHECK(phase == 2 && stack->physical == 123);
    stack->physical = stack->top = 0;
}

int main(void)
{
    run = &owner;
    scheduler.active = TRUE;
    scheduler.tasks = tasks; initialize_task_table();
    scheduler.current = &tasks[0];
    tasks[0].state = TASK_RUNNING;
    tasks[1].state = TASK_DEFERRED;
    tasks[1].program = &successor;
    tasks[1].process = (KPROC *)&successor;
    tasks[1].thread = (KTHREAD *)&successor;
    tasks[1].deferred_owner = &owner;
    tasks[1].execution.stack.physical = 123;
    tasks[2].state = TASK_DEFERRED;
    tasks[2].deferred_owner = &other;
    CHECK(ksched_defer_cancel() == E_OK && phase == 3);
    CHECK(tasks[2].state == TASK_DEFERRED);
    CHECK(ksched_defer_cancel() == E_OK && phase == 3);
    puts("Deferred cancellation releases its process reference and stack once: PASS");
    return 0;
}

/* Legacy scheduler fixtures must never acquire clone startup ownership. */
void thread_startup_abort(THREAD_STARTUP_DRAFT **startup)
{
    if (startup && *startup) abort();
}

void thread_startup_reclaim(THREAD_STARTUP_DRAFT **startup)
{
    (void)startup;
    abort(); /* no published clone exists in this legacy fixture */
}

void ksched_admission_release(KSCHED_ADMISSION *h) { if(h->identity) abort(); }

void panic(const char *format,...) { (void)format; abort(); }

KPROC *kproc_retain(PD *p) { return (KPROC *)p; }
PD *kproc_live_pd(const KPROC *p) { return (PD *)p; }
