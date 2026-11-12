/* Real transaction, KTHREAD and KASPACE; counted PROCESS/resource model. */
#include "emutos.h"
#include "biosext.h"
#include "kproc.h"
#include "kaspace.h"
#include "kthread.h"
#include "proc_clone.h"
#include "gemerror.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct kproc {
    WORD pid;
    unsigned refs;
    BOOL live, pending, model, started;
    KASPACE *space;
    KTHREAD *threads;
};
struct aspace { int marker; };
static unsigned allocations, frees, destroys, resources, fail_alloc_after, fail_retain_after;

void *kalloc(ULONG size)
{
    void *p;
    if (fail_alloc_after && !--fail_alloc_after) return NULL;
    p = malloc(size);
    if (p) allocations++;
    return p;
}
void kfree(void *p) { if (p) { frees++; free(p); } }
void panic(const char *fmt, ...) { (void)fmt; abort(); }
#if CONF_WITH_MMAP
void fileback_put(FILE_BACKING *backing) { (void)backing; abort(); }
#endif
void aspace_destroy(ASPACE *as) { assert(as && as->marker == 42); destroys++; }
BOOL aspace_translate(const ASPACE *as, VADDR va, PADDR *physical, UWORD *prot)
{ (void)as; (void)va; (void)physical; (void)prot; return FALSE; }
BOOL aspace_fault_in(ASPACE *as, VADDR va, UWORD prot)
{ (void)as; (void)va; (void)prot; return TRUE; }

KPROC *kproc_retain_live(KPROC *p)
{
    if (!p || !p->live || p->pending || (fail_retain_after && !--fail_retain_after)) return NULL;
    p->refs++; return p;
}
/* PD mechanics are modeled; the real loader/KPROC path is native-tested. */
KPROC *kproc_retain(PD *pd) { return kproc_retain_live((KPROC *)pd); }
PD *kproc_live_pd(const KPROC *p) { return p && p->live && !p->pending ? (PD *)p : NULL; }
BOOL kproc_unlaunched(PD *pd) { return pd && !((KPROC *)pd)->started; }
void kproc_release(KPROC *p) { if (p) { assert(p->refs); p->refs--; } }
WORD kproc_ref_pid(const KPROC *p) { return p->pid; }
KASPACE *kproc_active_space(const KPROC *p) { return p && p->live && !p->pending ? p->space : NULL; }
BOOL kproc_thread_start_allowed(const KPROC *p) { return p->live && !p->pending && !p->model; }
BOOL kproc_thread_attach_space(KPROC *p, KASPACE *space, KTHREAD *t,
    KPROC **process, KASPACE **retained, KTHREAD **next, BOOL identity_only)
{
    (void)identity_only;
    if (!p || !space || (p->space && p->space != space) || !kproc_retain_live(p)) return FALSE;
    *retained = kaspace_share(space);
    if (!*retained) { kproc_release(p); return FALSE; }
    *process = p; *next = p->threads; p->threads = t; return TRUE;
}
BOOL kproc_thread_detach(KPROC *p, KTHREAD *t, KTHREAD *next)
{
    KTHREAD **link;
    for (link = &p->threads; *link; link = kthread_next_link(*link))
        if (*link == t) { *link = next; return !p->threads && p->pending; }
    abort();
}
BOOL kproc_clone_target_ready(const KPROC *p, KTHREAD *t)
{ return p && p->live && !p->pending && !p->space && !p->model && p->threads == t; }
BOOL kproc_clone_adopt_space(KPROC *p, KTHREAD *t, KASPACE *space)
{
    if (!t || !space || !kproc_clone_target_ready(p,t)) return FALSE;
    p->space = space; p->model = TRUE; return TRUE;
}
void proc_dispose_process(KPROC *p)
{
    if (!p->live) return;
    p->pending = TRUE;
    if (p->threads) return;
    p->live = FALSE; resources++; kaspace_put(p->space); p->space = NULL;
}
/* The portable harness models PROCESS ownership; real factory allocation,
 * inheritance and rollback are exercised by the x32 production QEMU test. */
LONG proc_create_clone_process(KPROC *source, UWORD policy, KPROC **result)
{ (void)source; (void)policy; (void)result; return EINVFN; }
static void dummy_reclaim(void *p) { (void)p; abort(); }

static void exercise(BOOL source_first)
{
    ASPACE backend = { 42 };
    KPROC source, target;
    PROC_CLONE_ARGS args;
    PROC_CLONE_TRANSACTION *tx = NULL, *unchanged;
    PROC_CLONE_RESULT a = PROC_CLONE_RESULT_INIT, b = PROC_CLONE_RESULT_INIT, sentinel;
    KTHREAD *survivor;
    KASPACE_HEAP *heap;
    unsigned i, before;

    memset(&source,0,sizeof(source)); memset(&target,0,sizeof(target));
    source.pid = 11; target.pid = 22; source.live = target.live = TRUE;
    source.refs = target.refs = 1;
    source.space = kaspace_adopt(&backend); assert(source.space);
    heap = kaspace_heap(source.space);
    heap->blocks = kalloc(sizeof(*heap->blocks)); assert(heap->blocks);
    heap->count = heap->capacity = 1; heap->blocks[0].va = 0x1000; heap->blocks[0].pages = 1;
    memset(&args,0,sizeof(args));
    args.source = &source; args.prepared_process = NULL;
    args.process_policy = PROC_CLONE_SAME_PROCESS; args.vm_policy = PROC_CLONE_SHARE_VM;
    for (i = 1; i <= PROC_CLONE_RESOURCE_MASK + 1; i++) {
        before = allocations - frees; args.resources = i;
        assert(proc_clone(&args,&tx) == EINVAL && !tx);
        args.process_policy = PROC_CLONE_PREPARED_PROCESS; args.prepared_process = &target;
        assert(proc_clone(&args,&tx) == EINVAL && !tx);
        assert(source.refs == 1 && target.refs == 1 && allocations - frees == before);
        args.process_policy = PROC_CLONE_SAME_PROCESS; args.prepared_process = NULL;
    }
    args.resources = 0;
    for (i = 1; i <= 2; i++) {
        before = allocations - frees; fail_alloc_after = i;
        unchanged = NULL;
        assert(proc_clone(&args,&unchanged) == ENSMEM && !unchanged);
        assert(!source.threads && source.refs == 1 && allocations - frees == before);
    }
    for (i = 1; i <= 3; i++) {
        before = allocations - frees; fail_retain_after = i;
        assert(proc_clone(&args,&tx) != E_OK && !tx);
        assert(!source.threads && source.refs == 1 && allocations - frees == before);
    }
#if CONF_WITH_X86_64_MEMTEST
    for (i = 1; i <= 4; i++) {
        before = allocations - frees; proc_clone_test_fail_after(i);
        assert(proc_clone(&args,&tx) == ENSMEM && !tx);
        assert(!source.threads && source.refs == 1 && allocations - frees == before);
    }
#endif
    before = allocations - frees;
    args.prepared_process = &target;
    assert(proc_clone(&args,&tx) == EINVAL && !tx);
    args.prepared_process = NULL;
    args.process_policy = (PROC_CLONE_PROCESS_POLICY)99;
    assert(proc_clone(&args,&tx) == EINVAL && !tx);
    args.process_policy = PROC_CLONE_SAME_PROCESS;
    args.vm_policy = (PROC_CLONE_VM_POLICY)99;
    assert(proc_clone(&args,&tx) == EINVAL && !tx);
    assert(source.refs == 1 && target.refs == 1 && allocations - frees == before);
    args.vm_policy = PROC_CLONE_PRIVATE_VM;
    assert(proc_clone(&args,&tx) == EINVAL && !tx);
    args.process_policy = PROC_CLONE_PREPARED_PROCESS; args.prepared_process = &target;
    assert(proc_clone(&args,&tx) == EINVFN && !tx);
    args.process_policy = PROC_CLONE_NEW_PROCESS; args.prepared_process = NULL;
    args.resources = PROC_CLONE_RESOURCE_MASK + 1;
    assert(proc_clone(&args,&tx) == EINVAL && !tx);
    args.resources = 0;
    assert(proc_clone(&args,&tx) == EINVFN && !tx); /* private VM remains unsupported */
    args.process_policy = PROC_CLONE_PREPARED_PROCESS; args.prepared_process = &target;
    args.vm_policy = PROC_CLONE_SHARE_VM;
    assert(proc_clone(&args,&tx) == E_OK && !target.space);
    {
        THREAD_STARTUP_ARGS startup;
        memset(&startup,0,sizeof(startup));
        assert(proc_clone_prepare_startup(tx,&startup) == EINVFN && !proc_clone_startup(tx));
    }
    unchanged = tx; before = allocations - frees;
    assert(proc_clone(&args,&tx) == EINVAL && tx == unchanged);
    assert(allocations - frees == before && !target.space);
    proc_clone_abort(&tx); proc_clone_abort(&tx);
    assert(!target.space && !target.threads && source.refs == 1 && target.refs == 1);

    args.process_policy = PROC_CLONE_SAME_PROCESS; args.prepared_process = NULL;
    assert(proc_clone(&args,&tx) == E_OK);
    assert(proc_clone_commit(&tx,&a) == E_OK && !tx);
    memset(&sentinel,0xa5,sizeof(sentinel)); memcpy(&b,&sentinel,sizeof(b));
    assert(proc_clone_commit(&tx,&b) == EINVAL && !memcmp(&b,&sentinel,sizeof(b)));
    proc_clone_abort(&tx);
    memset(&b,0,sizeof(b));
    assert(proc_clone(&args,&tx) == E_OK);
    unchanged = tx; memcpy(&sentinel,&a,sizeof(a)); before = allocations - frees;
    assert(proc_clone_commit(&tx,&a) == EINVAL && tx == unchanged && !memcmp(&a,&sentinel,sizeof(a)));
    assert(allocations - frees == before);
    assert(proc_clone_commit(&tx,&b) == E_OK && !tx);
    assert(a.pid == source.pid && b.pid == a.pid && a.tid != b.tid);
    assert(kthread_aspace(a.thread) == source.space && kthread_aspace(b.thread) == source.space);
    assert(!kthread_set_execution(a.thread,&source,dummy_reclaim) && !kthread_start(a.thread));
    proc_clone_release(source_first ? &a : &b);
    assert(source.live && source.threads && source.space);
    survivor = source_first ? b.thread : a.thread;
    /* The remaining THREAD is explicitly owned through survivor below. */
    memset(&a,0,sizeof(a)); memset(&b,0,sizeof(b));

    args.process_policy = PROC_CLONE_PREPARED_PROCESS; args.prepared_process = &target;
    assert(proc_clone(&args,&tx) == E_OK && !target.space);
    assert(proc_clone_commit(&tx,&a) == E_OK && !tx);
    assert(a.pid == target.pid && a.pid != source.pid && a.tid != kthread_tid(survivor) &&
           kthread_process(survivor) == &source && kthread_aspace(survivor) == target.space && target.space == source.space);
    assert(kthread_process(a.thread) == &target && !kthread_start(a.thread));
    if (source_first) {
        kthread_destroy(survivor);
        proc_dispose_process(&source);
        assert(!source.live && target.live && kaspace_heap_contains(target.space,0x1000));
        assert(kaspace_heap_demand_page(target.space,0x1000,ASPACE_PROT_USER) == 1);
    }
    proc_dispose_process(&target); assert(target.live && target.pending);
    kthread_destroy(a.thread); assert(!target.live && !target.space);
    if (!source_first) {
        assert(source.live && kaspace_heap_contains(source.space,0x1000));
        proc_dispose_process(&source); assert(source.live && source.pending);
        kthread_destroy(survivor);
    }
    assert(source.refs == 1 && target.refs == 1 && !source.threads && !target.threads);
    kproc_release(&source); kproc_release(&target);
    assert(!source.refs && !target.refs);

    /* A terminal-state change between prepare and commit refuses publication.
     * Output and handle remain intact until explicitly aborted. */
    memset(&source,0,sizeof(source)); source.pid = 33; source.live = TRUE; source.refs = 1;
    source.space = kaspace_adopt(&backend); assert(source.space);
    args.source = &source; args.prepared_process = NULL; args.process_policy = PROC_CLONE_SAME_PROCESS;
    assert(proc_clone(&args,&tx) == E_OK);
    memset(&sentinel,0,sizeof(sentinel)); memcpy(&b,&sentinel,sizeof(b));
    proc_dispose_process(&source); assert(source.live && source.pending);
    assert(proc_clone_commit(&tx,&b) == EACCDN && tx && !memcmp(&b,&sentinel,sizeof(b)));
    proc_clone_abort(&tx); assert(!tx && !source.live && source.refs == 1);
    kproc_release(&source);
}
BOOL aspace_private_range_ok(const ASPACE *as, VADDR address, VADDR bytes)
{ (void)as; (void)address; (void)bytes; return TRUE; }
static unsigned loaded_reclaims;
static void loaded_reclaim(void *resource) { assert(resource); loaded_reclaims++; }
static void exercise_loaded(void)
{
    ASPACE backend = { 42 };
    KPROC target;
    KASPACE *space;
    PROC_CLONE_ARGS args = { 0 };
    PROC_CLONE_TRANSACTION *tx = NULL;
    PROC_CLONE_RESULT result = PROC_CLONE_RESULT_INIT;
    KTHREAD *thread;
    unsigned stage, balance, destroyed = destroys;
    LONG error;
    memset(&target, 0, sizeof(target));
    target.live = TRUE; target.pid = 44; target.refs = 1;
    space = kaspace_adopt(&backend); assert(space);
    args.source = &target;
    args.process_policy = PROC_CLONE_LOADED_PROCESS;
    args.vm_policy = PROC_CLONE_PREPARED_VM;
    args.prepared_space = space;
    balance = allocations - frees;
    /* Fail each transaction/THREAD metadata allocation before loader adoption. */
    for (stage = 1; stage < 8; stage++) {
        fail_alloc_after = stage;
        error = proc_clone(&args, &tx);
        fail_alloc_after = 0;
        if (!error) { proc_clone_abort(&tx); break; }
        assert(error == ENSMEM && !tx);
        assert(!target.space && !target.threads && target.refs == 1);
        assert(allocations - frees == balance && destroys == destroyed);
    }
    assert(stage < 8 && !target.space && !target.threads && target.refs == 1);
#if CONF_WITH_X86_64_MEMTEST
    /* Function-entry failure injection must survive non-runnable loader
     * bootstrap; actual loader allocations were faulted independently above. */
    proc_clone_test_fail_after(1);
#endif
    assert(proc_clone(&args, &tx) == E_OK);
    assert(proc_clone_commit(&tx, &result) == E_OK && !tx);
    thread = result.thread;
    assert(thread && result.pid == 44 && result.tid && !result.owned_process);
    assert(!target.space && kthread_aspace(thread) == space && kthread_process(thread) == &target);
    assert(!kthread_is_executing(thread));
    assert(kthread_set_execution(thread, &target, loaded_reclaim));
    assert(!kthread_start(thread)); /* loader has not committed its VM yet */
#if CONF_WITH_X86_64_MEMTEST
    {
        PROC_CLONE_ARGS next = {0};
        PROC_CLONE_TRANSACTION *blocked = NULL;
        next.source = &target; next.process_policy = PROC_CLONE_SAME_PROCESS;
        next.vm_policy = PROC_CLONE_SHARE_VM;
        /* Give the intended factory a live selected root without starting it. */
        target.space = kaspace_share(space);
        assert(proc_clone(&next,&blocked) == ENSMEM && !blocked);
        assert(target.threads == thread && !kthread_is_executing(thread));
        kaspace_put(target.space); target.space = NULL;
    }
#endif
    target.space = kaspace_share(space); /* existing loader adoption boundary */
    assert(kthread_start(thread));
    target.started = TRUE;
    assert(!proc_clone_prepare_loaded_thread((PD *)&target, space));
    assert(target.threads == thread && kthread_is_executing(thread));
    kthread_stop(thread); kthread_destroy(thread);
    assert(loaded_reclaims == 1 && !target.threads && target.refs == 1);
    kaspace_put(target.space); target.space = NULL;
    kaspace_put(space);
    assert(destroys == destroyed + 1);
    assert(allocations == frees);
}

int main(void)
{
    exercise(FALSE); exercise(TRUE);
    assert(allocations == frees && destroys == 4 && resources == 6);
    exercise_loaded();
    return 0;
}

BOOL aspace_release_lazy(ASPACE *as,VADDR va,VADDR bytes) { (void)as;(void)va;(void)bytes;abort();return FALSE; }

void aspace_unborrow(ASPACE *as,VADDR va,VADDR bytes) { (void)as;(void)va;(void)bytes;abort(); }
