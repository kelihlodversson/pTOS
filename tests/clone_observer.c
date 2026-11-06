/* Real owned observer record, with modeled PROCESS/completion and registration.
 * Run under each user-ASPACE configuration; this is host execution, not CPU startup. */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "emutos.h"
#include "../bdos/proc_clone.c"
#include <assert.h>
struct kproc { WORD pid; ULONG references; BOOL live, completed; UWORD status; };
struct thread_startup_draft { THREAD_STARTUP_OBSERVER observer; void *context; };
static unsigned allocations, frees, fail_registration;
static BOOL fail_allocate;
static KASPACE *space = (KASPACE *)1;
void *kalloc(ULONG bytes) { void *p; if (fail_allocate) return NULL; p = malloc(bytes); if (p) allocations++; return p; }
void kfree(void *p) { assert(p); frees++; free(p); }
void panic(const char *fmt, ...) { (void)fmt; abort(); }
KPROC *kproc_retain_live(KPROC *p) { if (!p->live) return NULL; p->references++; return p; }
void kproc_release(KPROC *p) { assert(p && p->references > 1); p->references--; }
WORD kproc_ref_pid(const KPROC *p) { return p->pid; }
BOOL kproc_ref_status(const KPROC *p, UWORD *status) { if (!p->completed) return FALSE; *status = p->status; return TRUE; }
KASPACE *kproc_active_space(const KPROC *p) { return p->live ? space : NULL; }
BOOL kproc_clone_target_ready(const KPROC *p, KTHREAD *t) { (void)t; return p->live; }
ULONG kthread_tid(const KTHREAD *thread) { return (ULONG)(uintptr_t)thread + 70; }
LONG thread_startup_observe(THREAD_STARTUP_DRAFT *d, const THREAD_STARTUP_OBSERVER *ops, void *context)
{
    assert(!d->context);
    if (fail_registration == 1) return ENSMEM;
    assert(ops->acquire(context) == E_OK);
    if (fail_registration == 2) { ops->release(context); return EIMBA; }
    d->observer = *ops; d->context = context; return E_OK;
}
static void detach(THREAD_STARTUP_DRAFT *d)
{ void *context = d->context; assert(context); d->context = NULL; d->observer.release(context); }
int main(void)
{
    KPROC source = { 11, 1, TRUE, FALSE, 0 }, target[2] = { {31,1,TRUE,FALSE,0}, {32,1,TRUE,FALSE,0} };
    THREAD_STARTUP_DRAFT draft[2];
    PROC_CLONE_TRANSACTION tx[2];
    PROC_CLONE_OBSERVER *observer[2] = { NULL, NULL }, *sentinel = (PROC_CLONE_OBSERVER *)123;
    PROC_CLONE_COMPLETION result = { 9, 8, 7 };
    unsigned i;
    memset(draft,0,sizeof(draft)); memset(tx,0,sizeof(tx));
    for (i=0; i<2; i++) { tx[i].source=&source; tx[i].process=&target[i]; tx[i].space=space;
        tx[i].startup=&draft[i]; tx[i].policy=PROC_CLONE_NEW_PROCESS; tx[i].thread=(KTHREAD *)(uintptr_t)(i+1); }
    assert(proc_clone_observe_async(&tx[0],&sentinel) == EINVAL && sentinel == (PROC_CLONE_OBSERVER *)123 && !allocations);
    fail_allocate=TRUE;
    assert(proc_clone_observe_async(&tx[0],&observer[0]) == ENSMEM && !observer[0]); fail_allocate=FALSE;
    target[0].live=FALSE;
    assert(proc_clone_observe_async(&tx[0],&observer[0]) == EACCDN && !observer[0]); target[0].live=TRUE;
    for (fail_registration=1; fail_registration<=2; fail_registration++) {
        assert(proc_clone_observe_async(&tx[0],&observer[0]) == (fail_registration==1 ? ENSMEM : EIMBA));
        assert(!observer[0] && !tx[0].observer && target[0].references==1 && allocations==frees);
    }
    fail_registration=0;
    for (i=0; i<2; i++) {
        assert(proc_clone_observe_async(&tx[i],&observer[i]) == E_OK && target[i].references==2);
        assert(proc_clone_observer_state(observer[i]) == PROC_CLONE_OBSERVER_PREPARED);
        observer[i]->state=PROC_CLONE_OBSERVER_PUBLISHED; /* backend's no-fail publication */
    }
    target[0].completed=TRUE; target[0].status=0x1234; target[0].live=FALSE;
    assert(proc_clone_observer_result(observer[0],&result) && result.pid==31 && result.tid==71 && result.status==0x1234);
    result.pid=9; result.tid=8; result.status=7;
    assert(!proc_clone_observer_result(observer[1],&result) && result.pid==9 && result.tid==8 && result.status==7);
    /* Creator drops its reference first; the registration still owns the record. */
    proc_clone_observer_release(&observer[0]); proc_clone_observer_release(&observer[0]);
    assert(target[0].references==2); detach(&draft[0]); assert(target[0].references==1);
    target[1].completed=TRUE; target[1].live=FALSE; target[1].status=0xffff;
    detach(&draft[1]); /* Root releases first; caller can observe after wait/reap. */
    assert(proc_clone_observer_state(observer[1])==PROC_CLONE_OBSERVER_REAPED && target[1].references==2);
    assert(proc_clone_observer_result(observer[1],&result) && result.pid==32 && result.tid==72 && result.status==0xffff);
    proc_clone_observer_release(&observer[1]); proc_clone_observer_release(&observer[1]);
    assert(target[1].references==1 && allocations==frees);
    /* Cancellation cannot turn a later unrelated completion into a child event. */
    tx[0].observer=NULL; target[0].live=TRUE; target[0].completed=FALSE;
    assert(proc_clone_observe_async(&tx[0],&observer[0])==E_OK); detach(&draft[0]);
    target[0].completed=TRUE;
    assert(proc_clone_observer_state(observer[0])==PROC_CLONE_OBSERVER_CANCELLED && !proc_clone_observer_result(observer[0],&result));
    proc_clone_observer_release(&observer[0]);
    assert(target[0].references==1 && allocations==frees);
    puts("Owned clone observers: rollback, independent completion, creator/root release orders and cancellation: PASS");
    return 0;
}
