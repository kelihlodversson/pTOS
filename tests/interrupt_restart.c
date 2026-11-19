/* Production retained-return transaction and KASPACE leases. CPU dispatch,
 * PROCESS/THREAD identity and MMU endpoints are modeled; no native IRQ proof. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "cuis/scheduler_irq_model.h"
#include "../bdos/interrupt_restart.c"

#define CODE 0x400000UL
#define STACK 0x10000000UL
struct aspace { int alive; };
struct kproc { KASPACE *space; UQUAD number; PD *pd; };
struct kthread { KPROC *process; BOOL executing; };
static PD pd;
PD *run=&pd;
static KPROC process;
static KTHREAD thread;
static KTHREAD_CONTEXT execution;
static BOOL target=TRUE,cleanup,cancelled,peer=TRUE,critical;
static BOOL resident_code=TRUE,resident_stack=TRUE;
static VADDR stack_first=STACK,stack_end=STACK+8192;
#if CONF_WITH_NATIVE_PREEMPT
static BOOL owned_code=TRUE,owned_stack=TRUE;
#endif
static unsigned allocations,frees,destroys,fail_alloc,cancel_calls;
#if CONF_WITH_IRQ_RESTART_TEST
static unsigned suspended_probe,fault_calls;
#endif
static UQUAD selected=4,user_observed=3;
static ULONG current_tid=17;
static jmp_buf exit_resume;
static IRQ_RESTART_ARGS arguments={CODE,4096,STACK,8192};

void panic(const char *format,...) { (void)format;abort(); }
void doassert(const char *file,long line,const char *function,const char *expression)
{ fprintf(stderr,"%s:%ld %s: %s\n",file,line,function,expression);abort(); }
void *kalloc(ULONG bytes)
{
    void *p;assert(!critical);
    if(fail_alloc && !--fail_alloc)return NULL;
    p=calloc(1,bytes);if(p)allocations++;return p;
}
void kfree(void *p) { assert(!critical);if(p){frees++;free(p);} }
void aspace_destroy(ASPACE *as) { assert(as->alive);as->alive=0;destroys++; }
BOOL aspace_translate(const ASPACE *as,VADDR va,PADDR *pa,UWORD *rights)
{
    UWORD protection=ASPACE_PROT_USER;
    assert(as->alive);
    if(va>=CODE && va<CODE+4096 && resident_code)protection|=ASPACE_PROT_EXEC;
    else if(va>=stack_first && va<stack_end && resident_stack)protection|=ASPACE_PROT_WRITE;
    else return FALSE;
    if(pa)*pa=va;
    if(rights)*rights=protection;
    return TRUE;
}
BOOL aspace_logical_protection(const ASPACE *as,VADDR va,UWORD *rights)
{ return aspace_translate(as,va,NULL,rights); }
BOOL aspace_private_range_ok(const ASPACE *as,VADDR va,VADDR bytes)
{ (void)as;return bytes && (va==CODE || (va>=STACK && va<STACK+8192)); }
#if CONF_WITH_NATIVE_PREEMPT
BOOL aspace_irq_range_owned(const ASPACE *as,VADDR va,VADDR bytes)
{ return (va==CODE?owned_code:owned_stack) && aspace_private_range_ok(as,va,bytes); }
BOOL aspace_retired_pin_overlap(const ASPACE *as,VADDR va,VADDR bytes)
{ (void)as;(void)va;(void)bytes;return FALSE; }
BOOL aspace_retired_pin_next(const ASPACE *as,VADDR from,VADDR *va,VADDR *bytes)
{ (void)as;(void)from;(void)va;(void)bytes;return FALSE; }
#endif
BOOL aspace_release_lazy(ASPACE *as,VADDR va,VADDR bytes)
{ (void)as;(void)va;(void)bytes;return TRUE; }
BOOL aspace_release_lazy_retired(ASPACE *as,VADDR va,VADDR bytes)
{ (void)as;(void)va;(void)bytes;return TRUE; }
BOOL aspace_release_lazy_retired_allowed(const ASPACE *as,VADDR va,VADDR bytes)
{ (void)as;(void)va;(void)bytes;return TRUE; }
void aspace_unborrow(ASPACE *as,VADDR va,VADDR bytes)
{ (void)as;(void)va;(void)bytes;abort(); }
void aspace_unborrow_retired(ASPACE *as,VADDR va,VADDR bytes)
{ (void)as;(void)va;(void)bytes;abort(); }
BOOL aspace_defer_unborrow(ASPACE *as,VADDR va,VADDR bytes)
{ (void)as;(void)va;(void)bytes;abort();return FALSE; }
#if CONF_WITH_MMAP
void fileback_put(FILE_BACKING *backing) { (void)backing;abort(); }
#endif
UQUAD ksched_context_id(void) { return execution.identity; }
KTHREAD_CONTEXT *ksched_interrupt_target(PD *loaded,KASPACE **space,ULONG *tid)
{
    assert(loaded==run);*space=process.space;*tid=current_tid;
    return target?&execution:NULL;
}
KTHREAD *ksched_exit_thread(PD *loaded) { assert(loaded==run);return &thread; }
BOOL kthread_is_executing(const KTHREAD *t) { return t->executing; }
KPROC *kthread_process(const KTHREAD *t) { return t->process; }
UQUAD kproc_ref_number(const KPROC *p) { return p->number; }
PD *kproc_live_pd(const KPROC *p) { return p->pd; }
KASPACE *kproc_active_space(const KPROC *p) { return p->space; }
BOOL ksched_cleanup_active(void) { return cleanup; }
BOOL ksched_cancellation_pending(void) { return cancelled; }
LONG kproc_copy_from_user_checked(void *destination,VADDR source,ULONG bytes)
{
    assert(!critical);
    if(source!=0x20000000UL || bytes!=sizeof(arguments))return EIMBA;
    memcpy(destination,&arguments,bytes);return E_OK;
}
UQUAD ksched_irq_restart_selected(void) { return selected; }
UQUAD ksched_irq_restart_user_observed(void) { return user_observed; }
void ksched_irq_restart_observe_user(UQUAD identity)
{ assert(identity==execution.identity);user_observed++; }
BOOL ksched_block(BOOL (*ready)(void *),void *argument)
{
    assert(!critical && ready(argument));
    critical=TRUE;
    interrupt_restart_root_ack(&execution);
    execution.interrupted_user.valid=FALSE;
    execution.on_cpu=FALSE;
    critical=FALSE;
#if CONF_WITH_NATIVE_PREEMPT
    interrupt_restart_reap(&execution);
#endif
#if CONF_WITH_IRQ_RESTART_TEST
    if (suspended_probe) {
        ASPACE *as=kaspace_backend(process.space);
        unsigned live=allocations-frees;
        assert(execution.irq_restart.state==IRQ_RESTART_SUSPENDED);
        assert(!kaspace_mapping_change(as,CODE,4096));
        assert(!kaspace_mapping_change(as,STACK,8192));
        assert(!kaspace_materialization_change(as,CODE,4096,ASPACE_PROT_USER|ASPACE_PROT_EXEC));
        /* Resident COW writes are legal under a held logical view. A
         * destructive mutation, or a new backing claim, cannot use it. */
        assert(kaspace_cow_change(as,STACK,4096));
        assert(allocations-frees==live);
        if(suspended_probe==2)resident_code=FALSE; /* injected backend failure */
        if(suspended_probe==3)resident_stack=FALSE;
    }
#endif
    selected+=2;if(peer)user_observed++;
    execution.on_cpu=TRUE;
    return TRUE;
}
void ksched_fault(PD *loaded,WORD signal)
{
#if CONF_WITH_IRQ_RESTART_TEST
    assert((suspended_probe==2 || suspended_probe==3) && loaded==run && signal==11 && !critical);
    assert(execution.on_cpu && execution.irq_restart.state==IRQ_RESTART_ABORTED);
    fault_calls++;
#else
    (void)loaded;(void)signal;abort();
#endif
}
void ksched_cancel_point(void)
{
    assert(!critical && cancelled && execution.on_cpu);
    assert(execution.irq_restart.state==IRQ_RESTART_ABORTED);
    cancel_calls++;longjmp(exit_resume,1);
}
void xterm(UWORD status)
{
#if CONF_WITH_IRQ_RESTART_TEST
    assert(status==(UWORD)-1 && fault_calls && execution.irq_restart.state==IRQ_RESTART_ABORTED);
    longjmp(exit_resume,1);
#else
    (void)status;abort();
#endif
}

#if CONF_WITH_IRQ_RESTART_TEST
static LONG operation(UQUAD selector)
{ return interrupt_restart_test((selector==IRQ_RESTART_BEGIN || selector==IRQ_RESTART_NO_CREDIT)?0x20000000UL:0,selector); }
#endif
#if CONF_WITH_NATIVE_PREEMPT
#include "ksched_quantum.h"
static KSCHED_QUANTUM quantum;
static UWORD patterns;
BOOL ksched_preempt_due(void) { return quantum.pending || cancelled; }
void ksched_preempt_probe_return(UWORD pattern) { patterns|=pattern; }
void ksched_cleanup_begin(KSCHED_CLEANUP_SCOPE *scope)
{ assert(!critical && !cleanup);cleanup=TRUE;scope->active=TRUE; }
void ksched_cleanup_end(KSCHED_CLEANUP_SCOPE *scope)
{ assert(!critical && cleanup && scope->active);cleanup=FALSE;scope->active=FALSE; }
#endif
static void capture(void)
{
    X86_USER_IRQ_CAPTURE *c=&execution.interrupted_user;
    c->valid=TRUE;c->sequence++;c->execution=execution.identity;
    c->tid=current_tid;c->cpu=execution.owner_cpu;
    c->image.rip=CODE;c->image.rsp=STACK+4096;
}
static void freeze(void)
{
    capture();critical=TRUE;
    assert(interrupt_restart_admit(&execution,execution.interrupted_user.sequence,CODE,STACK+4096));
    critical=FALSE;
}
#if CONF_WITH_IRQ_RESTART_TEST
int main(void)
{
    ASPACE backend={1};
    KASPACE_HEAP *heap;
    unsigned baseline,i;
    IRQ_RESTART retired;
    process.space=kaspace_adopt(&backend);assert(process.space);
    process.number=1;process.pd=run;thread.process=&process;thread.executing=TRUE;
    execution.identity=11;execution.on_cpu=TRUE;
    heap=kaspace_heap(process.space);
    heap->blocks=kalloc(sizeof(*heap->blocks));assert(heap->blocks);
    heap->count=heap->capacity=1;
    heap->blocks[0].va=STACK;heap->blocks[0].pages=2;heap->blocks[0].reservation=1;
    baseline=allocations-frees;
    assert(operation(IRQ_RESTART_QUERY)==IRQ_RESTART_EMPTY);
    assert(operation(IRQ_RESTART_NO_CREDIT)==E_OK);
    assert(operation(IRQ_RESTART_FINISH)==EACCDN); /* no IRQ in the armed window */
    assert(operation(IRQ_RESTART_NO_CREDIT)==E_OK);
    capture();interrupt_restart_user_capture(&execution);
    assert(operation(IRQ_RESTART_FINISH)==EINVFN); /* genuine capture with transport refused */
    assert(operation(IRQ_RESTART_QUERY)==IRQ_RESTART_EMPTY && allocations-frees==baseline);
    for(i=0;i<64;i++) {
        unsigned n;
        for(n=1;n<=2;n++) {
            assert(prepare(&execution,process.space,current_tid,&arguments,n)==ENSMEM);
            assert(execution.irq_restart.state==IRQ_RESTART_EMPTY && allocations-frees==baseline);
        }
        fail_alloc=1;assert(operation(IRQ_RESTART_BEGIN)==ENSMEM);
        fail_alloc=2;assert(operation(IRQ_RESTART_BEGIN)==ENSMEM);
        assert(allocations-frees==baseline);
        resident_code=FALSE;assert(operation(IRQ_RESTART_BEGIN)==EACCDN);resident_code=TRUE;
        resident_stack=FALSE;assert(operation(IRQ_RESTART_BEGIN)==EACCDN);resident_stack=TRUE;
        assert(operation(IRQ_RESTART_BEGIN)==E_OK && allocations-frees==baseline+2);
        process.number++;
        assert(operation(IRQ_RESTART_QUERY)==EACCDN); /* attached stale owner is not EMPTY */
        process.number--;
        assert(operation(IRQ_RESTART_BEGIN)==EACCDN);
        assert(!kaspace_mapping_change(&backend,CODE,1) && !kaspace_mapping_change(&backend,STACK,1));
        capture();critical=TRUE;
        target=FALSE;assert(!interrupt_restart_admit(&execution,1,CODE,STACK));target=TRUE;
        cancelled=TRUE;assert(!interrupt_restart_admit(&execution,1,CODE,STACK));cancelled=FALSE;
        cleanup=TRUE;assert(!interrupt_restart_admit(&execution,1,CODE,STACK));cleanup=FALSE;
        assert(!interrupt_restart_admit(&execution,0,CODE,STACK));
        assert(!interrupt_restart_admit(&execution,1,CODE+4096,STACK));
        assert(!interrupt_restart_admit(&execution,1,CODE,STACK+8192));
        critical=FALSE;freeze();peer=(i&1)!=0;suspended_probe=1;
        assert(interrupt_restart_suspend(&execution)==peer);
        suspended_probe=0;
        critical=TRUE;
        assert(interrupt_restart_return_valid(&execution));
        execution.identity++;assert(!interrupt_restart_return_valid(&execution));execution.identity--;
        current_tid++;assert(!interrupt_restart_return_valid(&execution));current_tid--;
        process.number++;assert(!interrupt_restart_return_valid(&execution));process.number--;
        execution.interrupted_user.sequence++;assert(!interrupt_restart_return_valid(&execution));execution.interrupted_user.sequence--;
        interrupt_restart_ack(&execution);
        critical=FALSE;
        assert(operation(IRQ_RESTART_FINISH)==(peer?E_OK:EAGAIN));
        assert(operation(IRQ_RESTART_ABORT)==EACCDN && allocations-frees==baseline);
        assert(kaspace_mapping_change(&backend,CODE,1) && kaspace_mapping_change(&backend,STACK,1));
    }
    assert(operation(IRQ_RESTART_NO_CREDIT)==E_OK);freeze();peer=TRUE;
    assert(!interrupt_restart_suspend(&execution));
    assert(execution.irq_restart.transported && interrupt_restart_return_valid(&execution));
    interrupt_restart_ack(&execution);
    assert(operation(IRQ_RESTART_FINISH)==EAGAIN && allocations-frees==baseline);
    assert(operation(IRQ_RESTART_BEGIN)==E_OK);freeze();cancelled=TRUE;
    if(!setjmp(exit_resume)) { (void)interrupt_restart_suspend(&execution);abort(); }
    assert(cancel_calls==1 && allocations-frees==baseline+2);
    execution.on_cpu=FALSE;interrupt_restart_dispose(&execution);execution.on_cpu=TRUE;
    cancelled=FALSE;assert(allocations-frees==baseline);
    for(suspended_probe=2;suspended_probe<=3;suspended_probe++) {
        assert(operation(IRQ_RESTART_BEGIN)==E_OK);freeze();
        if(!setjmp(exit_resume)) { (void)interrupt_restart_suspend(&execution);abort(); }
        assert(fault_calls==suspended_probe-1 && allocations-frees==baseline+2);
        assert(!interrupt_restart_return_valid(&execution));
        execution.on_cpu=FALSE;interrupt_restart_dispose(&execution);execution.on_cpu=TRUE;
        resident_code=resident_stack=TRUE;
        assert(allocations-frees==baseline);
    }
    suspended_probe=0;
    assert(operation(IRQ_RESTART_BEGIN)==E_OK);
    critical=TRUE;interrupt_restart_detach(&execution,&retired);critical=FALSE;
    execution.identity++;assert(allocations-frees==baseline+2);
    interrupt_restart_retire(&retired);assert(allocations-frees==baseline);
    assert(operation(IRQ_RESTART_BEGIN)==E_OK);
    kaspace_put(process.space);assert(!destroys); /* Leases retain the root. */
    assert(operation(IRQ_RESTART_ABORT)==E_OK);
    assert(destroys==1 && allocations==frees);
    puts("Retained IRQ transaction: rollback, no-peer return, stale identity, suspended mapping exclusion/fault, cancellation, replacement and exact lease/root conservation: PASS (dispatch/CPU/PROCESS models)");
    return 0;
}

#else
int main(void)
{
    ASPACE backend={1};
    KASPACE *thread_space;
    unsigned baseline,i;
    process.space=kaspace_adopt(&backend);assert(process.space);
    thread_space=kaspace_share(process.space);assert(thread_space);
    process.number=1;process.pd=run;thread.process=&process;thread.executing=TRUE;
    execution.identity=11;execution.on_cpu=TRUE;
    baseline=allocations-frees;
    for(i=1;i<=2;i++) {
        fail_alloc=i;
        assert(interrupt_restart_reserve(&execution.irq_restart)==ENSMEM);
        assert(execution.irq_restart.state==IRQ_RESTART_EMPTY && allocations-frees==baseline);
    }
    assert(interrupt_restart_reserve(&execution.irq_restart)==E_OK);
    assert(allocations-frees==baseline+2);
    for(i=0;i<64;i++) {
        unsigned tick;
        capture();critical=TRUE;ksched_quantum_reset(&quantum);
        execution.interrupted_user.image.r15=(i&1)?0x7240000000724015ULL:0x7130000000713015ULL;
        peer=(i%4)!=0;
        for(tick=0;tick<8;tick++)ksched_quantum_tick(&quantum,FALSE);
        assert(quantum.left==KSCHED_QUANTUM_TICKS && !quantum.pending);
        assert(!ksched_quantum_capture_needed(&quantum));
        assert(!interrupt_restart_admit(&execution,execution.interrupted_user.sequence,CODE,STACK+4096));
        for(tick=1;tick<KSCHED_QUANTUM_TICKS;tick++) {
            assert(ksched_quantum_capture_needed(&quantum)==(quantum.left==1));
            ksched_quantum_tick(&quantum,TRUE);assert(!quantum.pending);
        }
        assert(ksched_quantum_capture_needed(&quantum)); /* before expiring device tick */
        ksched_quantum_tick(&quantum,TRUE);assert(quantum.pending);
        assert(ksched_quantum_capture_needed(&quantum));
        cleanup=TRUE;assert(!interrupt_restart_admit(&execution,1,CODE,STACK));cleanup=FALSE;
        target=FALSE;assert(!interrupt_restart_admit(&execution,1,CODE,STACK));target=TRUE;
        resident_code=FALSE;assert(!interrupt_restart_admit(&execution,1,CODE,STACK));resident_code=TRUE;
        resident_stack=FALSE;assert(!interrupt_restart_admit(&execution,1,CODE,STACK));resident_stack=TRUE;
        critical=FALSE;interrupt_restart_user_entry();critical=TRUE;
        for(tick=0;tick<100;tick++)ksched_quantum_tick(&quantum,TRUE);
        assert(!quantum.left && quantum.pending); /* unsafe IRQs never lose expiry */
        assert(kaspace_mapping_can_change(&backend,CODE,1));
        execution.identity++;assert(!interrupt_restart_admit(&execution,1,CODE,STACK));execution.identity--;
        critical=FALSE;
        if(!(i%8))assert(kaspace_mutation_begin(process.space));
        freeze();
        assert(!kaspace_mapping_can_change(&backend,CODE,1));
        assert(!kaspace_mapping_can_change(&backend,STACK,1));
        assert(interrupt_restart_suspend(&execution)==peer);
        critical=TRUE;
        assert(interrupt_restart_return_valid(&execution));
        current_tid++;assert(!interrupt_restart_return_valid(&execution));current_tid--;
        process.number++;assert(!interrupt_restart_return_valid(&execution));process.number--;
        interrupt_restart_ack(&execution);
        assert(execution.irq_restart.state==IRQ_RESTART_ARMED);
        assert(!kaspace_mapping_can_change(&backend,CODE,1));
        assert(!kaspace_mapping_can_change(&backend,STACK,1));
        assert(allocations-frees==baseline+2);
        critical=FALSE;
        if(!(i%8))kaspace_mutation_end(process.space);
        if(i&1) {
            interrupt_restart_user_entry();
            assert(kaspace_mapping_can_change(&backend,CODE,1));
            assert(kaspace_mapping_can_change(&backend,STACK,1));
        }
    }
    assert(patterns==3);peer=TRUE;
    /* A32 may keep SP at the bottom of a resident page, with a guard below.
     * An exclusive top instead needs the resident page below SP. Both views
     * must retain their actual page and retire after the same root ACK. */
    for(i=0;i<4;i++) {
        VADDR sp=i==1?STACK+8192:STACK+4096+(i==3?4:0);
        VADDR held=i==2?STACK:STACK+4096;
        stack_first=i==2?STACK:STACK+4096;
        capture();execution.interrupted_user.image.rsp=sp;quantum.pending=TRUE;
        critical=TRUE;
        resident_stack=FALSE;
        assert(!interrupt_restart_admit(&execution,execution.interrupted_user.sequence,CODE,sp));
        resident_stack=TRUE;owned_stack=FALSE;
        assert(!interrupt_restart_admit(&execution,execution.interrupted_user.sequence,CODE,sp));
        owned_stack=TRUE;
        assert(interrupt_restart_admit(&execution,execution.interrupted_user.sequence,CODE,sp));
        assert(execution.irq_restart.stack_address==held && execution.irq_restart.stack_bytes==4096);
        assert(!kaspace_mapping_can_change(&backend,held,4096));
        critical=FALSE;
        assert(interrupt_restart_suspend(&execution));
        critical=TRUE;
        assert(interrupt_restart_return_valid(&execution));interrupt_restart_ack(&execution);
        critical=FALSE;interrupt_restart_user_entry();
        assert(kaspace_mapping_can_change(&backend,held,4096));
        assert(allocations-frees==baseline+2);
    }
    stack_first=STACK;
    {
        KASPACE_RESERVATION_HOLD image={0};
        KASPACE_RESERVATION_INFO info={CODE,4096,0,ASPACE_PROT_USER|ASPACE_PROT_EXEC,0};
        void *owner=NULL;
        owned_code=FALSE;capture();critical=TRUE;
        assert(!interrupt_restart_admit(&execution,execution.interrupted_user.sequence,CODE,STACK+4096));
        critical=FALSE;
        info.identity=kaspace_reservation_next();
        assert(kaspace_reservation_hold_captured(process.space,&info,CODE,4096,info.rights,
                                               process.number,&process,&image)==E_OK);
        freeze();assert(interrupt_restart_suspend(&execution));
        /* The authentic existing captured-image witness may retire first;
         * the IRQ's bounded resident view still excludes mapping destruction. */
        assert(kaspace_reservation_release(&image,&owner) && owner==&process);
        assert(!kaspace_mapping_can_change(&backend,CODE,1));
        assert(interrupt_restart_return_valid(&execution));interrupt_restart_ack(&execution);
        freeze();assert(interrupt_restart_suspend(&execution));
        assert(interrupt_restart_return_valid(&execution));interrupt_restart_ack(&execution);
        interrupt_restart_user_entry();
        assert(kaspace_mapping_can_change(&backend,CODE,1));
        assert(allocations-frees==baseline+2);owned_code=TRUE;
    }
    /* A CPU-bound cancelled user must still reach the existing safe cancel
     * checkpoint: cancellation is not a reason to refuse automatic departure. */
    stack_first=STACK+4096; /* cancellation must also depart from a bottom SP */
    cancelled=TRUE;ksched_quantum_reset(&quantum);freeze();
    if(!setjmp(exit_resume)) { (void)interrupt_restart_suspend(&execution);abort(); }
    assert(cancel_calls==1);
    execution.on_cpu=FALSE;interrupt_restart_dispose(&execution);execution.on_cpu=TRUE;
    cancelled=FALSE;stack_first=STACK;assert(allocations-frees==baseline);
    assert(interrupt_restart_reserve(&execution.irq_restart)==E_OK);quantum.pending=TRUE;freeze();
    assert(interrupt_restart_suspend(&execution));interrupt_restart_ack(&execution);
    kaspace_put(process.space);assert(!destroys);
    execution.on_cpu=FALSE;interrupt_restart_dispose(&execution);
    kaspace_put(thread_space);
    assert(destroys==1 && allocations==frees);
    puts("Automatic IRQ owner: preallocated rollback, deferred admission, 64 allocation-free cycles, stale identities, cancellation and exact conservation: PASS (CPU/dispatcher models)");
    return 0;
}
#endif
