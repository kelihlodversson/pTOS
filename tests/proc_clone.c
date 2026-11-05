/* Real transaction, KTHREAD and KASPACE; counted PROCESS/resource model. */
#include "emutos.h"
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
    BOOL live, pending, model;
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
void kproc_release(KPROC *p) { if (p) { assert(p->refs); p->refs--; } }
WORD kproc_ref_pid(const KPROC *p) { return p->pid; }
KASPACE *kproc_active_space(const KPROC *p) { return p && p->live && !p->pending ? p->space : NULL; }
BOOL kproc_thread_start_allowed(const KPROC *p) { return p->live && !p->pending && !p->model; }
BOOL kproc_thread_attach_space(KPROC *p, KASPACE *space, KTHREAD *t,
    KPROC **process, KASPACE **retained, KTHREAD **next)
{
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
    PROC_CLONE_RESULT a, b, sentinel;
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
    args.source = &source; args.prepared_process = NULL;
    args.process_policy = PROC_CLONE_SAME_PROCESS; args.vm_policy = PROC_CLONE_SHARE_VM;
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
    unchanged = tx; before = allocations - frees;
    assert(proc_clone(&args,&tx) == EINVAL && tx == unchanged);
    assert(allocations - frees == before && !target.space);
    proc_clone_abort(&tx); proc_clone_abort(&tx);
    assert(!target.space && !target.threads && source.refs == 1 && target.refs == 1);

    args.process_policy = PROC_CLONE_SAME_PROCESS; args.prepared_process = NULL;
    assert(proc_clone(&args,&tx) == E_OK);
    assert(proc_clone_commit(&tx,&a) == E_OK && !tx);
    memset(&sentinel,0xa5,sizeof(sentinel)); b = sentinel;
    assert(proc_clone_commit(&tx,&b) == EINVAL && !memcmp(&b,&sentinel,sizeof(b)));
    proc_clone_abort(&tx);
    assert(proc_clone(&args,&tx) == E_OK);
    assert(proc_clone_commit(&tx,&b) == E_OK && !tx);
    assert(a.pid == source.pid && b.pid == a.pid && a.tid != b.tid);
    assert(kthread_aspace(a.thread) == source.space && kthread_aspace(b.thread) == source.space);
    assert(!kthread_set_execution(a.thread,&source,dummy_reclaim) && !kthread_start(a.thread));
    kthread_destroy(source_first ? a.thread : b.thread);
    assert(source.live && source.threads && source.space);
    survivor = source_first ? b.thread : a.thread;

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
    memset(&sentinel,0xa5,sizeof(sentinel)); b = sentinel;
    proc_dispose_process(&source); assert(source.live && source.pending);
    assert(proc_clone_commit(&tx,&b) == EACCDN && tx && !memcmp(&b,&sentinel,sizeof(b)));
    proc_clone_abort(&tx); assert(!tx && !source.live && source.refs == 1);
    kproc_release(&source);
}
int main(void)
{
    exercise(FALSE); exercise(TRUE);
    assert(allocations == frees && destroys == 4 && resources == 6);
    return 0;
}
