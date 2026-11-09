/* Production receipt + real CUIS ring paths; canonical PROCESS/scheduler modeled.
 * Explicitly not native THREAD-fault or generic PROCESS ownership evidence. */
#include "emutos.h"
#include "../../bdos/process_exec_internal.h"
#define RECEIPT_RUNTIME_FIXTURE 1
static BOOL receipt_fixture_active;
static LONG receipt_fixture_import(KPROC *,ULONG,BOOL,PROCESS_OBSERVER **);
static BOOL receipt_fixture_ready(void *);
static void receipt_fixture_release(PROCESS_OBSERVER **);
#define main legacy_session_checks
#include "session.c"
#undef main
static struct {
    PROCESS_COMPLETION completion;
    ULONG leases;
    BOOL open,committed;
} receipt_children[512];
static BOOL receipt_close_error;
static ULONG receipt_next,receipt_import_count,receipt_release_count;
LONG proc_exec_handle_prepare(KPROC *owner,char *path,char *tail,char *environment,char *directory,ULONG policy)
{
    ULONG i=receipt_next++;
    (void)path;(void)tail;(void)environment;(void)directory;
    if (owner!=(KPROC *)run || !input_irqs || !cleanup_depth || i>=512 || policy!=PROCESS_EXEC_ASYNC) __builtin_abort();
    receipt_children[i].open=TRUE;
    receipt_children[i].completion.state=PROCESS_UNSTARTED;
    receipt_children[i].completion.number_low=i+1;
    receipt_children[i].completion.pid=(LONG)i+1000;
    return (LONG)i+100;
}
LONG proc_exec_handle_commit(KPROC *owner,ULONG handle)
{
    ULONG i=handle-100,j,t;
    BOOL bound=FALSE;
    if (owner!=(KPROC *)run || input_irqs || !cleanup_depth || i>=receipt_next ||
        !receipt_children[i].open || receipt_children[i].committed) __builtin_abort();
    for (t=0;t<RECEIPT_TABLES;t++) for (j=0;j<RECEIPT_SLOTS;j++) {
        CUIS_RECEIPT_SLOT *slot=&receipt_tables[t].slots[j];
        if (slot->state==CUIS_RECEIPT_BOUND && slot->tag==handle) bound=TRUE;
    }
    if (!bound) __builtin_abort();
    receipt_children[i].committed=TRUE;receipt_children[i].completion.state=PROCESS_RUNNING;
    return receipt_children[i].completion.pid;
}
LONG proc_exec_handle_close(KPROC *owner,ULONG handle)
{
    ULONG i=handle-100;
    if (owner!=(KPROC *)run || !input_irqs || i>=receipt_next) __builtin_abort();
    if (!receipt_children[i].open) return EIHNDL;
    receipt_children[i].open=FALSE; return receipt_close_error ? EACCDN:E_OK;
}
static LONG receipt_fixture_import(KPROC *owner,ULONG handle,BOOL prepared,PROCESS_OBSERVER **out)
{
    ULONG i=handle-100;
    if (owner!=(KPROC *)run || !input_irqs || !cleanup_depth || *out) __builtin_abort();
    if (i>=receipt_next || !receipt_children[i].open) return EIHNDL;
    if (prepared && receipt_children[i].committed) return EACCDN;
    *out=(PROCESS_OBSERVER *)&receipt_children[i]; receipt_children[i].leases++; receipt_import_count++;
    return E_OK;
}
static BOOL receipt_fixture_ready(void *observer)
{
    return ((typeof(receipt_children[0]) *)observer)->completion.state>=PROCESS_COMPLETED;
}
static void receipt_fixture_release(PROCESS_OBSERVER **observer)
{
    typeof(receipt_children[0]) *child=(void *)*observer;
    if (!input_irqs || !cleanup_depth || !child->leases) __builtin_abort();
    child->leases--;receipt_release_count++;*observer=NULL;
}
BOOL process_observer_snapshot(const PROCESS_OBSERVER *observer,PROCESS_COMPLETION *out)
{
    *out=((const typeof(receipt_children[0]) *)observer)->completion;return TRUE;
}
LONG kproc_copy_to_user_checked(VADDR address,const void *data,ULONG bytes)
{
    return kproc_copy_to_user(address,data,bytes) ? E_OK:EIMBA;
}
static void complete_receipt_child(CUIS_RECEIPT_TEST_RECORD *record,ULONG order)
{
    PROCESS_COMPLETION *c=&receipt_children[record->tag-100].completion;
    c->state=PROCESS_COMPLETED;c->kind=PROCESS_COMPLETION_NORMAL;c->status=0xffff;c->order_low=order;
}
static int receipt_runtime_checks(void)
{
    CUIS_RECEIPT_TEST_RECORD a,b,old,query,quota[256];
    LONG quota_sources[16],extra;
    LONG source,destination,replacement;
    ULONG before,refs=process_references,i,j;
    char tail[]={0,0},environment[]={0,0};
    stream_heap_mode=TRUE;
    run=&first; source=cuis_service(CUIS_SESSION_START,CUIS_SESSION_NEW);CHECK(source>=0);
    run=&second;destination=cuis_service(CUIS_SESSION_START,CUIS_SESSION_NEW);CHECK(destination>=0);
    run=&first;receipt_fixture_active=TRUE;
    CHECK(cuis_receipt_test_launch(source,destination,0,"child",tail,environment,&a)>0);
    CHECK(cuis_receipt_test_launch(source,destination,1,"child",tail,environment,&b)>0);
    CHECK(a.state==CUIS_RECEIPT_BOUND && a.tag!=b.tag && receipt_active==2);
    memset(stream_input,0x41,16);a.packet=0x10000;a.bytes=16;
    CHECK(cuis_receipt_test_prepare(&a)==E_OK);
    memset(stream_input,0x42,16);b.packet=0x10000;b.bytes=16;
    CHECK(cuis_receipt_test_prepare(&b)==E_OK);
    complete_receipt_child(&a,10);complete_receipt_child(&b,11);
    /* Publisher A stalled. Publisher B must not pass its canonical ordinal. */
    CHECK(cuis_receipt_test_commit(&b)==EAGAIN && !find_handle(destination)->stream_bytes);
    CHECK(cuis_receipt_test_commit(&a)==16);
    run=&second;CHECK(cuis_stream_service(CUIS_STREAM_READ,destination,0x20000,16,0,0)==16);
    CHECK(stream_output[0]==0x41);run=&first;
    /* THREAD loss is modeled by discarding local success, then sibling QUERY.
     * Receiver has drained; the endpoint-owned receipt remains authoritative. */
    query=a;query.packet=0;query.bytes=0;
    CHECK(cuis_receipt_test_query(&query)==E_OK && query.state==CUIS_RECEIPT_COMMITTED);
    before=find_handle(destination)->stream_bytes;
    CHECK(cuis_receipt_test_commit(&a)==16 && find_handle(destination)->stream_bytes==before);
    CHECK(cuis_receipt_test_commit(&b)==16);
    run=&second;CHECK(cuis_stream_service(CUIS_STREAM_READ,destination,0x20000,16,0,0)==16);
    CHECK(stream_output[0]==0x42);run=&first;
    CHECK(cuis_receipt_test_reclaim(&a)==EACCDN);
    CHECK(cuis_receipt_test_ack(&a)==E_OK && cuis_receipt_test_ack(&a)==E_OK);
    CHECK(proc_exec_handle_close((KPROC *)run,a.tag)==E_OK);
    CHECK(cuis_receipt_test_commit(&a)==16 && !find_handle(destination)->stream_bytes);
    CHECK(cuis_receipt_test_reclaim(&a)==E_OK && cuis_receipt_test_reclaim(&a)==E_OK);
    old=a;
    CHECK(cuis_receipt_test_launch(source,destination,0,"child",tail,environment,&a)>0 && a.attempt_low>old.attempt_low);
    CHECK(cuis_receipt_test_commit(&old)==EIHNDL && cuis_receipt_test_ack(&old)==EIHNDL);
    CHECK(cuis_receipt_test_ack(&b)==E_OK && proc_exec_handle_close((KPROC *)run,b.tag)==E_OK &&
          cuis_receipt_test_reclaim(&b)==E_OK);
    /* Full destination cannot create a false COMMITTED outcome. */
    a.packet=0x10000;a.bytes=16;CHECK(cuis_receipt_test_prepare(&a)==E_OK);complete_receipt_child(&a,12);
    find_handle(destination)->stream_bytes=CUIS_STREAM_BYTES;
    CHECK(cuis_receipt_test_commit(&a)==EAGAIN);query=a;query.packet=0;
    CHECK(cuis_receipt_test_query(&query)==E_OK && query.state==CUIS_RECEIPT_PREPARED);
    find_handle(destination)->stream_bytes=0;
    run=&second;CHECK(cuis_service(CUIS_SESSION_END,destination)==E_OK);
    replacement=cuis_service(CUIS_SESSION_START,CUIS_SESSION_NEW);CHECK(replacement>destination);run=&first;
    CHECK(cuis_receipt_test_commit(&a)==EACCDN && !find_handle(replacement)->stream_bytes);
    CHECK(proc_exec_handle_close((KPROC *)run,a.tag)==E_OK && cuis_receipt_test_reclaim(&a)==E_OK);
    /* Fixed slot admission, immutable staged bytes, copy failure and pending
     * cancellation cannot fabricate COMMITTED or silently evict an attempt. */
    CHECK(cuis_receipt_test_launch(source,replacement,0,"child",tail,environment,&a)>0);
    CHECK(cuis_receipt_test_launch(source,replacement,0,"child",tail,environment,&query)==EAGAIN);
    a.packet=0x10000;a.bytes=16;memset(stream_input,0x43,16);
    fail_copy=TRUE;CHECK(cuis_receipt_test_prepare(&a)==EIMBA);fail_copy=FALSE;
    query=a;query.packet=0;
    CHECK(cuis_receipt_test_query(&query)==E_OK && query.state==CUIS_RECEIPT_BOUND);
    CHECK(cuis_receipt_test_prepare(&a)==E_OK);
    memset(stream_input,0x44,16);CHECK(cuis_receipt_test_prepare(&a)==EACCDN);
    complete_receipt_child(&a,13);cancel_pending=TRUE;
    CHECK(cuis_receipt_test_commit(&a)==EACCDN && !find_handle(replacement)->stream_bytes);
    cancel_pending=FALSE;CHECK(cuis_receipt_test_ack(&a)==EIHNDL);
    CHECK(cuis_receipt_test_commit(&a)==16);
    query=a;query.packet=0x20000;query.bytes=16;fail_copy=TRUE;
    CHECK(cuis_receipt_test_query(&query)==EIMBA);fail_copy=FALSE;
    query=a;query.packet=0;CHECK(cuis_receipt_test_query(&query)==E_OK && query.state==CUIS_RECEIPT_COMMITTED);
    CHECK(cuis_receipt_test_ack(&a)==E_OK);receipt_close_error=TRUE;
    CHECK(proc_exec_handle_close((KPROC *)run,a.tag)==EACCDN);receipt_close_error=FALSE;
    before=find_handle(replacement)->stream_bytes;
    CHECK(cuis_receipt_test_commit(&a)==16 && find_handle(replacement)->stream_bytes==before &&
          proc_exec_handle_close((KPROC *)run,a.tag)==EIHNDL && cuis_receipt_test_reclaim(&a)==E_OK);
    CHECK(!receipt_active && receipt_import_count==receipt_release_count);
    /* Exhaust actual fixed tables and all 256 active slots. Empty generation
     * fences remain charged; no allocator pressure may evict them. */
    quota_sources[0]=source;
    for(i=0;i<16;i++) {
        if(i) { quota_sources[i]=cuis_service(CUIS_SESSION_START,CUIS_SESSION_NEW);CHECK(quota_sources[i]>=0); }
        for(j=0;j<16;j++) CHECK(cuis_receipt_test_launch(quota_sources[i],replacement,j,"child",tail,environment,&quota[16*i+j])>0);
    }
    CHECK(receipt_active==256);
    extra=cuis_service(CUIS_SESSION_START,CUIS_SESSION_NEW);CHECK(extra>=0);
    CHECK(cuis_receipt_test_launch(extra,replacement,0,"child",tail,environment,&query)==EAGAIN && receipt_active==256);
    for(i=0;i<256;i++) CHECK(proc_exec_handle_close((KPROC *)run,quota[i].tag)==E_OK && cuis_receipt_test_reclaim(&quota[i])==E_OK);
    CHECK(!receipt_active && cuis_receipt_test_launch(extra,replacement,0,"child",tail,environment,&query)==EAGAIN);
    CHECK(cuis_service(CUIS_SESSION_END,extra)==E_OK);
    for(i=1;i<16;i++)CHECK(cuis_service(CUIS_SESSION_END,quota_sources[i])==E_OK);

    for(i=0;i<receipt_next;i++)CHECK(!receipt_children[i].open && !receipt_children[i].leases);
    CHECK(cuis_service(CUIS_SESSION_END,source)==E_OK);run=&second;
    CHECK(cuis_service(CUIS_SESSION_END,replacement)==E_OK);run=&first;
    CHECK(process_references==refs && !clients && !cleanup_depth && input_irqs && !stream_heap_bad && allocations==frees);
    stream_heap_mode=FALSE;
    return 0;
}
int main(void)
{
    int result=legacy_session_checks();
    if(result)return result;
    return receipt_runtime_checks();
}
