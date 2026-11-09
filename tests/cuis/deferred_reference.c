/* Exercise the scheduler's real deferred cancellation and ownership order. */
#include <stdlib.h>
#include <stdio.h>
#include "emutos.h"
#include "string.h"
#include "scheduler_irq_model.h"
#include "../../bdos/native_sched.c"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "deferred reference:%d: %s\n", __LINE__, #c); abort(); } } while (0)

static PD owner, successor, other;
static KERNEL_TASK tasks[KSCHED_LIMIT];
static unsigned phase, owner_refs, root_attempts;
PD *run;
static BOOL committing;
BOOL kproc_exec_commit_begin(KPROC *p)
{ CHECK(p==(KPROC *)&owner); if(committing)return FALSE; committing=TRUE; return TRUE; }
void kproc_exec_commit_end(KPROC *p)
{ CHECK(p==(KPROC *)&owner && committing); committing=FALSE; }


static void reenter_root(void)
{
    KERNEL_TASK *previous = scheduler.current;
    CHECK(scheduler_model_flags == 0x200);
    CHECK(tasks[1].state == TASK_RETIRING && !tasks[1].process &&
          !tasks[1].parent && !tasks[1].program && !tasks[1].thread &&
          !tasks[1].deferred_owner_identity && tasks[1].execution.stack.physical == 123);
    scheduler.current = NULL; /* Model cleanup yielding to the verified root. */
    CHECK(!reap_finished_on_root(&tasks[1]));
    scheduler.current = previous;
    {
        void *nested = NULL;
        CHECK(ksched_task_reserve(FALSE, &nested) == E_OK && nested != &tasks[1]);
        ksched_task_cancel(&nested);
        CHECK(!nested && tasks[1].state == TASK_RETIRING && tasks[1].execution.stack.physical == 123);
    }
    root_attempts++;
}
void proc_dispose_process(KPROC *process)
{
    CHILD_WAIT wait;
    CHECK(phase == 0 && process == (KPROC *)&successor);
    reenter_root(); /* Before THREAD destruction and PROCESS release. */
    wait.parent = &owner; wait.pid = 42;
    CHECK(child_ready(&wait)); /* No unpublished/retiring public child exists. */
    CHECK(ksched_defer_cancel() == EACCDN); /* Selection is already in flight. */
    ksched_exited_identity((KPROC *)&owner, &owner); /* Reentrant owner teardown. */
    CHECK(tasks[1].state == TASK_RETIRING && tasks[2].state == TASK_DEFERRED);
    phase++;
}
BOOL kthread_reap(KTHREAD *thread)
{
    (void)thread;
    abort(); /* A private retiring THREAD must never reach public root reap. */
}

KPROC *kproc_retain(PD *pd)
{ CHECK(pd==&owner); owner_refs++; return (KPROC *)pd; }
void kproc_release(KPROC *process)
{
    if (process==(KPROC *)&owner) { CHECK(owner_refs); owner_refs--; return; }
    CHECK(phase == 2 && process == (KPROC *)&successor && scheduler_model_flags == 0x200);
    CHECK(tasks[1].state == TASK_RETIRING && !tasks[1].execution.stack.physical);
    phase++;
}

void kthread_destroy(KTHREAD *thread)
{
    CHECK(phase == 1 && thread == (KTHREAD *)&successor);
    reenter_root(); /* Repeated root pass still cannot steal local ownership. */
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
    scheduler.initial = 1;
    scheduler.tasks = tasks;
    scheduler.current = &tasks[0];
    tasks[0].state = TASK_RUNNING;
    tasks[1].state = TASK_DEFERRED;
    tasks[1].pid = 42;
    tasks[1].parent = &owner; /* A prepared async cancellation can retain this. */
    tasks[1].program = &successor;
    tasks[1].process = (KPROC *)&successor;
    tasks[1].thread = (KTHREAD *)&successor;
    tasks[1].deferred_owner_identity = (KPROC *)&owner; owner_refs=1;
    tasks[1].execution.stack.physical = 123;
    tasks[2].state = TASK_DEFERRED;
    tasks[2].deferred_owner_identity = (KPROC *)&other;
    CHECK(ksched_defer_cancel() == E_OK && phase == 3 && !owner_refs);
    CHECK(tasks[2].state == TASK_DEFERRED && root_attempts == 2);
    CHECK(tasks[1].state == TASK_UNUSED && !tasks[1].process && !tasks[1].thread);
    { /* The same slot is reusable only after both destructors/references end. */
        void *slot = NULL;
        CHECK(ksched_task_reserve(FALSE, &slot) == E_OK && slot == &tasks[1]);
        ksched_task_cancel(&slot);
        CHECK(!slot && tasks[1].state == TASK_UNUSED);
    }
    CHECK(ksched_defer_cancel() == E_OK && phase == 3 && !owner_refs);
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

void *kalloc(ULONG bytes) { (void)bytes; abort(); }
void kfree(void *pointer) { (void)pointer; abort(); }
