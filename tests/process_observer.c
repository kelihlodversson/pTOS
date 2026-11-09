/* Production KPROC lifetime/registry plus non-consuming observer ownership.
 * CPU/MMU resources are absent; any unexpected heavyweight release aborts. */
#include <stdlib.h>
#include <stdio.h>
#include "emutos.h"
#include "string.h"
#include "../bdos/kproc.c"
#include "process_observer.h"
#undef assert
#include <assert.h>

PD caller, child;
PD *run = &caller;
volatile LONG hz_200;
SBYTE get_default_handle(int i) { (void)i; return -1; }
static unsigned allocations, frees;
static BOOL fail_allocate;
static unsigned post_unlink_checks;
static PROCESS_OBSERVER *during_retirement;
static void (*allocation_finished)(void *);
static void *allocation_token;
BOOL procmem_on_free(void *base, void (*notify)(void *), void *token)
{
    assert(base==&child && !allocation_finished && token && notify);
    allocation_finished=notify; allocation_token=token;
    return TRUE;
}
static UQUAD retained_number;
static WORD retained_pid;
void *kalloc(ULONG bytes) { void *p; if (fail_allocate) return NULL; p=calloc(1,bytes); if(p) allocations++; return p; }
void kfree(void *p) { if(p) { frees++; free(p); } }
void panic(const char *format, ...) { fprintf(stderr,"panic: %s\n",format); abort(); }
void bzero(void *p, size_t size)
{
    memset(p,0,size);
    if (during_retirement && p==(UBYTE *)during_retirement+offsetof(KPROC,pd)) {
        PROCESS_COMPLETION snap;
        assert(!kproc_find(&child) && !kproc_file_references_allowed(&child));
        assert(!kproc_begin_file_cleanup(&child));
        assert(kproc_file_references_allowed(&caller));
        post_unlink_checks++;
        assert(!process_observer_ready(during_retirement));
        assert(process_observer_snapshot(during_retirement,&snap));
        assert(snap.number_low==(ULONG)retained_number && snap.pid==retained_pid &&
               snap.kind==PROCESS_COMPLETION_NORMAL && snap.status==0xffff);
    }
}
UQUAD procmem_gen(const void *p) { return p ? 1 : 0; }
ULONG procmem_size(const void *p) { (void)p; return 0; }
void kaspace_put(KASPACE *p)
{
    assert(!p);
    /* Production kproc_destroy has detached the registry before reaching
     * nested teardown/resource callbacks. Neither that phase nor the payload
     * wipe may reopen a retiring parent's file admission as a boot PD. */
    if (during_retirement) {
        assert(!kproc_find(&child) && !kproc_file_references_allowed(&child));
        assert(!kproc_begin_file_cleanup(&child));
        assert(kproc_file_references_allowed(&caller));
        post_unlink_checks++;
    }
}
void kstack_free(PADDR p) { assert(!p); }

void doassert(const char *file, long line, const char *function, const char *expression)
{
    fprintf(stderr,"%s:%ld %s: %s\n",file,line,function,expression); abort();
}
void aspace_leave(ASPACE_ROOT root) { assert(!root); }
BOOL aspace_release_lazy(ASPACE *as, VADDR address, VADDR bytes)
{ (void)as; (void)address; (void)bytes; abort(); }
BOOL aspace_move_private(ASPACE *from, ASPACE *to, VADDR address, VADDR bytes, UWORD prot)
{ (void)from; (void)to; (void)address; (void)bytes; (void)prot; abort(); }
int main(void)
{
    PROCESS_OBSERVER *a=NULL,*b=NULL,*replacement=NULL;
    PROCESS_COMPLETION snap;
    KPROC *p;
    PROCESS_IDENTITY identity;
    ULONG references;
    ULONG first_order;
    assert(kproc_create(&caller) && kproc_create(&child));
    kproc_find(&caller)->uft[0]=NUMSTD;
    kproc_find(&child)->uft[1]=NUMSTD;
    kproc_find(&child)->uft[2]=NUMSTD+1;
    kproc_standard_file_invalidate(NUMSTD);
    assert(kproc_find(&caller)->uft[0]<0 && kproc_find(&child)->uft[1]<0 &&
           kproc_find(&child)->uft[2]==NUMSTD+1);
    p=kproc_retain(&child); assert(p);
    assert(process_observer_attach(p,&a)==0 && process_observer_attach(p,&b)==0);
    assert(process_observer_attach(p,&a)==EINVAL && p->references==4);
    references=p->references; p->references=~(ULONG)0;
    assert(process_observer_attach(p,&replacement)==EACCDN && !replacement);
    p->references=references;
    assert(process_observer_snapshot(a,&snap) && snap.state==PROCESS_UNSTARTED && !process_observer_ready(a));
    retained_number=p->number; retained_pid=p->pid;
    kproc_mark_started(&child);
    assert(process_observer_snapshot(a,&snap) && snap.state==PROCESS_RUNNING && snap.kind==PROCESS_COMPLETION_NONE);
    assert(kproc_file_references_allowed(&child) && kproc_file_references_allowed(&caller));
    assert(kproc_begin_file_cleanup(&child) && !kproc_begin_file_cleanup(&child));
    assert(!kproc_file_references_allowed(&child) && kproc_file_references_allowed(&caller));
    kproc_note_file_cleanup(&child,EWRITF,TRUE);
    kproc_note_file_cleanup(&child,EINTRN,FALSE);
    kproc_complete(&child,0xffff); kproc_note_fault(&child,4); kproc_complete(&child,9);
    assert(process_observer_snapshot(a,&snap) && snap.kind==PROCESS_COMPLETION_NORMAL && snap.status==0xffff && !snap.signal);
    assert(!process_observer_ready(a)); /* Committed result is not notification. */
    during_retirement=a;
    assert(kproc_begin_exit(&child)); kproc_destroy(&child); during_retirement=NULL;
    assert(!kproc_ref_has_basepage(p) && p->retired && p->references==3);
    assert(post_unlink_checks>=3 && !p->destroying_pd);
    assert(kproc_ref_file_cleanup(p,&references)==EWRITF && references==1);
    assert(!process_observer_ready(a)); /* Scheduler has not published wait yet. */
    kproc_publish_observers(p); kproc_publish_observers(p);
    assert(process_observer_ready(a));
    assert(process_observer_snapshot(a,&snap) && snap.order_low && !snap.order_high);
    first_order=snap.order_low;
    assert(kproc_create(&child) && process_observer_snapshot(a,&snap) && snap.pid==retained_pid);
    assert(process_observer_attach(kproc_find(&child),&replacement)==0);
    { PROCESS_OBSERVER *unpublished=NULL;
      assert(process_observer_attach(kproc_find(&child),&unpublished)==0);
      process_observer_release(&unpublished); assert(!unpublished); }
    assert(replacement->number>retained_number && replacement->pid!=retained_pid);
    kproc_mark_started(&child); kproc_note_fault(&child,4); kproc_note_fault(&child,11);
    assert(!process_observer_ready(replacement));
    kproc_complete(&child,0xffff);
    assert(process_observer_snapshot(replacement,&snap) && snap.kind==PROCESS_COMPLETION_SIGNAL && snap.signal==4 && !snap.status);
    assert(kproc_begin_exit(&child)); kproc_destroy(&child);
    assert(!process_observer_ready(replacement));
    kproc_publish_observers(replacement);
    assert(process_observer_snapshot(replacement,&snap) && snap.order_low>first_order);
    process_observer_release(&a); process_observer_release(&a);
    assert(process_observer_ready(b) && p->references==2);
    process_observer_release(&b); kproc_release(p);
    process_observer_release(&replacement);
    memset(&identity,0,sizeof(identity)); identity.selector=PROCESS_IDENTITY_NUMBER;
    identity.number_low=(ULONG)retained_number; identity.number_high=(ULONG)(retained_number>>32);
    assert(kproc_identity_info(&caller,&identity)==ESRCH);
    last_pid=retained_pid-1; /* Host-only PID reuse; never rewind PROCESS numbers. */
    fail_allocate=TRUE; assert(!kproc_create(&child)); fail_allocate=FALSE;
    assert(kproc_create(&child)); p=kproc_retain(&child);
    assert(p->pid==retained_pid && p->number>retained_number);
    assert(process_observer_attach(p,&a)==0);
    assert(kproc_begin_exit(&child)); kproc_destroy(&child);
    assert(!process_observer_ready(a) && process_observer_snapshot(a,&snap) &&
           snap.state==PROCESS_UNSTARTED);
    kproc_publish_observers(p);
    assert(process_observer_snapshot(a,&snap) && snap.state==PROCESS_ABANDONED &&
           snap.kind==PROCESS_COMPLETION_NONE && !snap.status && !snap.signal && !snap.order_low && !snap.order_high);
    assert(process_observer_ready(a)); process_observer_release(&a); kproc_release(p);
    {
        PD slow = {0}, fast = {0};
        PROCESS_OBSERVER *one=NULL, *two=NULL;
        PROCESS_COMPLETION first, second;
        assert(kproc_create(&slow) && kproc_create(&fast));
        assert(process_observer_attach(kproc_find(&slow),&one)==0 &&
               process_observer_attach(kproc_find(&fast),&two)==0);
        kproc_mark_started(&slow); kproc_mark_started(&fast);
        kproc_complete(&slow,10); /* Committed first, but not yet teardown-safe. */
        kproc_complete(&fast,11);
        assert(kproc_begin_exit(&fast)); kproc_destroy(&fast);
        kproc_publish_observers(two);
        assert(!process_observer_ready(one) && process_observer_ready(two));
        assert(kproc_begin_exit(&slow)); kproc_destroy(&slow);
        kproc_publish_observers(one);
        assert(process_observer_snapshot(one,&first) && process_observer_snapshot(two,&second));
        assert(first.status==10 && second.status==11 && second.order_low<first.order_low);
        /* The same canonical ordering is visible through every observer. */
        kproc_publish_observers(two);
        assert(process_observer_snapshot(two,&snap) && snap.order_low==second.order_low);
        process_observer_release(&one); process_observer_release(&two);
    }
    /* Refused-start disposal can remain pending until root drops membership.
     * Membership is modeled here; registry/ref/retirement code is production. */
    assert(kproc_create(&child)); p=kproc_retain(&child);
    assert(process_observer_attach(p,&a)==E_OK);
    p->threads=(KTHREAD *)1;
    { PD *claimed=NULL;
      assert(!kproc_begin_disposal(p,&claimed) && !claimed && !process_observer_ready(a));
      assert(!kproc_begin_disposal(p,&claimed) && !claimed);
      p->threads=NULL; /* Safe root detach in the scheduler fixture. */
      assert(kproc_begin_disposal(p,&claimed) && claimed==&child); }
    kproc_destroy(&child);
    assert(!process_observer_ready(a)); kproc_publish_observers(p);
    assert(process_observer_ready(a) && process_observer_snapshot(a,&snap) && snap.state==PROCESS_ABANDONED);
    process_observer_release(&a); kproc_release(p);
    /* The real KPROC discard retains metadata until the allocator, not the
     * record unlink, confirms final reclamation (including delayed pins). */
    assert(kproc_create(&child));
    assert(process_observer_attach(kproc_find(&child),&a)==E_OK);
    kproc_discard_after_free(&child);
    assert(!process_observer_ready(a) && !kproc_ref_has_basepage(a) &&
           process_observer_snapshot(a,&snap) && snap.state==PROCESS_UNSTARTED);
    assert(allocation_finished && allocation_token==a);
    allocation_finished(allocation_token);
    allocation_finished=NULL; allocation_token=NULL;
    assert(process_observer_ready(a)); process_observer_release(&a);
    assert(kproc_begin_exit(&caller)); kproc_destroy(&caller);
    assert(allocations==frees && !kproc_list && !retired_processes);
    puts("Production KPROC observation: full status/fault, retirement, PD reuse, multiple references and rollback: PASS");
    return 0;
}
