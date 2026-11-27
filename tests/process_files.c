/* Production descriptor namespace + SFT close; disk/PROCESS execution modeled. */
#define main legacy_close_fixture_main
#define kproc_file_references_allowed legacy_reference_admission
#define kproc_standard_file_invalidate legacy_invalidate
#define flush legacy_flush
#include "fs_close.c"
#undef flush
#undef kproc_standard_file_invalidate
#undef kproc_file_references_allowed
#undef main
#include "process_files.h"
#include "kproc.h"
#include "process_search_absent.h"
extern void *malloc(size_t);
extern void free(void *);
struct kproc { PD *pd;UQUAD number;ULONG refs;BOOL dead;PROCESS_FILES files; };
static PD child_pd,other_pd;
static KPROC processes[3];
static unsigned allocations,frees;
static BOOL fail_allocation,rebind_retained;
#if CONF_WITH_FPOLL_TIME64
static FS_FILE_REFERENCE poll_pin = FS_FILE_REFERENCE_INIT;
static UQUAD poll_generation;
static BOOL pin_during_flush;
#endif
void *kalloc(ULONG bytes)
{ void *p;if(fail_allocation)return NULL;p=malloc(bytes);if(p)allocations++;return p; }
void kfree(void *p) { if(p){frees++;free(p);} }
PROCESS_FILES *kproc_files(PD *pd)
{ int i;for(i=0;i<3;i++)if(processes[i].pd==pd)return &processes[i].files;return NULL; }
PROCESS_FILES *kproc_ref_files(KPROC *p) { return p?&p->files:NULL; }
UQUAD kproc_ref_number(const KPROC *p) { return p?p->number:0; }
PD *kproc_live_pd(const KPROC *p) { return p&&!p->dead?p->pd:NULL; }
KPROC *kproc_retain(PD *pd)
{ int i;for(i=0;i<3;i++)if(processes[i].pd==pd&&!processes[i].dead){processes[i].refs++;return &processes[i];}return NULL; }
KPROC *kproc_retain_identity(KPROC *p) { if(!p||!p->refs)return NULL;p->refs++;return p; }
KPROC *kproc_retain_live(KPROC *p) { if(!p||p->dead)return NULL;p->refs++;return p; }
void kproc_release(KPROC *p) { if(p){CHECK(p->refs>1);p->refs--;} }
BOOL kproc_file_references_allowed(PD *pd)
{ PROCESS_FILES *t=kproc_files(pd);return !t||!t->retiring; }
void kproc_note_file_cleanup(PD *pd,LONG error,BOOL transferred)
{ (void)pd;(void)error;(void)transferred; }
void kproc_note_file_owner_cleanup(KPROC *p,LONG error,BOOL transferred)
{ (void)p;(void)error;(void)transferred; }
void kproc_standard_file_refs(ULONG *refs)
{
    int i,j;memset(refs,0,OPNFILES*sizeof(*refs));
    for(i=0;i<3;i++)for(j=0;j<NUMHANDLES;j++) {
        PROCESS_FILE_BINDING *b=&processes[i].files.binding[j];
        if(b->state && b->handle>=NUMSTD)refs[b->handle-NUMSTD]++;
    }
}
void kproc_standard_file_invalidate(WORD backend)
{
    int i;for(i=0;i<3;i++)process_files_invalidate(&processes[i].files,backend,sft[backend-NUMSTD].f_incarnation);
}
void flush(BCB *b)
{
    if(rebind_retained){rebind_retained=FALSE;CHECK(ixforce(0,NUMSTD+1,&owner)==E_OK);}
#if CONF_WITH_FPOLL_TIME64
    if (pin_during_flush) {
        WORD bios = 0;

        pin_during_flush = FALSE;
        CHECK(fs_sft_busy(0) && !fs_sft_accessible(0));
        CHECK(process_files_poll_pin(kproc_files(run), 0, &poll_pin, &bios, &poll_generation) == E_OK);
        CHECK(poll_pin.owned && poll_pin.handle == NUMSTD && !bios && poll_generation);
        CHECK(fs_reference_ready(&poll_pin, FS_READY_READ) == EAGAIN);
    }
#endif
    legacy_flush(b);
}
static void init_process(int i,PD *pd)
{
    int j;CHECK(processes[i].refs<=1);
    memset(pd,0,sizeof(*pd));for(j=0;j<NUMSTD;j++)pd->p_uft[j]=-1;
    processes[i].pd=pd;processes[i].refs=1;processes[i].dead=FALSE;processes[i].number++;
    CHECK(process_files_init(&processes[i].files,&processes[i],pd->p_uft));
}
static WORD open_bound(void)
{
    LONG descriptor;
    open_file();descriptor=process_files_open_begin(run,NUMSTD);CHECK(descriptor>=NUMSTD);
    CHECK(process_files_open_end(run,(WORD)descriptor,NUMSTD)==E_OK);
    return (WORD)descriptor;
}
static void inherit_files(UWORD policy)
{
    KPROC_FILE_SNAPSHOT *snapshot=NULL;
    CHECK(kproc_files_snapshot_begin(&processes[0],&snapshot)==E_OK);
    CHECK(kproc_files_snapshot_prepare(snapshot,&processes[1],policy)==E_OK);
    CHECK(kproc_files_snapshot_validate(snapshot));
    CHECK(kproc_files_snapshot_seal(snapshot)==E_OK && kproc_files_snapshot_seal(snapshot)==E_OK);
    run=&other_pd;CHECK(!kproc_files_snapshot_validate(snapshot));
    kproc_files_snapshot_commit(snapshot,&processes[1]);
    run=&owner;CHECK(!processes[0].files.snapshot);
    kproc_files_snapshot_end(&snapshot);
    CHECK(!snapshot);
}
static WORD pfs_second(void)
{
    LONG descriptor;
    CHECK(fs_sft_claim(1));sft[1].f_own=run;sft[1].f_use=1;sft[1].f_ofd=NULL;
    driver.close=driver_close;driver.release=driver_release;driver.release_file=driver_release_file;
    sft[1].f_pfs.fs=&driver;sft[1].f_pfs.pos=13;
    descriptor=process_files_open_begin(run,NUMSTD+1);CHECK(descriptor>=NUMSTD);
    CHECK(process_files_open_end(run,(WORD)descriptor,NUMSTD+1)==E_OK);
    return (WORD)descriptor;
}
int main(void)
{
    WORD descriptor,backend,duplicate,second;
    FS_HANDLE opened;
    UQUAD incarnation;
    FS_CLOSE_REPORT report;
    FS_CLEANUP_TOKEN residual;
    KPROC_FILE_SNAPSHOT *snapshot=NULL;
    unsigned before;
    int order;
    init_process(0,&owner);init_process(1,&child_pd);init_process(2,&other_pd);
    /* Initial KPROC precedes stdhdl_init in the real boot path. */
    memset(owner.p_uft,0,sizeof(owner.p_uft));
    CHECK(process_files_init(&processes[0].files,&processes[0],owner.p_uft));
    CHECK(process_files_resolve(&owner,0,&backend,NULL)==EIHNDL);
    for(order=0;order<NUMSTD;order++) CHECK(process_files_bind_bios(&owner,order,-1)==E_OK);
    CHECK(process_files_resolve(&owner,0,&backend,NULL)==E_OK && backend==-1);
    CHECK(xclose(NUMHANDLES)==EIHNDL && xclose(32767)==EIHNDL);
    puts("Empty initial UFT gains canonical BIOS bindings; invalid local Fclose retains EIHNDL: PASS");
    for(order=0;order<2;order++) {
        run=&owner;descriptor=open_bound();CHECK(descriptor==NUMSTD);
        CHECK(ixforce(0,descriptor,&owner)==E_OK && sft[0].f_use==2);
        inherit_files(PROCESS_FILES_DESCRIPTORS);CHECK(sft[0].f_use==4 && !sft[0].f_staged_refs);
        run=order?&owner:&child_pd;CHECK(getofd(descriptor)==&file);
        getofd(descriptor)->o_bytnum=37;
        CHECK(xclose(descriptor)==E_OK && xclose(descriptor)==EIHNDL);
        CHECK(sft[0].f_use==3);
        run=order?&child_pd:&owner;
        CHECK(getofd(descriptor)==&file && getofd(descriptor)->o_bytnum==37);
        CHECK(xclose(descriptor)==E_OK);CHECK(xclose(0)==E_OK);
        run=order?&owner:&child_pd;CHECK(xclose(0)==E_OK);
        CHECK(!sft[0].f_use && !sft[0].f_own && !sft[0].f_staged_refs);
    }
    puts("Independent same-number parent/child bindings, both close orders, shared OFD cursor, exact refs: PASS");
#if CONF_WITH_FPOLL_TIME64
    run = &owner;
    descriptor = open_bound();
    CHECK(ixforce(0, descriptor, &owner) == E_OK);
    pin_during_flush = TRUE;
    CHECK(xclose(descriptor) == E_OK && !pin_during_flush);
    CHECK(kproc_files(run)->binding[0].generation == poll_generation);
    CHECK(fs_reference_ready(&poll_pin, FS_READY_READ) == FS_READY_READ);
    CHECK(sft[0].f_use == 2 && sft[0].f_staged_refs == 1);
    CHECK(fs_reference_unpin(&poll_pin) == E_OK && !poll_pin.owned);
    CHECK(xclose(0) == E_OK && !sft[0].f_use && !sft[0].f_staged_refs);
    descriptor = open_bound();
    CHECK(fs_reference_pin_poll(descriptor, poll_pin.incarnation, &poll_pin) == EIHNDL);
    CHECK(!poll_pin.owned && xclose(descriptor) == E_OK);
    puts("Poll pins a live alias during close and rejects a stale SFT incarnation: PASS");
#endif
    run=&owner;descriptor=open_bound();CHECK(ixforce(0,descriptor,&owner)==E_OK);
    duplicate=(WORD)xdup(0);CHECK(duplicate==NUMSTD+1);
    second=pfs_second();CHECK(second==NUMSTD+2);
    CHECK(process_files_resolve(&owner,duplicate,&backend,NULL)==E_OK && backend==NUMSTD);
    CHECK(getofd(duplicate)==&file && !getofd(second));
    CHECK(fs_handle_lookup(second,&opened)==E_OK && opened.slot==&sft[1] && opened.slot->f_pfs.fs);
    sft[1].f_pfs.pos=29;inherit_files(PROCESS_FILES_ALL);
    CHECK(sft[0].f_use==6 && sft[1].f_use==2);
    CHECK(xclose(descriptor)==E_OK && xclose(duplicate)==E_OK && xclose(0)==E_OK && xclose(second)==E_OK);
    run=&child_pd;CHECK(getofd(duplicate)==&file && fs_handle_lookup(second,&opened)==E_OK);
    CHECK(opened.slot==&sft[1] && opened.slot->f_pfs.pos==29);
    CHECK(xclose(descriptor)==E_OK && xclose(duplicate)==E_OK && xclose(0)==E_OK && xclose(second)==E_OK);
    CHECK(!sft[0].f_use && !sft[1].f_use);run=&owner;
    puts("Displaced local/backend numbers and ALL snapshot retain exact OFD/PFS identities and shared cursors: PASS");
    {
        PD boot;PROCESS_FILES imported;
        memset(&boot,0,sizeof(boot));for(order=0;order<NUMSTD;order++)boot.p_uft[order]=-1;
        open_file();sft[0].f_own=&boot;run=&boot;boot.p_uft[0]=NUMSTD;sft[0].f_use++;
        CHECK(process_files_inherit(&child_pd,&boot,PROCESS_FILES_DESCRIPTORS)==E_OK && sft[0].f_use==3);
        CHECK(xclose(0)==E_OK && xclose(NUMSTD)==E_OK && sft[0].f_use==1);
        run=&child_pd;CHECK(getofd(0)==&file && xclose(0)==E_OK && !sft[0].f_use);
        run=&owner;open_file();owner.p_uft[0]=owner.p_uft[1]=NUMSTD;
        CHECK(!process_files_init(&imported,&processes[0],owner.p_uft));
        owner.p_uft[0]=owner.p_uft[1]=-1;
        CHECK(process_files_init(&imported,&processes[0],owner.p_uft));
        CHECK(imported.binding[NUMSTD].handle==NUMSTD && imported.binding[NUMSTD].incarnation==sft[0].f_incarnation);
        process_files_retire(&imported);CHECK(!sft[0].f_use);
    }
    puts("Trusted boot standard handoff and preexisting explicit ownership import preserve refs; inconsistent aliases rejected: PASS");
    run=&owner;descriptor=open_bound();before=allocations-frees;
    {
        PROCESS_FILE_OPERATION operation=PROCESS_FILE_OPERATION_INIT;
        UQUAD epoch=processes[0].files.epoch;
        CHECK(process_files_operation_begin(run,&operation)==E_OK);
        CHECK(processes[0].files.operations==1 && processes[0].files.epoch!=epoch);
        CHECK(kproc_files_snapshot_begin(&processes[0],&snapshot)==EACCDN && !snapshot);
        process_files_operation_end(&operation);process_files_operation_end(&operation);
        CHECK(!processes[0].files.operations);
        CHECK(kproc_files_snapshot_begin(&processes[0],&snapshot)==E_OK);
        CHECK(process_files_operation_begin(run,&operation)==EACCDN);
        process_files_invalidate_epoch(&processes[0].files);
        CHECK(!kproc_files_snapshot_validate(snapshot));kproc_files_snapshot_end(&snapshot);
    }
    fail_allocation=TRUE;CHECK(kproc_files_snapshot_begin(&processes[0],&snapshot)==ENSMEM && !snapshot);fail_allocation=FALSE;
    CHECK(allocations-frees==before);
    CHECK(kproc_files_snapshot_begin(&processes[0],&snapshot)==E_OK);
    CHECK(xclose(descriptor)==EACCDN && xdup(0)==EACCDN);
    CHECK(ixforce(0,descriptor,&owner)==EACCDN);
    CHECK(kproc_files_snapshot_validate(snapshot));
    kproc_files_snapshot_end(&snapshot);CHECK(xclose(descriptor)==E_OK);
    CHECK(allocations-frees==before);
    puts("Reversible source snapshot rejects competing close/dup/force, allocation failure releases admission: PASS");
    descriptor=open_bound();pfs_second();
    CHECK(kproc_files_snapshot_begin(&processes[0],&snapshot)==E_OK);
    sft[1].f_close_busy=TRUE;
    CHECK(kproc_files_snapshot_prepare(snapshot,&processes[1],PROCESS_FILES_DESCRIPTORS)==EIHNDL);
    sft[1].f_close_busy=FALSE;kproc_files_snapshot_end(&snapshot);
    CHECK(sft[0].f_use==1 && !sft[0].f_staged_refs && sft[1].f_use==1 && !sft[1].f_staged_refs);
    CHECK(xclose(descriptor)==E_OK && xclose(NUMSTD+1)==E_OK);
    puts("Partial inherited-reference acquisition rolls back only its exact prefix: PASS");
    descriptor=open_bound();file.o_dmd=&media;
    CHECK(kproc_files_snapshot_begin(&processes[0],&snapshot)==E_OK);
    CHECK(kproc_files_snapshot_prepare(snapshot,&processes[1],PROCESS_FILES_DESCRIPTORS)==E_OK);
    incarnation=sft[0].f_incarnation;
    ofd_invalidate_drive(&media);CHECK(!kproc_files_snapshot_validate(snapshot));
    CHECK(process_files_resolve(&owner,descriptor,&backend,NULL)==EIHNDL);
    kproc_files_snapshot_end(&snapshot);CHECK(!sft[0].f_use && !sft[0].f_staged_refs);
    CHECK(sft[0].f_incarnation==incarnation);
    puts("Authoritative media invalidation revokes exact source/target preparation without blocking teardown: PASS");
    /* Final PFS callback installs the same std number at a successor binding. */
    CHECK(fs_sft_claim(0));sft[0].f_own=run;sft[0].f_use=1;sft[0].f_ofd=NULL;
    sft[0].f_pfs.fs=&driver;descriptor=process_files_open_begin(run,NUMSTD);
    CHECK(process_files_open_end(run,descriptor,NUMSTD)==E_OK);
    pfs_second();CHECK(ixforce(0,descriptor,&owner)==E_OK);CHECK(xclose(descriptor)==E_OK);
    rebind_standard=TRUE;CHECK(xclose(0)==E_OK);rebind_standard=FALSE;
    CHECK(process_files_resolve(&owner,0,&backend,NULL)==E_OK && backend==NUMSTD+1 && sft[1].f_use==2);
    CHECK(xclose(0)==E_OK && xclose(NUMSTD+1)==E_OK);
    puts("Consuming finalizer preserves a callback's successor descriptor generation: PASS");
    descriptor=open_bound();pfs_second();CHECK(ixforce(0,descriptor,&owner)==E_OK);CHECK(xclose(descriptor)==E_OK);
    buffer.b_dirty=TRUE;fail_disk=TRUE;rebind_retained=TRUE;
    CHECK(process_files_close(&owner,0,FALSE,&report)==EWRITF && report.disposition==FS_CLOSE_TRANSFERRED);
    CHECK(!rebind_retained && sft[0].f_cleanup_refs==1 && !sft[0].f_staged_refs);
    residual=report.token;fail_disk=FALSE;
    CHECK(process_files_resolve(&owner,0,&backend,NULL)==E_OK && backend==NUMSTD+1);
    CHECK(fs_cleanup_retry(&residual,&report)==E_OK && !sft[0].f_use);
    CHECK(xclose(0)==E_OK && xclose(NUMSTD+1)==E_OK);
    puts("Retained close with callback successor transfers the old exact owner to canonical recovery: PASS");
    descriptor=open_bound();inherit_files(PROCESS_FILES_DESCRIPTORS);
    CHECK(kproc_files_snapshot_begin(&processes[0],&snapshot)==E_OK);
    CHECK(kproc_files_snapshot_prepare(snapshot,&processes[2],PROCESS_FILES_DESCRIPTORS)==E_OK);
    process_files_retire(&processes[0].files);processes[0].dead=TRUE;
    CHECK(!kproc_files_snapshot_validate(snapshot));kproc_files_snapshot_end(&snapshot);
    run=&child_pd;CHECK(getofd(descriptor)==&file && xclose(descriptor)==E_OK);
    process_files_retire(&processes[1].files);process_files_retire(&processes[2].files);
    CHECK(!sft[0].f_use && !sft[0].f_staged_refs && !sft[0].f_cleanup_refs && allocations==frees);
    CHECK(processes[0].refs==1 && processes[1].refs==1 && processes[2].refs==1);
    puts("Source retirement and failed admission cleanup preserve surviving bindings and exact final baselines: PASS");
    return 0;
}
