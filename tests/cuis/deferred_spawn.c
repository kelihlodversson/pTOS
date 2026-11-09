/* Exercise real scheduler deferred construction, rollback and handoff. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "string.h"
#include "gemerror.h"
#include "scheduler_irq_model.h"
#include "../../bdos/native_sched.c"

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"deferred spawn:%d: %s\n",__LINE__,#c); abort(); } } while (0)
static PD owner, child;
static KERNEL_TASK tasks[KSCHED_LIMIT];
static unsigned references, owner_references, stacks, identities, preparations, commits, disposals, stack_releases;
static BOOL prepare_ok, identity_ok, owner_ok;
static void *execution;
static void (*reclaimer)(void *);
static BOOL stopped, inject_retirement;
PD *run;
static BOOL committing;
BOOL kproc_exec_commit_begin(KPROC *p)
{ CHECK(p==(KPROC *)&owner); if(committing)return FALSE; committing=TRUE; return TRUE; }
void kproc_exec_commit_end(KPROC *p)
{ CHECK(p==(KPROC *)&owner && committing); committing=FALSE; }


LONG cuis_service(ULONG function, UQUAD handle) { CHECK(function == CUIS_SESSION_QUERY && handle == 23); return E_OK; }
LONG cuis_child_bind_spawn(ULONG session, ULONG token, PD *pd) { (void)session; (void)token; (void)pd; abort(); }
BOOL kproc_unlaunched(PD *pd) { return pd == &child; }
ASPACE_ROOT kproc_user_root(PD *pd) { CHECK(pd == &child); return 0; }
KPROC *kproc_retain(PD *pd)
{ if (pd == &owner) { if (!owner_ok) return NULL; owner_references++; return (KPROC *)pd; } CHECK(pd == &child); references++; return (KPROC *)pd; }
void kproc_release(KPROC *p)
{ if (p == (KPROC *)&owner) { CHECK(owner_references); owner_references--; return; } CHECK(p == (KPROC *)&child && references); references--; }
BOOL kproc_prepare_async_thread(PD *p, PD *parent, PD *controller, KTHREAD **thread)
{
    CHECK(p == &child && parent == &owner && controller == &owner);
    /* Real preparation callback: its unpublished slot cannot be selected by
     * reentrant reservation. Cancel the nested storage without acquiring refs. */
    {
        void *nested = NULL;
        CHECK(tasks[1].state == TASK_RESERVED && tasks[1].program == &child);
        CHECK(ksched_spawn(&child) == EACCDN);
        CHECK(ksched_task_reserve(FALSE, &nested) == E_OK && nested == &tasks[2]);
        ksched_task_cancel(&nested);
        CHECK(!nested && tasks[1].state == TASK_RESERVED);
    }
    preparations++; *thread = NULL;
    if (!prepare_ok) return FALSE;
    *thread = kthread_create(p);
    return *thread != NULL;
}
void kproc_commit_async(PD *p) { CHECK(p == &child); commits++; }
WORD kproc_pid(PD *p) { if (p == &owner) return 7; CHECK(p == &child); return 42; }
BOOL kthread_stack_allocate(KTHREAD_STACK *s)
{ CHECK(!s->physical); stacks++; s->physical = 123; s->top = 456; return TRUE; }
KTHREAD_STACK kthread_stack_detach(KTHREAD_STACK *s)
{ KTHREAD_STACK result = *s; s->physical = s->top = 0; return result; }
void kthread_stack_release(KTHREAD_STACK *s)
{
    if (!s->physical) return;
    CHECK(s->physical == 123 && stacks);
    stack_releases++; stacks--; s->physical = s->top = 0;
}
KTHREAD *kthread_create(PD *p) { CHECK(p == &child); if (!identity_ok) return NULL; identities++; return (KTHREAD *)p; }
BOOL kthread_set_execution(KTHREAD *t, void *resource, void (*reclaim)(void *))
{ CHECK(t == (KTHREAD *)&child && identities == 1 && !execution); execution = resource; reclaimer = reclaim; return TRUE; }
void kthread_destroy(KTHREAD *t)
{
    CHECK(t == (KTHREAD *)&child && identities);
    CHECK(execution == &tasks[1].execution);
    reclaimer(execution); execution = NULL; reclaimer = NULL; identities--;
}
BOOL kthread_reap(KTHREAD *t) { CHECK(stopped); kthread_destroy(t); return TRUE; }
void panic(const char *format, ...) { (void)format; abort(); }
void proc_dispose_process(KPROC *p)
{
    CHECK(p == (KPROC *)&child && scheduler_model_flags == 0x200);
    if (inject_retirement) {
        KERNEL_TASK *previous=execution_cpu.current;
        void *stale=&tasks[1];
        CHILD_WAIT wait;
        inject_retirement=FALSE;
        CHECK(tasks[1].state==TASK_RETIRING && !tasks[1].parent && !tasks[1].process &&
              !tasks[1].thread && references==1 && identities==1 && stacks==1);
        execution_cpu.current=NULL;
        CHECK(!reap_finished_on_root(&tasks[1]));
        execution_cpu.current=previous;
        wait.parent=&owner; wait.pid=42;
        CHECK(child_ready(&wait) && ksched_process_commit(&stale)==EACCDN && stale==&tasks[1]);
        CHECK(references==1 && identities==1 && stacks==1);
    }
    disposals++;
}

int main(void)
{
    run = &owner;
    scheduler.active = TRUE;
    scheduler.tasks = tasks; initialize_task_table();
    execution_cpu.current = &tasks[0];
    execution_cpu.root.pd = &owner;
    tasks[0].state = TASK_RUNNING;
    CHECK(ksched_defer(&child,23) == ENSMEM);
    CHECK(!preparations && !commits && !references && !stacks && !identities);
    owner_ok = TRUE;
    CHECK(ksched_defer(&child,23) == ENSMEM);
    CHECK(preparations == 1 && !commits && !references && !owner_references && !stacks && !identities);
    prepare_ok = TRUE;
    CHECK(ksched_defer(&child,23) == ENSMEM);
    CHECK(preparations == 2 && !commits && !references && !stacks && !identities);
    identity_ok = TRUE;
    CHECK(ksched_defer(&child,23) == E_OK);
    CHECK(tasks[1].state == TASK_DEFERRED && tasks[1].thread &&
          references == 1 && owner_references == 1 && stacks == 1 && identities == 1 && commits == 1);
    CHECK(ksched_defer_cancel() == E_OK);
    CHECK(!references && !owner_references && !stacks && !identities && disposals == 1);
    CHECK(ksched_defer(&child,23) == E_OK);
    ksched_exited_identity((KPROC *)&child, &owner);
    CHECK(tasks[1].state == TASK_DEFERRED && owner_references == 1 && identities == 1);
    ksched_exited_identity((KPROC *)&owner, &owner);
    CHECK(tasks[1].state == TASK_NEW && tasks[1].thread == (KTHREAD *)&child &&
          references == 1 && !owner_references && stacks == 1 && identities == 1);
    stopped = TRUE; /* Backend handoff has abandoned the user continuation. */
    tasks[1].state = TASK_ZOMBIE;
    CHECK(!reap_finished_on_root(&tasks[1]) && stacks == 1 && references == 1);
    execution_cpu.current = NULL;
    CHECK(reap_finished_on_root(&tasks[1]) && !stacks && references == 1);
    {
        unsigned reaped = stack_releases;
        CHECK(reap_finished_on_root(&tasks[1]) && stack_releases == reaped);
    }
    tasks[1].state = TASK_DONE;
    CHECK(reap_finished_on_root(&tasks[1]) && !references && !stacks &&
          tasks[1].state == TASK_UNUSED && !tasks[1].pid);
    CHECK(!reap_finished_on_root(&tasks[1]));
    /* The generic transport can hold a fully prepared child across a syscall
     * return. It is neither runnable nor visible to public child waits. */
    {
        void *slot = NULL;
        CHILD_WAIT wait;
        unsigned disposed = disposals;
        execution_cpu.current = &tasks[0];
        tasks[1].program = NULL; /* Modeled finalizer revokes the retired PD. */
        CHECK(ksched_process_prepare(&child,FALSE,NULL) == EINVAL);
        slot = (void *)1;
        CHECK(ksched_process_prepare(&child,FALSE,&slot) == EINVAL && slot == (void *)1);
        slot = NULL; prepare_ok = FALSE;
        CHECK(ksched_process_prepare(&child,FALSE,&slot) == ENSMEM && !slot &&
              !references && !stacks && !identities);
        prepare_ok = TRUE;
        CHECK(ksched_process_prepare(&child,FALSE,&slot) == E_OK && slot == &tasks[1]);
        CHECK(tasks[1].state == TASK_RESERVED && references == 1 && stacks == 1 && identities == 1);
        wait.parent = &owner; wait.pid = 42;
        CHECK(child_ready(&wait));
        inject_retirement=TRUE;
        ksched_process_cancel(&slot); ksched_process_cancel(&slot);
        CHECK(!inject_retirement && !slot && !references && !stacks && !identities && disposals == disposed+1 &&
              tasks[1].state == TASK_UNUSED);
        CHECK(ksched_process_prepare(&child,FALSE,&slot) == E_OK);
        CHECK(ksched_process_publish(&slot) == 42 && !slot && tasks[1].state == TASK_NEW &&
              !child_ready(&wait));
        tasks[1].state = TASK_DONE; execution_cpu.current = NULL;
        CHECK(reap_finished_on_root(&tasks[1]) && !references && !stacks && !identities);
    }
    puts("Deferred scheduler construction, rollback and identity handoff: PASS");
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

/* This fixture only reserves within an existing group. */
void *kalloc(ULONG bytes) { (void)bytes; abort(); }
void kfree(void *pointer) { (void)pointer; abort(); }

void ksched_admission_owner_exited(KPROC *owner) { (void)owner; }
BOOL ksched_admission_abandoned(const KSCHED_ADMISSION *h) { (void)h; return FALSE; }
void ksched_admission_release(KSCHED_ADMISSION *h) { h->identity=0; }
PD *ksched_controller_pd(void) { return &owner; }

BOOL ksched_admission_complete(const KSCHED_ADMISSION *h,UWORD index,LONG status)
{ (void)h; (void)index; (void)status; abort(); } /* async slots are not launch results */

void ksched_waiter_retire(KPROC *p,ULONG tid,UQUAD execution) { (void)p; (void)tid; (void)execution; }
ULONG kthread_tid(const KTHREAD *t) { (void)t; return 17; }

PD *kproc_live_pd(const KPROC *p) { return (PD *)p; }

void ksched_admission_abandon(const KSCHED_ADMISSION *h) { if(h->identity) abort(); }

LONG ksched_admission_create(KPROC *p,UWORD n,KSCHED_ADMISSION *h)
{ (void)p; (void)n; (void)h; abort(); } /* no synchronous admission in this fixture */
BOOL ksched_admission_retain(const KSCHED_ADMISSION *a,KSCHED_ADMISSION *b)
{ (void)a; (void)b; abort(); }
