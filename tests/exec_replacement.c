/* Actual KPROC/KASPACE replacement-admission policy. Scheduler, CPU and the
 * already-live THREAD are modeled; this never commits or executes Pexec200. */
#define main existing_mapping_fixture_main
#include "shared_root_mapping.c"
#undef main
#include "exec_replace.h"
#include "ksched.h"

struct kthread { KPROC *process;KASPACE *space;KTHREAD *next;ULONG tid;BOOL executing; };
static KTHREAD *current_thread;
static UQUAD current_execution=17;
static BOOL cancellation;
static unsigned cleanup_depth;
KPROC *kthread_process(const KTHREAD *t) { return t?t->process:NULL; }
KASPACE *kthread_aspace(const KTHREAD *t) { return t?t->space:NULL; }
KTHREAD **kthread_next_link(KTHREAD *t) { return &t->next; }
ULONG kthread_tid(const KTHREAD *t) { return t?t->tid:0; }
BOOL kthread_is_executing(const KTHREAD *t) { return t && t->executing; }
KTHREAD *ksched_exit_thread(PD *pd) { return current_thread && current_thread->process->pd==pd?current_thread:NULL; }
UQUAD ksched_context_id(void) { return current_execution; }
BOOL ksched_cancellation_pending(void) { return cancellation; }
BOOL ksched_clone_context(KPROC *p,PD **controller)
{
    if (!current_thread || current_thread->process!=p || p->pd!=run) return FALSE;
    *controller=run;return TRUE;
}
void ksched_cleanup_begin(KSCHED_CLEANUP_SCOPE *s)
{ CHECK(!s->active);s->active=TRUE;s->execution=current_execution;s->thread=current_thread;cleanup_depth++; }
void ksched_cleanup_end(KSCHED_CLEANUP_SCOPE *s)
{ CHECK(s->active && cleanup_depth && s->execution==current_execution && s->thread==current_thread);s->active=FALSE;cleanup_depth--; }
/* This fixture exercises admission only. Real image disposal is verified by
 * the native loader selector; it must never be substituted here. */
void proc_dispose_process(KPROC *p) { (void)p;CHECK(0); }
BOOL ksched_wait_ready(BOOL (*ready)(void *),void *context) { return ready(context); }
ASPACE_ROOT aspace_root(const ASPACE *as) { return (ASPACE_ROOT)(uintptr_t)as; }

static KPROC *reentry_source;
static KTHREAD *reentry_thread;
static KASPACE *reentry_selected;
static unsigned reentries;
static void admit_during_mapping_retirement(void)
{
    KPROC_EXEC_REPLACEMENT *receipt=NULL;
    fileback_put_callback=NULL;
    reentries++;
    CHECK(kproc_exec_replacement_prepare(reentry_source,reentry_thread,reentry_selected,&receipt)==E_OK);
    CHECK(receipt && kproc_exec_replacement_validate(receipt)==E_OK);
    kproc_exec_replacement_abort(&receipt);
}
static void test_abort_reentry(KPROC *source,KTHREAD *thread,KASPACE *selected)
{
    ASPACE backend={0};
    KASPACE *third=kaspace_adopt(&backend);
    PD origin_pd={0},foreign_pd={0};
    KPROC origin={0},foreign={0};
    KASPACE_MEMBER *members=NULL;
    LONG mapping;
    unsigned live=allocations-frees;
    ULONG refs=source->references,root_refs=selected->refs;
    origin.pd=&origin_pd;origin.references=1;origin.number=11;origin.pid=33;
    origin.active_space=third;origin.aspace=&backend;
    foreign.pd=&foreign_pd;foreign.references=1;foreign.number=10;foreign.pid=32;
    source->next=&foreign;foreign.next=&origin;
    CHECK(kaspace_member_new(third,origin.number,&origin,&origin.mapping_members)==E_OK);
    origin.allocation_scope=kaspace_member_scope(origin.mapping_members);
    mapping=kproc_mmap(&origin_pd,0,USER_PAGE_SIZE,PROT_READ,MAP_PRIVATE,0,0);
    CHECK(mapping>0 && file.references==1);
    CHECK(kproc_clone_prepare_mapping_owners(&foreign,&origin,third,FALSE,&members)==E_OK);
    kaspace_member_release(&origin.mapping_members);origin.allocation_scope=NULL;
    reentry_source=source;reentry_thread=thread;reentry_selected=selected;
    fileback_put_callback=admit_during_mapping_retirement;
    /* Member release frees its record before final file backing callbacks.
     * The borrowed prepared-member alias is invalid throughout ABORTING. */
    kproc_abort_mapping_owners(&foreign,third,&members);
    CHECK(reentries==1 && !fileback_put_callback && !members &&
          !foreign.prepared_mapping_members && !foreign.mapping_preparation);
    CHECK(!cleanup_depth && !source->exec_replacement_ticket && !file.references &&
          source->references==refs && selected->refs==root_refs && allocations-frees==live);
    source->next=NULL;origin.active_space=NULL;
    kaspace_put(third);
    CHECK(backend.destroyed==1);
    reentry_source=NULL;reentry_thread=NULL;reentry_selected=NULL;
    puts("Replacement admission during actual mapping/fileback abort reentry: PASS");
}

int main(void)
{
    ASPACE old_backend={0},new_backend={0};
    KASPACE *old=kaspace_adopt(&old_backend),*selected=kaspace_adopt(&new_backend);
    KPROC source={0},peer={0};
    KTHREAD caller={0},other={0};
    KPROC_EXEC_REPLACEMENT *replacement=NULL,*second=NULL;
    ULONG roots,identities;
    unsigned baseline,i;
    UQUAD ticket;
    source.pd=&child;source.references=1;source.number=9;source.pid=31;
    source.started=TRUE;source.active_space=old;source.aspace=&old_backend;
    caller.process=&source;caller.space=old;caller.tid=23;caller.executing=TRUE;
    source.threads=&caller;current_thread=&caller;kproc_list=&source;
    roots=old->refs;identities=source.references;baseline=allocations-frees;
    test_abort_reentry(&source,&caller,selected);
    CHECK(allocations-frees==baseline && old->refs==roots && selected->refs==1);
    {
        KPROC *identity=NULL;KASPACE *space=NULL;KTHREAD *next=NULL;
        /* Smallest production owner control for #970: a fresh THREAD cannot
         * attach the selected replacement root to this live PROCESS. No
         * replacement ticket or allocation fault causes this rejection. */
        CHECK(!kproc_thread_attach_space(&source,selected,&other,&identity,&space,&next,TRUE));
        CHECK(!identity && !space && !next && source.threads==&caller &&
              source.active_space==old && old->refs==roots && selected->refs==1);
    }
    /* Neither active nor pending root fields describe a prepared THREAD's
     * association. Test both selected-root exclusivity and source sharing. */
    for (i=0;i<2;i++) {
        PD prepared_pd={0};KPROC prepared={0};KTHREAD pending_thread={0};
        KPROC *identity=NULL;KASPACE *held=NULL;KTHREAD *next=NULL;
        LONG admitted;
        prepared.pd=&prepared_pd;prepared.number=10;prepared.references=1;
        source.next=&prepared;
        CHECK(kproc_thread_attach_space(&prepared,i?old:selected,&pending_thread,&identity,&held,&next,TRUE));
        pending_thread.process=identity;pending_thread.space=held;pending_thread.next=next;
        CHECK(prepared.threads==&pending_thread && !prepared.active_space && !prepared.pending_space);
        admitted=kproc_exec_replacement_prepare(&source,&caller,selected,&replacement);
        kproc_exec_replacement_abort(&replacement);
        (void)kproc_thread_detach(&prepared,&pending_thread,next);
        kaspace_put(held);kproc_release(identity);source.next=NULL;
        CHECK(selected->refs==1 && old->refs==roots && source.references==identities &&
              !cleanup_depth && !source.exec_replacement_ticket && allocations-frees==baseline);
        CHECK(admitted==EACCDN);
    }
    /* Canonical prepared mapping membership precedes even initial THREAD
     * attachment. A shared source also has an ordinary existing membership. */
    CHECK(kaspace_member_new(old,source.number,&source,&source.mapping_members)==E_OK);
    source.allocation_scope=kaspace_member_scope(source.mapping_members);
    for (i=0;i<2;i++) {
        PD prepared_pd={0};KPROC prepared={0};KASPACE_MEMBER *members=NULL;
        KASPACE *construction=i?old:selected;
        LONG admitted;
        prepared.pd=&prepared_pd;prepared.number=10;prepared.references=1;prepared.pid=32;
        source.next=&prepared;
        CHECK(kproc_clone_prepare_mapping_owners(&prepared,&source,construction,!i,&members)==E_OK);
        CHECK(members && prepared.prepared_mapping_members==members &&
              !prepared.active_space && !prepared.pending_space && !prepared.threads);
        admitted=kproc_exec_replacement_prepare(&source,&caller,selected,&replacement);
        kproc_exec_replacement_abort(&replacement);
        kproc_abort_mapping_owners(&prepared,construction,&members);source.next=NULL;
        CHECK(admitted==EACCDN && !replacement && !source.exec_replacement_ticket && !cleanup_depth);
    }
    kaspace_member_release(&source.mapping_members);source.allocation_scope=NULL;
    CHECK(selected->refs==1 && old->refs==roots && source.references==identities && allocations-frees==baseline);
    CHECK(kproc_exec_replacement_prepare(&source,&caller,old,&replacement)==EACCDN && !replacement);
    other=caller;other.tid++;
    CHECK(kproc_exec_replacement_prepare(&source,&other,selected,&replacement)==EACCDN && !replacement);
    fail_allocation=1;
    CHECK(kproc_exec_replacement_prepare(&source,&caller,selected,&replacement)==ENSMEM && !replacement);
    fail_allocation=0;
    CHECK(!cleanup_depth && !source.exec_replacement_ticket && old->refs==roots && selected->refs==1);
    CHECK(allocations-frees==baseline && source.references==identities);
    for(i=1;i<=4;i++) {
        kproc_exec_replacement_test_fail_after(i);
        CHECK(kproc_exec_replacement_prepare(&source,&caller,selected,&replacement)==ENSMEM && !replacement);
        CHECK(!source.exec_replacement_ticket && !cleanup_depth &&
              old->refs==roots && selected->refs==1 && source.references==identities && allocations-frees==baseline);
    }
    CHECK(kproc_exec_replacement_prepare(&source,&caller,selected,&replacement)==E_OK);
    CHECK(replacement && cleanup_depth==1 && source.exec_replacement_ticket);
    ticket=source.exec_replacement_ticket;
    CHECK(source.active_space==old && source.pid==31 && source.number==9 && source.threads==&caller);
    CHECK(source.references==identities+1 && old->refs==roots+1 && selected->refs==2);
    CHECK(kproc_exec_replacement_validate(replacement)==E_OK);
    CHECK(kproc_exec_replacement_prepare(&source,&caller,selected,&second)==EACCDN && !second);
    CHECK(!kproc_exec_commit_begin(&source)); /* not a launch-right commit */
    {
        KPROC *retained=NULL;KASPACE *attached=NULL;KTHREAD *next=NULL;
        CHECK(!kproc_thread_attach_space(&source,old,&other,&retained,&attached,&next,TRUE));
        CHECK(!retained && !attached && !next && source.threads==&caller);
    }
    current_execution++;
    CHECK(kproc_exec_replacement_validate(replacement)==EACCDN);
    current_execution--;
    caller.tid++;
    CHECK(kproc_exec_replacement_validate(replacement)==EACCDN);
    caller.tid--;
    cancellation=TRUE;
    CHECK(kproc_exec_replacement_validate(replacement)==EACCDN);
    cancellation=FALSE;
    peer.pd=&child;peer.active_space=old;source.next=&peer;
    CHECK(kproc_exec_replacement_validate(replacement)==EACCDN);
    source.next=NULL;
    peer.active_space=selected;source.next=&peer;
    CHECK(kproc_exec_replacement_validate(replacement)==EACCDN);
    source.next=NULL;
    CHECK(kproc_exec_replacement_validate(replacement)==E_OK);
    kproc_exec_replacement_abort(&replacement);kproc_exec_replacement_abort(&replacement);
    CHECK(!replacement && !cleanup_depth && !source.exec_replacement_ticket);
    CHECK(source.active_space==old && source.pid==31 && source.number==9 && source.threads==&caller && caller.executing);
    CHECK(old->refs==roots && selected->refs==1 && source.references==identities && allocations-frees==baseline);
    CHECK(kproc_exec_replacement_prepare(&source,&caller,selected,&replacement)==E_OK);
    CHECK(source.exec_replacement_ticket>ticket);
    CHECK(!kproc_exec_replacement_matches(&source,&caller,old,selected,NULL,ticket));
    kproc_exec_replacement_abort(&replacement);
    CHECK(allocations-frees==baseline && old->refs==roots && selected->refs==1 && source.references==identities);
    /* Source resources are unchanged and both roots are alive at conservation.
     * Caller release below is normal fixture cleanup, not forced leak recovery. */
    CHECK(!old_backend.destroyed && !new_backend.destroyed && !source.completed && !source.observer_ready);
    source.active_space=NULL;kproc_list=NULL;
    kaspace_put(selected);kaspace_put(old);
    CHECK(old_backend.destroyed==1 && new_backend.destroyed==1 && allocations==frees);
    puts("Reversible exact PROCESS/TID/execution/root replacement admission: PASS (host CPU/thread models; no exec commit)");
    return 0;
}
