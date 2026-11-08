/* Production owned-rights backend; loader and canonical identity are modeled.
 * Actual loader/admission and KPROC completion have independent native tests. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "../bdos/process_exec.c"
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"exec handles:%d: %s\n",__LINE__,#c); abort(); } } while (0)
struct identity {
    struct process_exec_handle *handles;
    PD *pd;
    unsigned refs;
    BOOL resources;
    PROCESS_COMPLETION completion;
};
static PD first_pd, second_pd;
static struct identity first, second, children[16];
static unsigned loads, allocations, frees, cancellations, publications;
static LONG load_error;
static BOOL fail_allocate;
PD *run=&first_pd;
struct process_exec_handle **kproc_exec_handles(KPROC *p)
{ return p ? &((struct identity *)p)->handles : NULL; }
PD *kproc_live_pd(const KPROC *p) { return p ? ((const struct identity *)p)->pd : NULL; }
void *kalloc(ULONG bytes) { void *p; if(fail_allocate)return NULL; p=calloc(1,bytes); if(p)allocations++; return p; }
void kfree(void *p) { CHECK(p); frees++; free(p); }
KPROC *kproc_retain_identity(KPROC *p)
{ struct identity *i=(struct identity *)p; if(i->refs==~0U)return NULL; CHECK(i->refs); i->refs++; return p; }
void process_observer_release(PROCESS_OBSERVER **p)
{ struct identity *i; if(!p||!*p)return; i=(struct identity *)*p; CHECK(i->refs); i->refs--; *p=NULL; }
BOOL process_observer_snapshot(const PROCESS_OBSERVER *p, PROCESS_COMPLETION *out)
{ CHECK(p&&out); *out=((const struct identity *)p)->completion; return TRUE; }
long proc_exec_prepare(char *path,char *tail,char *env,char *cwd,KPROC **out,void **prepared)
{
    struct identity *child;
    CHECK(!*out && !*prepared && path && tail && env && cwd);
    if(load_error)return load_error;
    CHECK(loads<16); child=&children[loads++];
    child->refs=2; child->resources=TRUE;
    child->completion.state=PROCESS_UNSTARTED; child->completion.pid=42;
    *out=(KPROC *)child; *prepared=child; return E_OK;
}
LONG ksched_process_publish(void **slot)
{
    struct identity *child=*slot;
    CHECK(child && child->refs>=2 && child->resources);
    *slot=NULL; publications++; child->completion.state=PROCESS_RUNNING; return child->completion.pid;
}
void ksched_process_cancel(void **slot)
{
    struct identity *child=*slot;
    CHECK(child && child->refs>=2 && child->resources);
    *slot=NULL; child->resources=FALSE; child->completion.state=PROCESS_ABANDONED;
    child->refs--; cancellations++;
}
int main(void)
{
    KPROC *owner=(KPROC *)&first, *foreign=(KPROC *)&second;
    PROCESS_OBSERVER *lease=NULL;
    PROCESS_COMPLETION result;
    LONG one,two,three;
    first.pd=&first_pd; second.pd=&second_pd;
    CHECK(proc_exec_handle_prepare(foreign,"p","t","e","d")==EACCDN && !loads);
    fail_allocate=TRUE;
    CHECK(proc_exec_handle_prepare(owner,"p","t","e","d")==ENSMEM && !loads);
    fail_allocate=FALSE; load_error=EPTHNF;
    CHECK(proc_exec_handle_prepare(owner,"p","t","e","d")==EPTHNF && !first.handles && allocations==frees);
    load_error=0;
    one=proc_exec_handle_prepare(owner,"p","t","e","d"); CHECK(one>0 && !publications);
    CHECK(proc_exec_handle_snapshot(owner,one,&result)==E_OK && result.state==PROCESS_UNSTARTED);
    CHECK(proc_exec_retain_observer(owner,one,TRUE,&lease)==E_OK && children[0].refs==3);
    CHECK(proc_exec_retain_observer(owner,one,TRUE,&lease)==EINVAL);
    CHECK(proc_exec_handle_close(owner,one)==E_OK && cancellations==1 && !children[0].resources);
    CHECK(proc_exec_handle_close(owner,one)==EIHNDL && children[0].refs==1);
    CHECK(process_observer_snapshot(lease,&result) && result.state==PROCESS_ABANDONED);
    process_observer_release(&lease); CHECK(!children[0].refs);
    two=proc_exec_handle_prepare(owner,"p","t","e","d"); CHECK(two>one);
    CHECK(proc_exec_handle_commit(owner,two)==42 && publications==1);
    CHECK(proc_exec_handle_commit(owner,two)==EACCDN && publications==1);
    CHECK(proc_exec_retain_observer(owner,two,TRUE,&lease)==EACCDN && !lease);
    /* Complete and retire the modeled execution; no public Pwait call exists
     * anywhere in this production rights backend. */
    children[1].resources=FALSE; children[1].refs--;
    children[1].completion.state=PROCESS_COMPLETED;
    children[1].completion.kind=PROCESS_COMPLETION_NORMAL;
    children[1].completion.status=0xffff; children[1].completion.order_low=1;
    CHECK(proc_exec_retain_observer(owner,two,FALSE,&lease)==E_OK);
    CHECK(proc_exec_handle_snapshot(owner,two,&result)==E_OK && result.status==0xffff);
    CHECK(proc_exec_handle_snapshot(owner,two,&result)==E_OK && result.order_low==1);
    three=proc_exec_handle_prepare(owner,"p","t","e","d"); CHECK(three>two);
    proc_exec_owner_exit(owner);
    CHECK(!first.handles && allocations==frees && cancellations==2 && publications==1);
    CHECK(!children[1].resources && children[1].refs==1 && !children[2].refs);
    CHECK(process_observer_snapshot(lease,&result) && result.status==0xffff);
    process_observer_release(&lease);
    /* A fresh canonical owner at a reused PD cannot use the retired rights. */
    second.pd=&first_pd; first.pd=NULL;
    CHECK(proc_exec_handle_snapshot(foreign,two,&result)==EIHNDL);
    CHECK(proc_exec_handle_snapshot(owner,two,&result)==EACCDN);
    last_handle=0x7fffffffUL; /* Private host cursor only: production never resets. */
    CHECK(proc_exec_handle_prepare(foreign,"p","t","e","d")==EAGAIN && loads==3);
    puts("Production PROCESS rights: private preparation, one commit, close, retained leases, owner retirement and stale handles: PASS");
    return 0;
}
