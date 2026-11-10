/* Production KTHREAD joins; PROCESS/ASPACE allocation and backend are models. */
#include "emutos.h"
#undef CONF_WITH_PTHJOIN
#define CONF_WITH_PTHJOIN 1
#include "../bdos/kthread.c"
#include <assert.h>
#include <stdlib.h>

struct kproc { unsigned refs; BOOL live; KTHREAD *threads; KASPACE *space; };
struct kaspace { unsigned refs; };
static unsigned allocations, frees, reclaims, fail_after;
void *kalloc(ULONG size)
{
    void *p;
    if (fail_after && !--fail_after) return NULL;
    p=malloc(size); if (p) allocations++; return p;
}
void kfree(void *p) { if (p) { frees++; free(p); } }
void panic(const char *fmt, ...) { (void)fmt; abort(); }
KPROC *kproc_retain(PD *pd) { KPROC *p=(KPROC *)pd; if (p && p->live) { p->refs++; return p; } return NULL; }
void kproc_release(KPROC *p) { if (p) { assert(p->refs); p->refs--; } }
KASPACE *kproc_active_space(const KPROC *p) { return p->space; }
BOOL kproc_thread_start_allowed(const KPROC *p) { return p->live; }
KASPACE *kaspace_share(KASPACE *p) { p->refs++; return p; }
void kaspace_put(KASPACE *p) { assert(p->refs); p->refs--; }
void kaspace_lease_release(KASPACE_LEASE **p) { assert(!*p); }
BOOL kproc_thread_attach_space(KPROC *p, KASPACE *space, KTHREAD *t,
    KPROC **owner, KASPACE **retained, KTHREAD **next, BOOL identity_only)
{
    (void)identity_only;
    if (!p->live) return FALSE;
    p->refs++; *owner=p; *retained=kaspace_share(space);
    *next=p->threads; p->threads=t; return TRUE;
}
BOOL kproc_thread_detach(KPROC *p, KTHREAD *t, KTHREAD *next)
{
    KTHREAD **link;
    for (link=&p->threads;*link;link=kthread_next_link(*link))
        if (*link==t) { *link=next; return FALSE; }
    abort();
}
BOOL kproc_finish_thread_handoff(KPROC *p, KTHREAD *t)
{ kproc_thread_detach(p,t,*kthread_next_link(t)); return TRUE; }
void proc_dispose_process(KPROC *p) { (void)p; abort(); }
static void reclaim(void *p) { assert(p); reclaims++; }
static KTHREAD *start(KPROC *p)
{
    KTHREAD *t=kthread_prepare((PD *)p,p->space);
    assert(t && kthread_set_execution(t,t,reclaim) && kthread_start(t)); return t;
}
static void finish(KTHREAD *t, UWORD status)
{
    assert(kthread_finish_exit(t,status));
    assert(kthread_reap(t));
}
int main(void)
{
    KASPACE space={1};
    KPROC p={1,TRUE,NULL,&space}, other={1,TRUE,NULL,&space};
    KTHREAD *a,*b,*target,*foreign;
    ULONG tid, previous;
    UWORD status;
    unsigned before;
    a=start(&p); b=start(&p); target=start(&p); foreign=start(&other);
    tid=kthread_tid(target);
    assert(kthread_join_begin(a,kthread_tid(a))==EDEADLK);
    assert(kthread_join_begin(a,kthread_tid(foreign))==ESRCH);
    assert(kthread_join_begin(a,tid)==0 && !kthread_join_ready(a));
    assert(kthread_join_begin(b,tid)==EACCDN);
    before=reclaims;
    finish(target,65535);
    assert(reclaims==before+1 && kthread_join_ready(a));
    assert(kthread_join_status(a,&status) && status==65535);
    kthread_join_cancel(a); /* models failed copyout/interrupted waiter */
    assert(kthread_join_begin(b,tid)==0);
    assert(kthread_join_status(b,&status) && status==65535);
    kthread_join_consume(b);
    assert(kthread_join_begin(a,tid)==ESRCH);
    assert(kthread_join_begin(a,kthread_tid(b))==0);
    assert(kthread_join_begin(b,kthread_tid(a))==EDEADLK);
    kthread_join_cancel(a);
    target=start(&p); tid=kthread_tid(target);
    assert(kthread_join_begin(a,tid)==0);
    assert(kthread_stop(a) && kthread_reap(a)); /* safe cancellation drops claim */
    assert(kthread_join_begin(b,tid)==0);
    finish(target,41);
    assert(kthread_join_status(b,&status) && status==41);
    kthread_join_consume(b);
    finish(b,17); finish(foreign,23);
    assert(!p.threads && !other.threads && p.refs==1 && other.refs==1 && space.refs==1);
    /* Completed status retains neither mappings nor live process resources. */
    assert(allocations>frees);
    kthread_forget_completions(&p); kthread_forget_completions(&other);
    assert(allocations==frees);
    before=allocations-frees; previous=last_tid;
    fail_after=2;
    assert(!kthread_prepare((PD *)&p,&space));
    assert(last_tid==previous && allocations-frees==before && p.refs==1 && space.refs==1);
    target=start(&p); tid=kthread_tid(target); assert(tid==previous+1);
    assert(kthread_finish_exit(target,9));
    kthread_forget_completions(&p); /* final PROCESS retires before root reap */
    p.live=FALSE;
    assert(kthread_reap(target));
    assert(allocations==frees && p.refs==1 && space.refs==1);
    return 0;
}
