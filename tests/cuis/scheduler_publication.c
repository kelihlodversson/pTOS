/* Production slot publication and wait/orphan policy, with modeled contexts.
 * No privileged context switch or user-clone execution is claimed here. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "string.h"
#include "gemerror.h"
#include "../../bdos/arch/x86_64/native_sched.c"
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"publication:%d: %s\n",__LINE__,#c); abort(); } } while (0)
static KERNEL_TASK table[KSCHED_LIMIT];
static PD parent, child, peer;
static UWORD allocations, frees, notifications;
PD *run;
void *kalloc(ULONG bytes)
{ CHECK(bytes == sizeof(table)); allocations++; return table; }
void kfree(void *p) { CHECK(p == table); frees++; }
void panic(const char *fmt, ...) { (void)fmt; abort(); }
void kproc_usage(PD *p, ULONG usage[8]) { (void)p; memset(usage,0,8*sizeof(*usage)); }
PD *kproc_notification_parent(PD *p) { (void)p; return NULL; }
KPROC *kproc_retain(PD *p) { return (KPROC *)p; }
void kproc_release(KPROC *p) { (void)p; }
WORD kproc_pid(PD *p) { (void)p; return 42; }
void cuis_child_completed(PD *p, WORD pid, UWORD status)
{ (void)p; (void)pid; (void)status; notifications++; }
static BOOL refused_stopped;
static unsigned refused_disposals;
LONG proc_start_scheduled(PD *p, BOOL take) { (void)take; CHECK(p==&child); return EIMBA; }
BOOL kthread_stop(KTHREAD *t) { CHECK(t==(KTHREAD *)1 && !refused_stopped); refused_stopped=TRUE; return TRUE; }
BOOL kthread_is_stopped(const KTHREAD *t) { CHECK(t==(KTHREAD *)1); return refused_stopped; }
void proc_discard_accessory(PD *p) { CHECK(p==&child && refused_stopped); refused_disposals++; }
void x86_64_process_save(X86_64_PROCESS_CONTEXT *c) { (void)c; }
int main(void)
{
    void *root = NULL, *slots[KSCHED_LIMIT] = { NULL };
    UWORD i;
    WORD parent_pid = 123;
    UQUAD identity;
    CHILD_WAIT wait;
    CHECK(ksched_task_reserve(TRUE,&root) == E_OK && root == table && table[0].state == TASK_RESERVED);
    identity = next_identity;
    CHECK(ksched_task_reserve(TRUE,&root) == EINVAL && root == table && next_identity == identity);
    CHECK(ksched_task_reserve(TRUE,&slots[0]) == EAGAIN && !slots[0] && allocations == 1);
    ksched_task_cancel(&root); ksched_task_cancel(&root);
    CHECK(!scheduler.tasks && frees == 1);
    scheduler.tasks = table; memset(table,0,sizeof(table)); scheduler.active = TRUE;
    scheduler.initial = 1; scheduler.current = table; table[0].state = TASK_RUNNING; run = &parent;
    for (i = 0; i < KSCHED_LIMIT-1; i++) {
        CHECK(ksched_task_reserve(FALSE,&slots[i]) == E_OK && slots[i] == &table[i+1]);
        CHECK(table[i+1].state == TASK_RESERVED);
    }
    identity = next_identity;
    CHECK(ksched_task_reserve(FALSE,&slots[KSCHED_LIMIT-1]) == EAGAIN &&
          !slots[KSCHED_LIMIT-1] && next_identity == identity && allocations == 1);
    /* A privately bound slot is still invisible to waits/exit callbacks. */
    table[1].parent = &parent; table[1].program = &child; table[1].process = (KPROC *)&child; table[1].pid = 42;
    wait.parent = &parent; wait.pid = 42;
    CHECK(child_ready(&wait)); /* no published child */
    CHECK(!ksched_parent_pid(&child,&parent_pid) && parent_pid == 123);
    ksched_exiting(&child,7);
    CHECK(!table[1].exited && table[1].program == &child && !notifications);
    for (i = 0; i < KSCHED_LIMIT-1; i++) ksched_task_cancel(&slots[i]);
    CHECK(frees == 1);
    CHECK(ksched_task_reserve(FALSE,&slots[0]) == E_OK && slots[0] == &table[1]);
    table[1].program = &child; table[1].process = (KPROC *)&child; table[1].parent = &parent; table[1].pid = 42;
    ksched_task_publish(&slots[0]); CHECK(!slots[0] && table[1].state == TASK_NEW);
    CHECK(!child_ready(&wait));
    table[0].state = TASK_BLOCKED;
    CHECK(ksched_task_reserve(FALSE,&slots[0]) == EINVFN && !slots[0]);
    /* Early child completion wakes the blocked parent using the existing
     * wait predicate. A separate child keeps its independent pending state. */
    table[2].state = TASK_NEW; table[2].parent = &parent; table[2].program = &peer; table[2].process = (KPROC *)&peer; table[2].pid = 43;
    table[1].asynchronous = TRUE;
    ksched_exiting(&child,7); table[1].state = TASK_ZOMBIE;
    CHECK(child_ready(&wait) && table[1].exited && !table[2].exited && !notifications);
    wait.pid = 43; CHECK(!child_ready(&wait));
    ksched_exiting(&parent,8);
    CHECK(!table[1].parent && table[1].state == TASK_DONE && !table[2].parent && table[2].program == &peer);
    /* A replacement at the same PD address cannot inherit the old children. */
    CHECK(child_ready(&wait) && !notifications && allocations == 1 && frees == 1);
    /* A refused legacy start must be DONE/root-reapable, not DRAINING
     * forever as though a runnable sibling still retained its PROCESS. */
    memset(table,0,sizeof(table));scheduler.current=&table[0];
    table[0].state=TASK_RUNNING;table[0].program=&child;
    table[0].process=(KPROC *)&child;table[0].thread=(KTHREAD *)1;
    if(!setjmp(scheduler.resume)) task_entry();
    CHECK(table[0].state==TASK_DONE && refused_stopped && refused_disposals==1 &&
          !table[0].program && !table[0].process_completed && table[0].result==EIMBA);
    puts("Shared scheduler publication: exhaustion, private reservations, cancellation, blocked/early wait and orphan identity: PASS");
    return 0;
}
