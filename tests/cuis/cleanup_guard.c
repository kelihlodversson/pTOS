/* Production execution guard; only IRQ mechanics and identities are modeled. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "string.h"
#include "scheduler_irq_model.h"
#include "../../bdos/native_sched.c"
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"cleanup guard:%d: %s\n",__LINE__,#c); abort(); } } while (0)
static PD parent, nested;
static KERNEL_TASK tasks[2];
static ULONG tids[2]={17,18};
static BOOL pending, expect_panic;
static unsigned deliveries;
static jmp_buf fatal, delivered;
static KTHREAD *identity(unsigned i) { return (KTHREAD *)&tasks[i]; }
BOOL kthread_is_executing(const KTHREAD *t) { return t==identity(0) || t==identity(1); }
KPROC *kthread_process(const KTHREAD *t) { CHECK(kthread_is_executing(t)); return (KPROC *)&parent; }
ULONG kthread_tid(const KTHREAD *t) { CHECK(kthread_is_executing(t)); return tids[t==identity(0) ? 0:1]; }
BOOL kproc_group_status(const KPROC *p,UWORD *status)
{ CHECK(p==(KPROC *)&parent); if(status)*status=0x2468; return pending; }
BOOL kproc_request_group_exit(KPROC *p,UWORD status)
{ (void)p; (void)status; abort(); } /* Protected Ptermgrp must reject before admission. */
void xterm(UWORD status) { CHECK(status==0x2468); deliveries++; longjmp(delivered,1); }
void panic(const char *format,...)
{ (void)format; if(expect_panic)longjmp(fatal,1); abort(); }
#define BAD(operation) do { expect_panic=TRUE; if(!setjmp(fatal)) { operation; abort(); } expect_panic=FALSE; scheduler_model_flags=0x200; } while(0)
int main(void)
{
    KSCHED_CLEANUP_SCOPE outer={0}, inner={0}, child={0}, empty={0}, stale;
    run=&parent; scheduler.active=TRUE; scheduler.tasks=tasks; execution_cpu.current=&tasks[0];
    tasks[0].program=tasks[1].program=&parent;
    tasks[0].process=tasks[1].process=(KPROC *)&parent;
    tasks[0].thread=identity(0); tasks[1].thread=identity(1);
    tasks[0].state=tasks[1].state=TASK_RUNNING;
    tasks[0].execution.identity=11; tasks[1].execution.identity=12;
    pending=TRUE; /* Guard entry does not clear or consume an existing request. */
    ksched_cleanup_begin(&outer); ksched_cleanup_begin(&inner);
    CHECK(ksched_cleanup_active() && ksched_cancellation_pending() && tasks[0].cleanup_depth==2);
    ksched_cancel_point(); CHECK(!deliveries && ksched_termgrp(7)==EACCDN);
    BAD(ksched_cleanup_end(&outer)); CHECK(tasks[0].cleanup_depth==2);
    BAD(ksched_cleanup_begin(&outer));
    run=&nested; CHECK(!ksched_cleanup_active() && !ksched_cancellation_pending());
    ksched_cleanup_begin(&child); CHECK(!child.thread && child.active);
    ksched_cleanup_end(&child); run=&parent;
    CHECK(ksched_cleanup_active() && tasks[0].cleanup_depth==2);
    execution_cpu.current=&tasks[1]; CHECK(!ksched_cleanup_active() && ksched_cancellation_pending());
    BAD(ksched_cleanup_end(&inner)); CHECK(!tasks[1].cleanup_depth);
    execution_cpu.current=&tasks[0];
    tasks[0].execution.identity++; BAD(ksched_cleanup_end(&inner)); tasks[0].execution.identity--;
    tids[0]++; BAD(ksched_cleanup_end(&inner)); tids[0]--;
    tasks[0].process=(KPROC *)&nested; BAD(ksched_cleanup_end(&inner)); tasks[0].process=(KPROC *)&parent;
    ksched_cleanup_end(&inner); CHECK(ksched_cleanup_active());
    stale=outer;
    ksched_cleanup_end(&outer); CHECK(!ksched_cleanup_active() && !deliveries && pending);
    ksched_cleanup_begin(&inner); BAD(ksched_cleanup_end(&stale)); ksched_cleanup_end(&inner);
    BAD(ksched_cleanup_end(&outer)); BAD(ksched_cleanup_end(&empty));
    if (!setjmp(delivered)) { ksched_cancel_point(); abort(); }
    CHECK(deliveries==1 && pending);
    execution_cpu.current=NULL; ksched_cleanup_begin(&child); ksched_cleanup_end(&child);
    CHECK(!tasks[0].cleanup_depth && !tasks[1].cleanup_depth && scheduler_model_flags==0x200);
    puts("Production cleanup guard: pending latch, nested PROCESS, sibling THREAD, identity/LIFO rejection and explicit checkpoint: PASS");
    return 0;
}
