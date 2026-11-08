/* Real result-wait/cancellation code; IRQ/CPU/PROCESS endpoints are host models. */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>
#include "emutos.h"
#undef CONF_WITH_PTERMGRP
#define CONF_WITH_PTERMGRP 1
#include "process.h"
#define NATIVE_SCHED_BACKEND_H
#define NATIVE_SCHED_CONTEXT X86_64_PROCESS_CONTEXT
static uintptr_t irq_flags=0x200;
static uintptr_t native_sched_irq_save(void) { uintptr_t old=irq_flags; irq_flags=0; return old; }
static void native_sched_irq_restore(uintptr_t f) { irq_flags=f; }
static void native_sched_irq_disable(void) { irq_flags=0; }
static uintptr_t native_sched_masked(uintptr_t f) { return f & ~0x200UL; }
static uintptr_t native_sched_root_stack_top(uintptr_t b,ULONG n) { return b+n-8; }
static void native_sched_root_context(NATIVE_SCHED_CONTEXT *c,PD *p,uintptr_t t) { (void)c; (void)p; (void)t; }
static void native_sched_save(NATIVE_SCHED_CONTEXT *c) { (void)c; abort(); }
static void native_sched_restore(const NATIVE_SCHED_CONTEXT *c) { (void)c; abort(); }
static void native_sched_idle(void) { abort(); }
static void native_sched_enter(uintptr_t top,void (*entry)(void)) NORETURN;
static void native_sched_enter(uintptr_t top,void (*entry)(void)) { (void)top; (void)entry; abort(); }
struct kproc { PD *pd; ULONG refs; };
struct kthread { struct kproc *process; ULONG tid; };
#include "../bdos/native_sched.c"
#undef assert
#define assert(c) do { if (!(c)) { fprintf(stderr,"result wait:%d: %s\n",__LINE__,#c); abort(); } } while (0)
static PD process_pd, kernel_pd, nested_pd;
static KPROC process={&process_pd,1};
static KTHREAD thread={&process,17};
static KERNEL_TASK task;
static BOOL pending, lose_context;
static unsigned allocations, frees, exits;
static jmp_buf terminated;
PD *run;
KPROC *kthread_process(const KTHREAD *t) { return t->process; }
ULONG kthread_tid(const KTHREAD *t) { return t->tid; }
BOOL kthread_is_executing(const KTHREAD *t) { return t==&thread; }
BOOL kproc_group_status(const KPROC *p,UWORD *status) { assert(p==&process); if(status)*status=61; return pending; }
KPROC *kproc_retain_live(KPROC *p) { assert(p==&process); p->refs++; return p; }
void kproc_release(KPROC *p) { assert(p==&process && p->refs>1); p->refs--; }
PD *kproc_live_pd(const KPROC *p)
{
    assert(p==&process);
    /* Fault-model an invalidated managed context before block admission. */
    if(lose_context) { lose_context=FALSE; scheduler.current=NULL; }
    return p->pd;
}
void *kalloc(ULONG bytes) { void *p=malloc(bytes); if(p)allocations++; return p; }
void kfree(void *p) { assert(p); frees++; free(p); }
void panic(const char *s,...) { (void)s; abort(); }
UQUAD procmem_gen(const void *p) { return p==&kernel_pd?1:0; }
BOOL procmem_range_live(VADDR p,VADDR n) { return p==(VADDR)(uintptr_t)&kernel_pd && n==sizeof(PD); }
void procmem_pin(VADDR p,VADDR n,int delta) { assert(procmem_range_live(p,n) && delta==1); }
void xterm(UWORD status)
{
    /* The result attachment must be gone BEFORE a nonreturning checkpoint.
     * Only the producer admission remains, retaining one PROCESS identity. */
    assert(status==61 && irq_flags==0x200 && process.refs==2 && allocations==frees+1);
    exits++;
    longjmp(terminated,1);
}
int main(void)
{
    KSCHED_ADMISSION admission={0};
    LONG output=-7;
    unsigned mode;
    assert(ksched_controller_install(&kernel_pd));
    scheduler.active=TRUE; scheduler.current=&task;
    task.program=&process_pd; task.process=&process; task.thread=&thread;
    task.state=TASK_RUNNING; task.execution.identity=100;
    run=&process_pd;
    assert(ksched_admission_create(&process,1,&admission)==E_OK);
    assert(ksched_result_wait(&admission,NULL,1)==EINVAL && irq_flags==0x200);
    assert(ksched_result_wait(&admission,&output,2)==EINVAL && output==-7 && process.refs==2 && irq_flags==0x200);
    run=&nested_pd; pending=TRUE;
    assert(ksched_result_wait(&admission,&output,1)==EINVFN && !exits && output==-7 && irq_flags==0x200);
    run=&process_pd; pending=FALSE;
    lose_context=TRUE;
    assert(ksched_result_wait(&admission,&output,1)==EINVFN && output==-7 && irq_flags==0x200 && process.refs==2);
    assert(!scheduler.current && allocations==frees+1);
    scheduler.current=&task;
    assert(ksched_admission_complete(&admission,0,47));
    assert(ksched_result_wait(&admission,&output,1)==E_OK && output==47 && irq_flags==0x200);
    output=-7;
    assert(ksched_result_wait(&admission,&output,1)==EACCDN && output==-7 && irq_flags==0x200);
    ksched_admission_release(&admission);
    for(mode=0;mode<2;mode++) {
        assert(ksched_admission_create(&process,1,&admission)==E_OK);
        if(mode) assert(ksched_admission_complete(&admission,0,48));
        pending=TRUE;
        if(!setjmp(terminated)) { ksched_result_wait(&admission,&output,1); abort(); }
        assert(exits==mode+1 && output==-7 && irq_flags==0x200);
        if(mode) { assert(ksched_admission_take(&admission,&output,1)==E_OK && output==48); output=-7; }
        ksched_admission_release(&admission);
        assert(process.refs==1 && allocations==frees);
    }
    puts("Managed result wait: cancel before/after readiness, unchanged outputs, cleanup before checkpoint, IRQ restore and nested isolation: PASS (host CPU model)");
    return 0;
}
