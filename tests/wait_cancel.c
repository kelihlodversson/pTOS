/* Production scheduler queries/hook guard; CPU switching and identities modeled. */
#include "emutos.h"
#undef CONF_WITH_PTERMGRP
#define CONF_WITH_PTERMGRP 1
#include "cuis/scheduler_irq_model.h"
#include "../bdos/native_sched.c"
#include <assert.h>
#include <stdlib.h>
static int exits, predicates;
static KERNEL_TASK tasks[KSCHED_LIMIT];
PD *run;
BOOL kthread_is_executing(const KTHREAD *t) { return t != NULL; }
KPROC *kthread_process(const KTHREAD *t) { return (KPROC *)t; }
BOOL kproc_group_status(const KPROC *p, UWORD *status)
{
    assert(p); if(status) *status=61; return TRUE;
}
void x86_64_process_save(X86_64_PROCESS_CONTEXT *p) { (void)p; abort(); }
static BOOL ready(void *p) { (void)p; predicates++; return TRUE; }
void xterm(UWORD status)
{
    assert(status==61 && ++exits==1);
    ksched_enter_exit(run);
    /* Models a termination hook reentering GEMDOS's checkpoint. */
    assert(!ksched_cancellation_pending());
    ksched_cancel_point();
    assert(exits==1);
    _Exit(0);
}
int main(void)
{
    run=(PD *)0x1000;
    scheduler.active=TRUE; execution_cpu.current=&tasks[0]; scheduler.tasks=tasks; initialize_task_table();
    tasks[0].program=run; tasks[0].thread=(KTHREAD *)0x2000; tasks[0].state=TASK_RUNNING;
    tasks[0].process=(KPROC *)0x2000;
    tasks[0].execution.cpu.pd=run; tasks[0].cancel_boundary=TRUE;
    assert(cancel_wait_ready(&tasks[0]));
    tasks[0].cancel_delivering=TRUE; assert(!cancel_wait_ready(&tasks[0]));
    tasks[0].cancel_delivering=FALSE;
    tasks[0].execution.cpu.pd=(PD *)0x3000; assert(!cancel_wait_ready(&tasks[0]));
    tasks[0].execution.cpu.pd=run;
    assert(ksched_cancellation_pending());
    assert(ksched_wait_cancelable(ready,NULL)==KSCHED_WAIT_CANCELLED && !predicates);
    run=(PD *)0x3000; /* distinct nested PROCESS on the same carrier */
    assert(!ksched_cancellation_pending());
    ksched_cancel_point();
    assert(!exits);
    run=tasks[0].program;
    ksched_cancel_point();
    abort();
}

void panic(const char *message, ...) { (void)message; abort(); }

/* The architecture regression executes this MMU/allocator probe; this hosted
 * scheduler fixture models CPU and memory ownership instead. */
void kproc_region_selftest(void) {}
