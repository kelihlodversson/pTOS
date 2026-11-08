/* Production slot publication and wait/orphan policy, with modeled contexts.
 * No privileged context switch or user-clone execution is claimed here. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "string.h"
#include "gemerror.h"
#ifndef __SANITIZE_ADDRESS__
/* Test the kernel continuation ABI, not glibc's fortified caller-stack ABI.
 * Other fixtures (and ASan ownership tests below) retain normal libc jumps. */
#include <setjmp.h>
#undef setjmp
#undef longjmp
int cuis_root_setjmp(jmp_buf state) __attribute__((returns_twice));
void cuis_root_longjmp(jmp_buf state, int value) NORETURN;
typedef char kernel_context_fits_host_buffer[sizeof(jmp_buf) >= 13*sizeof(long) ? 1 : -1];
#define setjmp cuis_root_setjmp
#define longjmp cuis_root_longjmp
#endif
#include "scheduler_irq_model.h"
#include "../../bdos/native_sched.c"
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"publication:%d: %s\n",__LINE__,#c); abort(); } } while (0)
static BOOL allocate_task_table(UWORD count)
{ KERNEL_TASK *slots[KSCHED_LIMIT]; return prepare_batch(count,slots); }
static KERNEL_TASK table[KSCHED_LIMIT];
static PD parent, child, peer, kernel_controller;
static UWORD allocations, frees, notifications;
static BOOL fail_growth;
PD *run;
jmp_buf errbuf;
static BOOL root_fault;
static unsigned root_release_calls, process_releases;
static BOOL allow_reap, reenter_cancel;
static void *replacement_reservation;
static LONG fault_output;
void *kalloc(ULONG bytes)
{
    void *p;
    CHECK(!runqueue_held);
    if (bytes == sizeof(table)) { allocations++; return table; }
    if (bytes == sizeof(KERNEL_TASK) && fail_growth) return NULL;
    p = malloc(bytes); if (p) allocations++; return p;
}
void kfree(void *p)
{
    CHECK(p && !runqueue_held);
    if(p==table) CHECK(scheduler.tasks!=table && !scheduler.active);
    if(p!=table) free(p);
    frees++;
}
void panic(const char *fmt, ...)
{
    if (root_fault && !strcmp(fmt,"scheduler root: cleanup callback escaped") &&
        root_release_calls==1 && fault_output==-123 && scheduler.active && !scheduler.parked) {
        puts("Persistent root escaping disk error: fail-stop, no repeated release/result delivery: PASS (host injection)");
        fflush(stdout); _Exit(0);
    }
    abort();
}
void kproc_usage(PD *p, ULONG usage[8]) { (void)p; memset(usage,0,8*sizeof(*usage)); }
PD *kproc_notification_parent(PD *p) { (void)p; return NULL; }
KPROC *kproc_retain(PD *p) { return (KPROC *)p; }
void kproc_release(KPROC *p)
{
    CHECK(!runqueue_held);
    if (p) process_releases++;
    if (reenter_cancel && p==(KPROC *)&parent) {
        reenter_cancel=FALSE;
        CHECK(!scheduler.entry_owner && !scheduler.waiter.identity);
        CHECK(ksched_task_reserve(TRUE,&replacement_reservation)==E_OK);
    }
    if (root_fault && p==(KPROC *)&child) { root_release_calls++; longjmp(errbuf,1); }
}
WORD kproc_pid(PD *p) { return p ? 42 : 0; }
KPROC *kproc_retain_live(KPROC *p) { return p; }
PD *kproc_live_pd(const KPROC *p) { return (PD *)p; }
UQUAD procmem_gen(const void *p) { return p==&kernel_controller ? 1 : 2; }
BOOL procmem_range_live(VADDR p,VADDR n) { return p==(VADDR)(uintptr_t)&kernel_controller && n==sizeof(PD); }
void procmem_pin(VADDR p,VADDR n,int delta) { CHECK(p==(VADDR)(uintptr_t)&kernel_controller && n==sizeof(PD) && delta==1); }
void cuis_child_completed(PD *p, WORD pid, UWORD status)
{ (void)p; (void)pid; (void)status; notifications++; }
BOOL cuis_child_pid_reserved(WORD pid) { (void)pid; return FALSE; }
static BOOL refused_stopped;
static unsigned refused_disposals;
LONG proc_start_scheduled(PD *p, BOOL take) { (void)take; CHECK(p==&child); return EIMBA; }
BOOL kthread_stop(KTHREAD *t) { CHECK(t==(KTHREAD *)1 && !refused_stopped); refused_stopped=TRUE; return TRUE; }
BOOL kthread_is_stopped(const KTHREAD *t) { CHECK(t==(KTHREAD *)1); return refused_stopped; }
void proc_discard_scheduled(PD *p) { CHECK(p==&child && refused_stopped); refused_disposals++; }
static uintptr_t modeled_flags = 0x200;
static unsigned root_entries;
void x86_64_process_save(X86_64_PROCESS_CONTEXT *c)
{
    memset(c,0,sizeof(*c)); c->pd=run; c->flags=modeled_flags;
    c->launch=(X86_64_PROCESS_LAUNCH *)run; /* Deliberately reusable identity. */
    modeled_flags=0;
}
void x86_64_process_restore(const X86_64_PROCESS_CONTEXT *c)
{ run=c->pd; modeled_flags=c->flags; }
void native_sched_enter(uintptr_t top, void (*entry)(void))
{
    CHECK(run==&kernel_controller && !scheduler.root.launch);
    CHECK(entry==scheduler_root_entry && top==(uintptr_t)root_stack+sizeof(root_stack)-8);
    root_entries++;
    __asm__ volatile ("mov %0,%%rsp; jmp *%1" :: "r"(top), "r"(entry) : "memory");
    __builtin_unreachable();
}
BOOL kthread_is_executing(const KTHREAD *t) { (void)t; return FALSE; }
BOOL kthread_start(KTHREAD *t) { (void)t; abort(); }
BOOL kthread_reap(KTHREAD *t) { CHECK(allow_reap && t); return TRUE; }
void proc_clone_commit_prepared(PROC_CLONE_TRANSACTION *tx, void *slot)
{ (void)tx; (void)slot; abort(); }
#ifndef __SANITIZE_ADDRESS__
/* Actual hosted stack switches; privileged context/MMU effects are modeled.
 * Hosted ASan cannot validate this kernel's private alternate-stack ABI. */
static BOOL never_ready(void *argument) { CHECK(argument==&child); return FALSE; }
static void root_lifetime_check(void)
{
    unsigned cycle;
    NATIVE_SCHED_CONTEXT empty;
    jmp_buf no_waiter;
    memset(&empty,0,sizeof(empty)); memset(no_waiter,0,sizeof(no_waiter));
    for (cycle=0;cycle<32;cycle++) {
        LONG output[2]={-1,-1};
        PD *caller=cycle&1?&parent:&peer;
        run=caller;
        CHECK(allocate_task_table(2));
        table[0].state=table[1].state=TASK_DONE;
        table[0].result=cycle+100; table[1].result=cycle+200;
        run=caller; modeled_flags=cycle&1?0x200:0;
        CHECK(dispatch_group(2,output)==E_OK && run==caller && modeled_flags==(cycle&1?0x200:0));
        CHECK(output[0]==(LONG)cycle+100 && output[1]==(LONG)cycle+200);
        CHECK(scheduler.root_started && scheduler.parked && !scheduler.waiter_attached &&
              !scheduler.active && !scheduler.current && !scheduler.tasks && root_entries==1);
        CHECK(!memcmp(&scheduler.root,&empty,sizeof(empty)) &&
              !memcmp(scheduler.waiter_resume,no_waiter,sizeof(no_waiter)));
        CHECK(!scheduler.waiter.identity);
        memset(caller,0x7d,sizeof(*caller)); /* Old identity storage is reusable. */
        output[0]=output[1]=-77;
    }
    /* Returning one admission must not drain another live blocked task.
     * A later bounded entry with no runnable work parks without spinning or
     * preserving a caller stack; a final entry reclaims the stopped peer. */
    {
        LONG output=-1;
        CHECK(allocate_task_table(1));
        table[0].state=TASK_DONE; table[0].result=311;
        table[1].state=TASK_BLOCKED; table[1].process=(KPROC *)&child;
        table[1].ready=never_ready; table[1].argument=&child;
        CHECK(dispatch_group(1,&output)==E_OK && output==311 && scheduler.active &&
              scheduler.tasks==table && table[1].state==TASK_BLOCKED && !scheduler.current &&
              !scheduler.waiter_attached && !scheduler.entry_owner && !scheduler.entry_argument);
        {
            void *reserved[KSCHED_LIMIT]={0}; UWORD n;
            UQUAD previous;
            for(n=0;n<KSCHED_LIMIT-1;n++) CHECK(ksched_task_reserve(FALSE,&reserved[n])==E_OK);
            previous=next_identity; fail_growth=TRUE;
            CHECK(ksched_task_reserve(FALSE,&reserved[KSCHED_LIMIT-1])==ENSMEM &&
                  !reserved[KSCHED_LIMIT-1] && next_identity==previous && table[1].state==TASK_BLOCKED);
            fail_growth=FALSE;
            for(n=0;n<KSCHED_LIMIT;n++) ksched_task_cancel(&reserved[n]);
            CHECK(scheduler.active && table[1].state==TASK_BLOCKED && !scheduler.waiter.identity);
        }
        output=-77; run=&peer; modeled_flags=0x200;
        CHECK(enter_root(TRUE,NULL,NULL)==E_OK && output==-77 && run==&peer && modeled_flags==0x200 &&
              table[1].state==TASK_BLOCKED && !scheduler.entry_ready && !scheduler.entry_argument && !scheduler.entry_owner);
        table[1].state=TASK_DONE;
        CHECK(enter_root(TRUE,NULL,NULL)==E_OK && !scheduler.active && !scheduler.tasks && output==-77);
    }
    puts("Detached admission: early result, independent blocked task, bounded later entry and final arena reclaim: PASS (CPU modeled)");
    CHECK(allocations==frees);
    puts("Persistent root: 32 real hosted park/resume stack handoffs, copied results and no retired identity: PASS (CPU modeled)");
    CHECK(allocate_task_table(2));
    table[0].state=TASK_DONE; table[0].process=(KPROC *)&child;
    fault_output=-123; root_fault=TRUE;
    dispatch_group(1,&fault_output);
    abort(); /* A fail-stop root must not return to a partially cleaned caller. */
}
#endif
int main(void)
{
    void *root = NULL, *slots[KSCHED_LIMIT] = { NULL };
    UWORD i;
    UQUAD identity;
    KERNEL_TASK *t, *grown;
    CHILD_WAIT wait;
    CHECK(ksched_controller_install(&kernel_controller));
    CHECK(ksched_task_reserve(TRUE,&root) == E_OK && root == table && table[0].state == TASK_RESERVED);
    identity = next_identity;
    CHECK(ksched_task_reserve(TRUE,&root) == EINVAL && root == table && next_identity == identity);
    CHECK(ksched_task_reserve(TRUE,&slots[0]) == EAGAIN && !slots[0] && allocations == 2);
    ksched_task_cancel(&root); ksched_task_cancel(&root);
    CHECK(!scheduler.tasks && frees == 2);
    scheduler.waiter_attached=TRUE; /* root parked, but old result not consumed */
    identity=next_identity;
    CHECK(ksched_task_reserve(TRUE,&root)==EAGAIN && !root && allocations==2 && next_identity==identity);
    scheduler.waiter_attached=FALSE;
    CHECK(allocate_task_table(1)); scheduler.active = TRUE;
    scheduler.current = table; table[0].state = TASK_RUNNING; run = &parent;
    for (i = 0; i < KSCHED_LIMIT-1; i++) {
        CHECK(ksched_task_reserve(FALSE,&slots[i]) == E_OK && slots[i] == &table[i+1]);
        CHECK(table[i+1].state == TASK_RESERVED);
    }
    identity = next_identity;
    fail_growth = TRUE;
    CHECK(ksched_task_reserve(FALSE,&slots[KSCHED_LIMIT-1]) == ENSMEM &&
          !slots[KSCHED_LIMIT-1] && next_identity == identity && allocations == 4);
    CHECK(!table[KSCHED_LIMIT-1].next_task); /* failed growth publishes nothing */
    fail_growth = FALSE;
    CHECK(ksched_task_reserve(FALSE,&slots[KSCHED_LIMIT-1]) == E_OK &&
          slots[KSCHED_LIMIT-1] == table[KSCHED_LIMIT-1].next_task && allocations == 5);
    CHECK(table[1].state == TASK_RESERVED && table[0].state == TASK_RUNNING);
    CHECK(ksched_task_reserve(FALSE,&slots[KSCHED_LIMIT-1]) == EINVAL);
    grown=slots[KSCHED_LIMIT-1];
    ksched_task_cancel(&slots[KSCHED_LIMIT-1]);
    CHECK(table[KSCHED_LIMIT-1].next_task==grown && grown->state==TASK_UNUSED && frees==2);
    CHECK(ksched_task_reserve(FALSE,&slots[KSCHED_LIMIT-1])==E_OK &&
          slots[KSCHED_LIMIT-1]==grown && allocations==5); /* stable high-water reuse */
    {
        KSCHED_ADMISSION independent={0};
        LONG result=-95;
        CHECK(ksched_admission_create(NULL,1,&independent)==E_OK &&
              ksched_admission_retain(&independent,&grown->completion));
        grown->result_slot=0; grown->result=95; grown->state=TASK_DONE;
        grown->ready=child_ready; grown->argument=&wait;
        scheduler.current=NULL;
        CHECK(reap_finished_on_root(grown) && !grown->ready && !grown->argument &&
              grown->state==TASK_UNUSED && !grown->completion.identity &&
              ksched_admission_take(&independent,&result,1)==E_OK && result==95);
        ksched_admission_release(&independent);
        scheduler.current=table;
        clear_task(grown); grown->state=TASK_RESERVED;
    }
    /* A privately bound slot is still invisible to waits/exit callbacks. */
    table[1].parent = &parent; table[1].program = &child; table[1].process = (KPROC *)&child; table[1].pid = 42;
    wait.parent = &parent; wait.pid = 42;
    CHECK(child_ready(&wait)); /* no published child */
    ksched_exiting(&child,7);
    CHECK(!table[1].exited && table[1].program == &child && !notifications);
    for (i = 0; i < KSCHED_LIMIT; i++) ksched_task_cancel(&slots[i]);
    CHECK(frees == 3);
    CHECK(ksched_task_reserve(FALSE,&slots[0]) == E_OK && slots[0] == &table[1]);
    table[1].program = &child; table[1].process = (KPROC *)&child; table[1].parent = &parent; table[1].pid = 42;
    table[1].asynchronous=TRUE;
    ksched_task_publish(&slots[0]); CHECK(!slots[0] && table[1].state == TASK_NEW);
    CHECK(!child_ready(&wait));
    table[0].state = TASK_BLOCKED;
    CHECK(ksched_task_reserve(FALSE,&slots[0]) == EINVFN && !slots[0]);
    /* Early child completion wakes the blocked parent using the existing
     * wait predicate. A separate child keeps its independent pending state. */
    grown=table[KSCHED_LIMIT-1].next_task;
    CHECK(grown && grown->state==TASK_UNUSED);
    grown->state=TASK_NEW; grown->asynchronous=TRUE; grown->parent=&parent; grown->program=&peer;
    grown->process=(KPROC *)&peer; grown->pid=43;
    CHECK(ksched_pid_reserved(43));
    grown->state=TASK_BLOCKED;
    grown->execution.identity=++next_identity;
    ksched_context_restore(grown->execution.identity);
    CHECK(scheduler.current==grown && ksched_context_id()==grown->execution.identity);
    scheduler.current=table;
    table[1].asynchronous = TRUE;
    ksched_exiting(&child,7); table[1].state = TASK_ZOMBIE;
    CHECK(child_ready(&wait) && table[1].exited && !grown->exited && !notifications);
    wait.pid = 43; CHECK(!child_ready(&wait));
    ksched_exiting(&parent,8);
    CHECK(!table[1].parent && table[1].state == TASK_DONE && !grown->parent && grown->program == &peer);
    /* A replacement at the same PD address cannot inherit the old children. */
    CHECK(child_ready(&wait) && !notifications && allocations == 6 && frees == 3);
    /* A worker owns its anchor through root reap, in either list order.
     * Stopped leader execution is not PROCESS/result/record retirement. */
    ksched_admission_release(&scheduler.waiter);
    for(i=0;i<2;i++) {
        KERNEL_TASK *anchor=&table[i], *worker=&table[1-i];
        KSCHED_ADMISSION result={0}; LONG status=-99;
        unsigned before=process_releases;
        for(t=table;t;t=t->next_task) clear_task(t);
        CHECK(ksched_admission_create(NULL,1,&result)==E_OK &&
              ksched_admission_retain(&result,&anchor->completion));
        scheduler.current=NULL;
        anchor->state=TASK_DRAINING; anchor->process=(KPROC *)&child;
        anchor->dependents=1; anchor->result=91;
        worker->state=TASK_DONE; worker->process=(KPROC *)&child;
        worker->thread=(KTHREAD *)1; worker->completion_owner=anchor;
        free_task_table(); /* cannot detach a live/dependent arena */
        CHECK(scheduler.tasks==table && scheduler.active);
        CHECK(reap_finished_on_root(anchor) && anchor->dependents==1 &&
              anchor->process && anchor->state==TASK_DRAINING &&
              ksched_admission_take(&result,&status,1)==EAGAIN && status==-99);
        anchor->state=TASK_DONE; /* final PROCESS completion */
        CHECK(reap_finished_on_root(anchor) && anchor->process &&
              process_releases==before && anchor->completion.identity);
        allow_reap=TRUE;
        CHECK(reap_finished_on_root(worker) && !anchor->dependents &&
              worker->state==TASK_UNUSED && !worker->thread && !worker->completion_owner);
        allow_reap=FALSE;
        CHECK(reap_finished_on_root(anchor) && anchor->state==TASK_UNUSED &&
              process_releases==before+2 && ksched_admission_take(&result,&status,1)==E_OK && status==91);
        ksched_admission_release(&result);
    }
    puts("Counted PROCESS anchors: leader-first, both record orders, delayed result and exactly-once release: PASS (host)");
    /* A refused legacy start must be DONE/root-reapable, not DRAINING
     * forever as though a runnable sibling still retained its PROCESS. */
    for (t=table;t;t=t->next_task) clear_task(t);
    scheduler.current=&table[0];
    table[0].state=TASK_RUNNING;table[0].program=&child;
    table[0].process=(KPROC *)&child;table[0].thread=(KTHREAD *)1;
    if(!setjmp(scheduler.resume)) task_entry();
    CHECK(table[0].state==TASK_DONE && refused_stopped && refused_disposals==1 &&
          !table[0].program && !table[0].process_completed && table[0].result==EIMBA);
    /* A retired attachment must lose its jump target, not just its result
     * permission. The producer can still finish after the consumer is gone. */
    ksched_admission_release(&scheduler.waiter);
    ksched_admission_release(&table[0].completion);
    CHECK(ksched_admission_create((KPROC *)&parent,1,&scheduler.waiter)==E_OK &&
          ksched_admission_retain(&scheduler.waiter,&table[0].completion));
    scheduler.entry_owner=kproc_retain(&parent);
    scheduler.waiter_attached=TRUE;
    memset(scheduler.waiter_resume,0xa5,sizeof(scheduler.waiter_resume));
    ksched_exiting(&parent,9);
    CHECK(!scheduler.waiter_attached && !scheduler.waiter.identity &&
          ksched_admission_abandoned(&table[0].completion));
    for (i=0;i<sizeof(scheduler.waiter_resume);i++)
        CHECK(!((UBYTE *)scheduler.waiter_resume)[i]);
    {
        LONG untouched=-91;
        CHECK(ksched_admission_complete(&table[0].completion,0,9) &&
              ksched_admission_take(&table[0].completion,&untouched,1)==EACCDN && untouched==-91);
        ksched_exiting(&parent,10); /* duplicate notification cannot restore it */
        CHECK(!scheduler.waiter_attached && !scheduler.waiter.identity);
    }
    puts("Attached waiter retirement: cleared continuation, late producer and unchanged output: PASS (host)");
    {
        void *pending_slot=NULL, *unchanged=NULL;
        unsigned before;
        scheduler.current=&table[0]; table[0].state=TASK_RUNNING; run=&parent;
        CHECK(ksched_task_reserve(FALSE,&pending_slot)==E_OK);
        before=allocations;
        ksched_shutdown_begin(); ksched_shutdown_begin();
        CHECK(!ksched_async_allowed() && ksched_yield()==EACCDN &&
              ksched_task_reserve(TRUE,&unchanged)==EACCDN &&
              ksched_task_reserve(FALSE,&unchanged)==EACCDN && !unchanged && allocations==before);
        ksched_task_cancel(&pending_slot); ksched_task_cancel(&pending_slot);
        CHECK(!pending_slot && !runqueue_held);
        scheduler.stopping=FALSE; /* only this isolated fixture can cold-start again */
    }
    puts("Shutdown closes admission/entry before cleanup; private reservations remain abortable: PASS (host)");
    /* Drop the fixture's synthetic, non-owning task fields before testing
     * whole-arena reclamation (production requires all resource owners gone). */
    for(t=table;t;t=t->next_task) clear_task(t);
    scheduler.current = NULL; free_task_table();
    CHECK(allocations == frees && !scheduler.tasks);
    {
        void *old_reservation=NULL;
        UQUAD replacement_identity;
        run=&parent;
        CHECK(ksched_task_reserve(TRUE,&old_reservation)==E_OK);
        reenter_cancel=TRUE;
        ksched_task_cancel(&old_reservation);
        CHECK(!old_reservation && replacement_reservation && !reenter_cancel &&
              scheduler.entry_owner==(KPROC *)&parent && scheduler.waiter.identity);
        replacement_identity=scheduler.waiter.identity;
        CHECK(((KERNEL_TASK *)replacement_reservation)->completion.identity==replacement_identity);
        ksched_task_cancel(&old_reservation); /* old consumed handle cannot touch replacement */
        CHECK(scheduler.waiter.identity==replacement_identity && scheduler.entry_owner==(KPROC *)&parent);
        ksched_task_cancel(&replacement_reservation);
        CHECK(!scheduler.tasks && !scheduler.waiter.identity && !scheduler.entry_owner && allocations==frees);
    }
    puts("Reentrant cancelled admission: detach-before-release preserves replacement owner/result identity: PASS (host)");
    puts("Shared scheduler growth/publication: exhaustion, private reservations, cancellation, blocked/early wait and orphan identity: PASS");
#ifndef __SANITIZE_ADDRESS__
    root_lifetime_check();
#else
    puts("Persistent root alternate-stack switch: native targets/unsanitized host only; hosted ASan ownership tests continue");
#endif
    return 0;
}

ULONG kthread_tid(const KTHREAD *t) { (void)t; return 17; }
