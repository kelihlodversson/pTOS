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
static unsigned allocations,frees,destroys,fail_alloc,cancel_calls;
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
    else if(va>=STACK && va<STACK+8192 && resident_stack)protection|=ASPACE_PROT_WRITE;
    else return FALSE;
    if(pa)*pa=va;
    if(rights)*rights=protection;
    return TRUE;
}
BOOL aspace_logical_protection(const ASPACE *as,VADDR va,UWORD *rights)
{ return aspace_translate(as,va,NULL,rights); }
BOOL aspace_private_range_ok(const ASPACE *as,VADDR va,VADDR bytes)
{ (void)as;return bytes && (va==CODE || va==STACK); }
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
    selected+=2;if(peer)user_observed++;
    execution.on_cpu=TRUE;
    return TRUE;
}
void ksched_fault(PD *loaded,WORD signal) { (void)loaded;(void)signal;abort(); }
void ksched_cancel_point(void)
{
    assert(!critical && cancelled && execution.on_cpu);
    assert(execution.irq_restart.state==IRQ_RESTART_ABORTED);
    cancel_calls++;longjmp(exit_resume,1);
}
void xterm(UWORD status) { (void)status;abort(); }

static LONG operation(UQUAD selector)
{ return interrupt_restart_test((selector==IRQ_RESTART_BEGIN || selector==IRQ_RESTART_NO_CREDIT)?0x20000000UL:0,selector); }
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
        assert(operation(IRQ_RESTART_BEGIN)==EACCDN);
        assert(!kaspace_mapping_change(&backend,CODE,1) && !kaspace_mapping_change(&backend,STACK,1));
        capture();critical=TRUE;
        target=FALSE;assert(!interrupt_restart_admit(&execution,1,CODE,STACK));target=TRUE;
        cancelled=TRUE;assert(!interrupt_restart_admit(&execution,1,CODE,STACK));cancelled=FALSE;
        cleanup=TRUE;assert(!interrupt_restart_admit(&execution,1,CODE,STACK));cleanup=FALSE;
        assert(!interrupt_restart_admit(&execution,0,CODE,STACK));
        assert(!interrupt_restart_admit(&execution,1,CODE+4096,STACK));
        assert(!interrupt_restart_admit(&execution,1,CODE,STACK+8192));
        critical=FALSE;freeze();peer=(i&1)!=0;
        assert(interrupt_restart_suspend(&execution)==peer);
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
    assert(operation(IRQ_RESTART_BEGIN)==E_OK);
    critical=TRUE;interrupt_restart_detach(&execution,&retired);critical=FALSE;
    execution.identity++;assert(allocations-frees==baseline+2);
    interrupt_restart_retire(&retired);assert(allocations-frees==baseline);
    assert(operation(IRQ_RESTART_BEGIN)==E_OK);
    kaspace_put(process.space);assert(!destroys); /* Leases retain the root. */
    assert(operation(IRQ_RESTART_ABORT)==E_OK);
    assert(destroys==1 && allocations==frees);
    puts("Retained IRQ transaction: rollback, no-peer return, stale identity, cancellation, replacement and exact lease/root conservation: PASS (dispatch/CPU/PROCESS models)");
    return 0;
}
