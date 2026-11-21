/* Actual PROCESS snapshot, trusted DTA cache and PFS iterator owners.
 * Only the disk provider and absent MMU/CPU are modeled. */
#define PROCESS_SEARCH_FIXTURE
#define pfs_handle_close descriptor_fixture_pfs_close
#include "process_files_retirement.c"
#undef pfs_handle_close
#undef main
#include "../fs/pfs.c"

LONG drvbits;
struct pfs_ops fat_pfs_ops;
static unsigned acquired,consumed,iterator_acquired,iterator_consumed;
static BOOL cookie_live[4096];
static DND directories[4096];
static DMD search_media;
static unsigned cookie_number(const PFSCOOKIE *cookie)
{
    uintptr_t first=(uintptr_t)directories,address=(uintptr_t)(ULONG)cookie->index;
    CHECK(address>=first && address<first+sizeof(directories) && (address-first)%sizeof(DND)==0);
    return (unsigned)((address-first)/sizeof(DND));
}
static struct { BOOL live;LONG offset; } positions[64];
static unsigned callback,clone_calls;
static KPROC_FILE_SNAPSHOT *attempt;
static KPROC *parent_owner,*child_owner;
static DTAINFO a,b;
static BOOL close_reentry,close_failure,shutdown_destroy;
static WORD eviction_retire,first_read_retire;
static LONG start(DTAINFO *);
static unsigned new_position(LONG offset)
{
    unsigned i;
    for(i=1;i<ARRAY_SIZE(positions);i++) if(!positions[i].live) {
        positions[i].live=TRUE;positions[i].offset=offset;iterator_acquired++;return i;
    }
    return 0;
}
static LONG directory_root(struct pfs_ops *fs,WORD drive,PFSCOOKIE *out)
{
    (void)drive;memset(out,0,sizeof(*out));out->fs=fs;out->index=(LONG)(uintptr_t)&directories[++acquired];
    directories[acquired].d_drv=&search_media;search_media.m_drvnum=2;
    CHECK(acquired<ARRAY_SIZE(cookie_live));cookie_live[cookie_number(out)]=TRUE;return E_OK;
}
static void directory_release(PFSCOOKIE *cookie)
{
    CHECK(cookie->index>0 && cookie_live[cookie_number(cookie)]);
    cookie_live[cookie_number(cookie)]=FALSE;consumed++;
    if(cookie->aux) {
        CHECK((ULONG)cookie->aux<ARRAY_SIZE(positions) && positions[cookie->aux].live);
        positions[cookie->aux].live=FALSE;iterator_consumed++;
    }
    memset(cookie,0,sizeof(*cookie));
}
static BOOL directory_valid(const PFSCOOKIE *cookie,LONG position)
{
    return cookie->fs==&fat_pfs_ops && cookie->index>0 && cookie_live[cookie_number(cookie)] &&
        position>0 && (ULONG)position<ARRAY_SIZE(positions) && cookie->aux==position && positions[position].live;
}
static LONG directory_read(PFSCOOKIE *cookie,LONG *position,char *name,int cap,PFSATTR *attr)
{
    LONG offset;
    if(!*position) { *position=new_position(0);cookie->aux=*position; }
    CHECK(directory_valid(cookie,*position));
    offset=positions[*position].offset++;
    if(first_read_retire) {
        WORD mode=first_read_retire;first_read_retire=0;
        if(mode==1) process_files_retire(&parent_owner->files);
        else if(mode==2) kproc_destroy(&caller);
        else CHECK(kproc_begin_file_cleanup(&caller));
    }
    if(callback==5) { callback=0;kproc_destroy(&caller); }
    if(offset>=3) return ENMFIL;
    memset(attr,0,sizeof(*attr));strlcpy(name,offset==0?"A.TXT":offset==1?"B.TXT":"C.TXT",cap);
    return E_OK;
}
static LONG directory_clone(const PFSCOOKIE *source,LONG position,PFSCOOKIE *out,LONG *next)
{
    LONG offset;
    CHECK(directory_valid(source,position));clone_calls++;offset=positions[position].offset;
    if(callback==1) { callback=0;kproc_files_snapshot_end(&attempt); }
    if(callback==2) { callback=0;kproc_destroy(&caller); }
    if(callback==7) { callback=0;kproc_destroy(&child); }
    if(callback==3 && clone_calls%2==0) return ENSMEM;
    if(callback==4) { callback=0;errcode=E_CHNG;errdrv=2;rwerr=E_CHNG;longjmp(errbuf,1); }
    CHECK(directory_root(&fat_pfs_ops,2,out)==E_OK);
    *next=new_position(offset);out->aux=*next;return E_OK;
}
static LONG directory_close(PFSCOOKIE *cookie,LONG position)
{
    CHECK(directory_valid(cookie,position));directory_release(cookie);
    if(eviction_retire) {
        WORD mode=eviction_retire;eviction_retire=0;
        if(mode==1) process_files_retire(&parent_owner->files);
        else kproc_destroy(&caller);
    }
    if(shutdown_destroy) {
        WORD i;
        shutdown_destroy=FALSE;
        CHECK(kproc_find(&caller)==parent_owner && parent_owner->files.retiring);
        for(i=0;i<KPROC_DTAS;i++)
            CHECK(!parent_owner->dtas[i].va && !parent_owner->dtas[i].directory);
        kproc_destroy(&caller);
    }
    if(close_reentry) {
        PD *saved=run;close_reentry=FALSE;run=&child;
        CHECK(start(&a)==E_OK);run=saved;
    }
    return close_failure?EWRITF:E_OK;
}
static LONG start(DTAINFO *dta)
{
    PROCESS_FILE_OPERATION operation=PROCESS_FILE_OPERATION_INIT;
    PD *process=run;
    LONG result;
    CHECK(process_files_search_begin(run,&operation)==E_OK);
    kproc_dta_forget(run,dta);
    result=pfs_do_sfirst_at("C:\\*.TXT",0,dta);
    if(result==E_OK) kproc_dta_save(run,dta);
    if(operation.table && (operation.table->retiring ||
        kproc_live_pd(operation.owner)!=process)) result=EACCDN;
    process_files_operation_end(&operation);return result;
}
static LONG advance(DTAINFO *dta)
{
    PROCESS_FILE_OPERATION operation=PROCESS_FILE_OPERATION_INIT;
    LONG result;
    CHECK(process_files_search_begin(run,&operation)==E_OK);
    if(!kproc_dta_restore(run,dta)) result=ENMFIL;
    else result=pfs_do_snext_at(dta);
    if(result==E_OK) kproc_dta_save(run,dta);
    else if(result==ENMFIL) kproc_dta_forget(run,dta);
    process_files_operation_end(&operation);return result;
}
static void setup(void)
{
    CHECK(!kproc_list && !retired_processes && !attempt && acquired==consumed && iterator_acquired==iterator_consumed);
    run=&caller;memset(&caller,0,sizeof(caller));memset(&child,0,sizeof(child));
    CHECK(kproc_create(&caller) && kproc_create(&child));
    parent_owner=kproc_find(&caller);child_owner=kproc_find(&child);
    parent_owner->aspace=child_owner->aspace=(ASPACE *)1;
    parent_owner->dta=&a;child_owner->dta=NULL;caller.p_curdrv=child.p_curdrv=2;
    CHECK(start(&a)==E_OK && !strcmp(a.dt_fname,"A.TXT"));
}
static void finish(void)
{
    kproc_files_snapshot_end(&attempt);callback=0;
    kproc_destroy(&caller);kproc_destroy(&child);
    CHECK(!kproc_list && !retired_processes && allocations==frees);
    CHECK(acquired==consumed && iterator_acquired==iterator_consumed);
}
int main(void)
{
    unsigned mode;
    fat_pfs_ops.root=directory_root;fat_pfs_ops.release=directory_release;
    fat_pfs_ops.readdir=directory_read;fat_pfs_ops.clone_iterator=directory_clone;
    fat_pfs_ops.iterator_valid=directory_valid;fat_pfs_ops.close_iterator=directory_close;
    for(mode=0;mode<2;mode++) {
        UBYTE expected[offsetof(DTAINFO,dt_fattr)];
        setup();CHECK(start(&b)==E_OK);
        CHECK(kproc_dta_restore(&caller,&a));memcpy(expected,&a,sizeof(expected));
        CHECK(kproc_files_snapshot_begin(parent_owner,&attempt)==E_OK);
        CHECK(kproc_files_snapshot_prepare(attempt,child_owner,PROCESS_FILES_ALL)==E_OK);
        CHECK(child_owner->dta==NULL && !child_owner->dtas[0].va);
        CHECK(kproc_files_snapshot_seal(attempt)==E_OK);
        kproc_files_snapshot_commit(attempt,child_owner);kproc_files_snapshot_end(&attempt);
        CHECK(child_owner->dta==&a);
        memset(&a,0xa5,offsetof(DTAINFO,dt_fattr));
        CHECK(kproc_dta_restore(&child,&a));
        CHECK(!memcmp(expected,&a,sizeof(expected)));
        if(mode) kproc_destroy(&caller);
        else { CHECK(advance(&a)==E_OK && !strcmp(a.dt_fname,"B.TXT")); }
        run=&child;CHECK(advance(&a)==E_OK && !strcmp(a.dt_fname,"B.TXT"));
        CHECK(advance(&b)==E_OK && !strcmp(b.dt_fname,"B.TXT"));
        CHECK(advance(&a)==E_OK && !strcmp(a.dt_fname,"C.TXT"));
        CHECK(advance(&a)==ENMFIL);
        kproc_destroy(&child);
        if(!mode) { run=&caller;CHECK(advance(&a)==E_OK && !strcmp(a.dt_fname,"C.TXT")); }
        finish();
    }
    puts("Production PROCESS/DTA/PFS snapshot: independent cursors, two DTAs, parent-first/child-first cleanup and exact refs: PASS");
    for(mode=0;mode<5;mode++) {
        LONG error;
        setup();CHECK(start(&b)==E_OK);
        if(mode==0) fat_pfs_ops.clone_iterator=NULL;
        if(mode==3) clone_calls=0;
        CHECK(kproc_files_snapshot_begin(parent_owner,&attempt)==E_OK);
        callback=mode;
        error=kproc_files_snapshot_prepare(attempt,child_owner,PROCESS_FILES_ALL);
        CHECK(error<0 && !child_owner->dtas[0].va && !child_owner->dta);
        callback=0;fat_pfs_ops.clone_iterator=directory_clone;
        finish();
    }
    puts("Unsupported iterator, reentrant abort, source retirement, partial provider failure and disk-error rollback preserve exact baselines: PASS");
    setup();
    CHECK(kproc_files_snapshot_begin(parent_owner,&attempt)==E_OK);fail_allocate=TRUE;
    CHECK(kproc_files_snapshot_prepare(attempt,child_owner,PROCESS_FILES_ALL)==ENSMEM);
    fail_allocate=FALSE;kproc_files_snapshot_end(&attempt);
    CHECK(acquired-consumed==1 && iterator_acquired-iterator_consumed==1);
    CHECK(kproc_files_snapshot_begin(parent_owner,&attempt)==E_OK);
    CHECK(kproc_files_snapshot_prepare(attempt,child_owner,PROCESS_FILES_ALL)==E_OK);
    kproc_files_snapshot_end(&attempt);finish();
    puts("Trusted search metadata allocation failure releases admission and permits an exact retry: PASS");
    setup();
    {
        static DTAINFO searches[KPROC_DTAS+1];
        static PD unrelated;
        WORD i,slot;
        PFS_SEARCH *block;
        for(i=0;i<KPROC_DTAS+1;i++) CHECK(start(&searches[i])==E_OK);
        CHECK(advance(&a)==ENMFIL && advance(&searches[0])==ENMFIL);
        CHECK(acquired-consumed==KPROC_DTAS && iterator_acquired-iterator_consumed==KPROC_DTAS);
        for(slot=0;slot<CONF_PFS_MAX_SEARCHES;slot++) if(search_slot_free(&pfs_searches[slot])) break;
        CHECK(slot<CONF_PFS_MAX_SEARCHES);block=&pfs_searches[slot];
        block->owner=(DTA *)&unrelated;block->proc=&unrelated;
        CHECK(directory_root(&fat_pfs_ops,2,&block->iterator.directory)==E_OK);
        block->iterator.position=new_position(0);block->iterator.directory.aux=block->iterator.position;block->iterator.owned=TRUE;
        CHECK(kproc_files_snapshot_begin(parent_owner,&attempt)==E_OK);
        CHECK(kproc_files_snapshot_prepare(attempt,child_owner,PROCESS_FILES_ALL)==ENHNDL);
        CHECK(!child_owner->dtas[0].va);kproc_files_snapshot_end(&attempt);
        CHECK(acquired-consumed==KPROC_DTAS+1 && iterator_acquired-iterator_consumed==KPROC_DTAS+1);
        pfs_search_free(block);
        CHECK(kproc_files_snapshot_begin(parent_owner,&attempt)==E_OK);
        CHECK(kproc_files_snapshot_prepare(attempt,child_owner,PROCESS_FILES_ALL)==E_OK);
        kproc_files_snapshot_end(&attempt);
    }
    finish();
    puts("Trusted-cache eviction closes exact iterators; full search-table partial preparation rolls back and retries successfully: PASS");
    for(mode=1;mode<=2;mode++) {
        static DTAINFO searches[KPROC_DTAS];
        WORD i;
        setup();
        for(i=0;i<KPROC_DTAS-1;i++) CHECK(start(&searches[i])==E_OK);
        CHECK(acquired-consumed==KPROC_DTAS);
        eviction_retire=(WORD)mode;
        CHECK(start(&searches[KPROC_DTAS-1])==EACCDN && !eviction_retire);
        CHECK(acquired==consumed && iterator_acquired==iterator_consumed);
        for(i=0;i<CONF_PFS_MAX_SEARCHES;i++) {
            CHECK(search_slot_free(&pfs_searches[i]));
            CHECK(!pfs_searches[i].process && !pfs_searches[i].proc);
        }
        finish();
    }
    puts("Full-cache eviction retirement/destruction closes the pending uncached search before releasing its PROCESS identity: PASS");
    for(mode=1;mode<=2;mode++) {
        WORD i;
        setup();first_read_retire=(WORD)mode;
        CHECK(start(&b)==EACCDN && !first_read_retire);
        CHECK(acquired==consumed && iterator_acquired==iterator_consumed);
        for(i=0;i<CONF_PFS_MAX_SEARCHES;i++) {
            CHECK(search_slot_free(&pfs_searches[i]));
            CHECK(!pfs_searches[i].process && !pfs_searches[i].proc);
        }
        finish();
    }
    puts("Retirement during the first provider read closes its uncached iterator for both live and destroyed PROCESSes: PASS");
    setup();first_read_retire=3;
    CHECK(start(&b)==EACCDN && !first_read_retire);
    CHECK(acquired-consumed==1 && iterator_acquired-iterator_consumed==1);
    finish();
    puts("Closed cleanup admission during the first provider read rejects and consumes the uncached iterator: PASS");
    setup();
    {
        PFS_SEARCH *search=NULL;
        WORD i;
        for(i=0;i<CONF_PFS_MAX_SEARCHES;i++) if(pfs_searches[i].process==parent_owner) search=&pfs_searches[i];
        CHECK(search);search->process=NULL;
        CHECK(kproc_files_snapshot_begin(parent_owner,&attempt)==E_OK);
        CHECK(kproc_files_snapshot_prepare(attempt,child_owner,PROCESS_FILES_ALL)==EACCDN);
        kproc_files_snapshot_end(&attempt);search->process=parent_owner;
    }
    finish();
    puts("Stale raw-PD iterator association fails canonical PROCESS admission before publication: PASS");
    setup();CHECK(start(&b)==E_OK);
    CHECK(kproc_files_snapshot_begin(parent_owner,&attempt)==E_OK);
    CHECK(kproc_files_snapshot_prepare(attempt,child_owner,PROCESS_FILES_ALL)==E_OK);
    pfs_invalidate_fat_drive(2);
    CHECK(!kproc_files_snapshot_validate(attempt));kproc_files_snapshot_end(&attempt);
    CHECK(acquired==consumed && iterator_acquired==iterator_consumed);
    CHECK(kproc_files_snapshot_begin(parent_owner,&attempt)==E_OK);
    CHECK(kproc_files_snapshot_prepare(attempt,child_owner,PROCESS_FILES_ALL)==E_OK);
    kproc_files_snapshot_end(&attempt);finish();
    puts("Authoritative FAT media invalidation closes published and staged owners, removes stale DTA witnesses and admits a fresh snapshot: PASS");
    setup();callback=5;close_reentry=TRUE;CHECK(advance(&a)==EACCDN);
    run=&child;CHECK(advance(&a)==E_OK && !strcmp(a.dt_fname,"B.TXT"));finish();
    setup();close_failure=TRUE;
    kproc_dta_forget(&caller,&a);kproc_dta_forget(&caller,&a);
    CHECK(acquired==consumed && iterator_acquired==iterator_consumed);close_failure=FALSE;finish();
    setup();callback=7;
    CHECK(kproc_files_snapshot_begin(parent_owner,&attempt)==E_OK);
    CHECK(kproc_files_snapshot_prepare(attempt,child_owner,PROCESS_FILES_ALL)==EACCDN);finish();
    puts("In-flight source retirement and target retirement during provider clone release each exact owner once: PASS");
    setup();CHECK(start(&b)==E_OK);shutdown_destroy=TRUE;
    kproc_files_shutdown();
    CHECK(!shutdown_destroy && !kproc_find(&caller));
    CHECK(acquired==consumed && iterator_acquired==iterator_consumed);
    {
        WORD i;
        for(i=0;i<CONF_PFS_MAX_SEARCHES;i++) CHECK(search_slot_free(&pfs_searches[i]));
    }
    finish();
    puts("Shutdown detaches every DTA before a close callback destroys its registered PROCESS; exact iterator and metadata baseline: PASS");
    setup();CHECK(kproc_files_snapshot_begin(parent_owner,&attempt)==E_OK);
    CHECK(kproc_files_snapshot_prepare(attempt,child_owner,PROCESS_FILES_STANDARD)==E_OK);
    CHECK(kproc_files_snapshot_seal(attempt)==E_OK);kproc_files_snapshot_commit(attempt,child_owner);
    CHECK(!child_owner->dta && !child_owner->dtas[0].va);finish();
    puts("Ordinary Pexec standard-descriptor handoff leaves searches fresh: PASS");
    return 0;
}
