/* Exercise real scheduler deferred construction, rollback and handoff. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "string.h"
#include "gemerror.h"
#include "../../bdos/arch/x86_64/native_sched.c"

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"deferred spawn:%d: %s\n",__LINE__,#c); abort(); } } while (0)
static PD owner, child;
static KERNEL_TASK tasks[KSCHED_LIMIT];
static unsigned references, stacks, identities, preparations, commits, disposals, stack_releases;
static BOOL prepare_ok, identity_ok;
PD *run;

LONG cuis_service(ULONG function, UQUAD handle) { CHECK(function == CUIS_SESSION_QUERY && handle == 23); return E_OK; }
LONG cuis_child_bind_spawn(ULONG session, ULONG token, PD *pd) { (void)session; (void)token; (void)pd; abort(); }
BOOL kproc_unlaunched(PD *pd) { return pd == &child; }
ASPACE_ROOT kproc_user_root(PD *pd) { CHECK(pd == &child); return 0; }
KPROC *kproc_retain(PD *pd) { CHECK(pd == &child); references++; return (KPROC *)pd; }
void kproc_release(KPROC *p) { CHECK(p == (KPROC *)&child && references); references--; }
BOOL kproc_prepare_async(PD *p, PD *parent, PD *controller)
{ CHECK(p == &child && parent == &owner && controller == &owner); preparations++; return prepare_ok; }
void kproc_commit_async(PD *p) { CHECK(p == &child); commits++; }
WORD kproc_pid(PD *p) { CHECK(p == &child); return 42; }
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
void kthread_destroy(KTHREAD *t) { CHECK(t == (KTHREAD *)&child && identities); identities--; }
void proc_dispose_process(KPROC *p) { CHECK(p == (KPROC *)&child); disposals++; }

int main(void)
{
    run = &owner;
    scheduler.active = TRUE;
    scheduler.initial = 1;
    scheduler.tasks = tasks;
    scheduler.current = &tasks[0];
    scheduler.root.pd = &owner;
    tasks[0].state = TASK_RUNNING;
    CHECK(ksched_defer(&child,23) == ENSMEM);
    CHECK(preparations == 1 && !commits && !references && !stacks && !identities);
    prepare_ok = TRUE;
    CHECK(ksched_defer(&child,23) == ENSMEM);
    CHECK(preparations == 2 && !commits && !references && !stacks && !identities);
    identity_ok = TRUE;
    CHECK(ksched_defer(&child,23) == E_OK);
    CHECK(tasks[1].state == TASK_DEFERRED && tasks[1].deferred_thread &&
          references == 1 && stacks == 1 && identities == 1 && commits == 1);
    CHECK(ksched_defer_cancel() == E_OK);
    CHECK(!references && !stacks && !identities && disposals == 1);
    CHECK(ksched_defer(&child,23) == E_OK);
    ksched_exited(&owner);
    CHECK(tasks[1].state == TASK_NEW && !tasks[1].deferred_thread &&
          references == 1 && stacks == 1 && !identities);
    tasks[1].state = TASK_ZOMBIE;
    CHECK(!reap_finished_on_root(&tasks[1]) && stacks == 1 && references == 1);
    scheduler.current = NULL;
    CHECK(reap_finished_on_root(&tasks[1]) && !stacks && references == 1);
    {
        unsigned reaped = stack_releases;
        CHECK(reap_finished_on_root(&tasks[1]) && stack_releases == reaped);
    }
    tasks[1].state = TASK_DONE;
    CHECK(reap_finished_on_root(&tasks[1]) && !references && !stacks &&
          tasks[1].state == TASK_UNUSED && !tasks[1].pid);
    CHECK(!reap_finished_on_root(&tasks[1]));
    puts("Deferred scheduler construction, rollback and identity handoff: PASS");
    return 0;
}
