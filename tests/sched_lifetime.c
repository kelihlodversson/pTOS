/* Production ownership implementation; CPU switching is covered natively. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "bdosdefs.h"
#include "string.h"
#include "gemerror.h"
struct kproc { PD *pd; ULONG refs; BOOL live; };
#include "../bdos/sched_lifetime.c"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"admission:%d: %s\n",__LINE__,#x); abort(); } } while (0)
static PD kernel_pd, recycled_pd;
static unsigned allocated, freed, pins;
static BOOL fail_alloc, fail_ref;
void *kalloc(ULONG bytes) { void *p; if (fail_alloc) return NULL; p=malloc(bytes); if (p) allocated++; return p; }
void kfree(void *p) { CHECK(p); freed++; free(p); }
void panic(const char *fmt, ...) { (void)fmt; abort(); }
UQUAD procmem_gen(const void *p) { return p==&kernel_pd?99:0; }
BOOL procmem_range_live(VADDR p,VADDR n) { return p==(VADDR)(uintptr_t)&kernel_pd && n==sizeof(PD); }
void procmem_pin(VADDR p,VADDR n,int d) { CHECK(p==(VADDR)(uintptr_t)&kernel_pd && n==sizeof(PD) && d==1); pins++; }
KPROC *kproc_retain_live(KPROC *p) { if (fail_ref || !p->live) return NULL; p->refs++; return p; }
void kproc_release(KPROC *p) { CHECK(p && p->refs>1); p->refs--; }
PD *kproc_live_pd(const KPROC *p) { return p->live?p->pd:NULL; }
int main(void)
{
    KPROC old={&recycled_pd,1,TRUE}, replacement={&recycled_pd,1,TRUE};
    KSCHED_ADMISSION a={0}, b={0}, producer={0}, stale={0};
    LONG output[2]={-7,-8};
    UQUAD token;
    unsigned cycle;
    CHECK(ksched_controller_install(&kernel_pd) && ksched_controller_install(&kernel_pd) && pins==1);
    CHECK(!ksched_controller_install(&recycled_pd));
    CHECK(ksched_admission_create(&old,0,&a)==EINVAL && !a.identity);
    CHECK(ksched_admission_create(&old,KSCHED_LIMIT+1,&a)==EINVAL && !a.identity);
    controller.references=0xffffffffUL;
    CHECK(ksched_admission_create(&old,1,&a)==EACCDN && !a.identity && !allocated);
    controller.references=1;
    for (cycle=0;cycle<32;cycle++) {
        token=last_admission;
        fail_alloc=TRUE;
        CHECK(ksched_admission_create(&old,2,&a)==ENSMEM && !a.identity && last_admission==token);
        fail_alloc=FALSE; fail_ref=TRUE;
        CHECK(ksched_admission_create(&old,2,&a)==EACCDN && !a.identity && last_admission==token && old.refs==1);
        fail_ref=FALSE;
        CHECK(ksched_admission_create(&old,2,&a)==0 && old.refs==2);
        CHECK(ksched_admission_create(&old,2,&a)==EINVAL && old.refs==2);
        lookup(&a)->references=0xffffffffUL;
        CHECK(!ksched_admission_retain(&a,&producer) && !producer.identity);
        lookup(&a)->references=1;
        CHECK(ksched_admission_retain(&a,&producer) && !ksched_admission_retain(&a,&producer));
        CHECK(ksched_admission_take(&a,output,2)==EAGAIN && output[0]==-7 && output[1]==-8);
        CHECK(ksched_admission_complete(&producer,1,42) && !ksched_admission_complete(&producer,1,77));
        CHECK(ksched_admission_take(&a,output,2)==EAGAIN && output[0]==-7);
        CHECK(ksched_admission_complete(&producer,0,41));
        CHECK(ksched_admission_take(&a,output,1)==EINVAL && output[0]==-7);
        CHECK(ksched_admission_take(&a,output,2)==0 && output[0]==41 && output[1]==42);
        output[0]=-7; output[1]=-8;
        CHECK(ksched_admission_take(&producer,output,2)==EACCDN && output[0]==-7 && output[1]==-8);
        stale=a;
        ksched_admission_release(&a); ksched_admission_release(&a);
        CHECK(ksched_admission_complete(&producer,0,99)==FALSE);
        ksched_admission_release(&producer);
        CHECK(old.refs==1 && controller.references==1 && !admissions);
        CHECK(ksched_admission_create(&old,2,&a)==0 && a.identity>stale.identity);
        CHECK(!ksched_admission_complete(&stale,0,99));
        ksched_admission_release(&stale); /* stale token cannot release replacement storage */
        CHECK(ksched_admission_create(&replacement,1,&b)==0);
        old.live=FALSE; old.pd=NULL;
        ksched_admission_owner_exited(&old);
        CHECK(ksched_admission_abandoned(&a) && !ksched_admission_abandoned(&b));
        CHECK(!ksched_admission_owner_pd(&a) && ksched_admission_owner_pd(&b)==&recycled_pd);
        CHECK(ksched_admission_complete(&a,0,10) && ksched_admission_complete(&a,1,11));
        CHECK(ksched_admission_take(&a,output,2)==EACCDN && output[0]==-7 && output[1]==-8);
        CHECK(ksched_admission_complete(&b,0,12) && ksched_admission_take(&b,output,1)==0 && output[0]==12);
        ksched_admission_release(&a); ksched_admission_release(&b);
        old.live=TRUE; old.pd=&recycled_pd; output[0]=-7; output[1]=-8;
        CHECK(old.refs==1 && replacement.refs==1 && allocated==freed && controller.references==1);
        CHECK(ksched_admission_create(&old,1,&a)==0 && ksched_admission_retain(&a,&producer));
        ksched_admission_abandon(&a); ksched_admission_abandon(&a); ksched_admission_release(&a);
        CHECK(ksched_admission_complete(&producer,0,14) && ksched_admission_abandoned(&producer));
        CHECK(ksched_admission_take(&producer,output,1)==EACCDN && output[0]==-7);
        ksched_admission_release(&producer);
        CHECK(allocated==freed && old.refs==1 && !admissions && controller.references==1);
    }
    for (cycle=0;cycle<32;cycle++) {
        KSCHED_WAITER first={0}, second={0}, stale_waiter;
        UQUAD previous=last_waiter;
        CHECK(ksched_admission_create(&old,2,&a)==E_OK);
        fail_alloc=TRUE;
        CHECK(ksched_waiter_attach(&a,&old,17,101,2,&first)==ENSMEM && !first.identity && last_waiter==previous);
        fail_alloc=FALSE; fail_ref=TRUE;
        CHECK(ksched_waiter_attach(&a,&old,17,101,2,&first)==EACCDN && !first.identity && last_waiter==previous);
        fail_ref=FALSE;
        CHECK(ksched_waiter_attach(&a,&replacement,17,101,2,&first)==EACCDN && !first.identity);
        CHECK(ksched_waiter_attach(&a,&old,0,101,2,&first)==EINVAL && !first.identity);
        CHECK(ksched_waiter_attach(&a,&old,17,0,2,&first)==EINVAL && !first.identity);
        CHECK(ksched_waiter_attach(&a,&old,17,101,1,&first)==EINVAL && !first.identity);
        CHECK(ksched_waiter_attach(&a,&old,17,101,2,&first)==E_OK);
        CHECK(ksched_waiter_attach(&a,&old,17,101,2,&first)==EINVAL);
        CHECK(ksched_waiter_attach(&a,&old,18,102,2,&second)==E_OK && first.identity!=second.identity);
        CHECK(!ksched_waiter_ready(ksched_waiter_context(&first)));
        CHECK(ksched_admission_complete(&a,0,51));
        CHECK(!ksched_waiter_ready(ksched_waiter_context(&second)));
        ksched_waiter_retire(&replacement,17,101); /* same PD cannot steal an attachment */
        CHECK(ksched_waiter_context(&first));
        ksched_waiter_retire(&old,17,999); /* nor a different execution of the same TID */
        CHECK(ksched_waiter_context(&first));
        CHECK(ksched_admission_complete(&a,1,52));
        CHECK(ksched_waiter_ready(ksched_waiter_context(&first)));
        CHECK(ksched_waiter_take(&first,&old,18,101,output,2)==EACCDN && output[0]==-7);
        CHECK(ksched_waiter_take(&first,&old,17,102,output,2)==EACCDN && output[0]==-7);
        CHECK(ksched_waiter_take(&first,&replacement,17,101,output,2)==EACCDN && output[0]==-7);
        CHECK(ksched_waiter_take(&first,&old,17,101,output,2)==E_OK && output[0]==51 && output[1]==52);
        output[0]=-7; output[1]=-8;
        CHECK(ksched_waiter_ready(ksched_waiter_context(&second)) &&
              ksched_waiter_take(&second,&old,18,102,output,2)==EACCDN && output[0]==-7);
        ksched_waiter_detach(&first); ksched_waiter_detach(&first);
        stale_waiter=second;
        ksched_waiter_retire(&old,18,102); ksched_waiter_retire(&old,18,102);
        CHECK(!ksched_waiter_context(&second) && ksched_waiter_take(&second,&old,18,102,output,2)==EINVAL);
        ksched_waiter_detach(&second);
        ksched_admission_release(&a);
        CHECK(ksched_admission_create(&old,1,&a)==E_OK);
        CHECK(ksched_waiter_attach(&a,&old,17,103,1,&first)==E_OK && first.identity>stale_waiter.identity);
        ksched_waiter_detach(&stale_waiter); /* stale handle cannot detach replacement */
        CHECK(ksched_waiter_context(&first));
        ksched_waiter_retire(&old,17,103); /* retire just this TID, preserve unconsumed result */
        CHECK(!ksched_admission_abandoned(&a) && !ksched_waiter_context(&first));
        CHECK(ksched_waiter_attach(&a,&old,19,104,1,&second)==E_OK);
        CHECK(ksched_admission_complete(&a,0,53) &&
              ksched_waiter_take(&second,&old,19,104,output,1)==E_OK && output[0]==53);
        ksched_waiter_detach(&first); ksched_waiter_detach(&second); ksched_admission_release(&a);
        output[0]=-7;
        CHECK(!result_waiters && !admissions && old.refs==1 && replacement.refs==1 && allocated==freed && controller.references==1);
    }
    {
        KSCHED_WAITER wait={0};
        CHECK(ksched_admission_create(&old,1,&a)==E_OK && ksched_waiter_attach(&a,&old,20,105,1,&wait)==E_OK);
        old.live=FALSE; old.pd=NULL; ksched_admission_owner_exited(&old);
        CHECK(ksched_waiter_ready(ksched_waiter_context(&wait)) &&
              ksched_waiter_take(&wait,&old,20,105,output,1)==EACCDN && output[0]==-7);
        ksched_waiter_retire(&old,20,105); ksched_waiter_detach(&wait); ksched_admission_release(&a);
        old.live=TRUE; old.pd=&recycled_pd;
        CHECK(ksched_admission_create(&old,1,&a)==E_OK);
        last_waiter=~0ULL;
        CHECK(ksched_waiter_attach(&a,&old,20,106,1,&wait)==ERANGE && !wait.identity);
        ksched_admission_release(&a);
        CHECK(!result_waiters && !admissions && old.refs==1 && allocated==freed && controller.references==1);
    }
    puts("TID-bound result attachments: independent waits, one consumer, detach/rebind, quiescent retirement, ABA and rollback: PASS (production code)");
    last_admission=~0ULL;
    CHECK(ksched_admission_create(&old,1,&a)==ERANGE && !a.identity && old.refs==1 && allocated==freed);
    puts("Controller/admission lifetime: identities, partial completion, one consumer, abandonment, PD reuse and allocation rollback: PASS (production code)");
    return 0;
}
