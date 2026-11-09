/* Production checked reference + FAT/PFS close; PROCESS registry/disk modeled. */
#define main close_backend_fixture_main
#include "fs_close.c"
#undef main
#include "kproc.h"
static BOOL source_live=TRUE;
PD *kproc_live_pd(const KPROC *p)
{ return source_live && p==(KPROC *)&owner ? &owner : NULL; }
void kproc_standard_file_refs(ULONG *refs)
{
    int i;
    memset(refs,0,OPNFILES*sizeof(*refs));
    for(i=0;i<NUMSTD;i++) if(owner.p_uft[i]>=NUMSTD)
        refs[owner.p_uft[i]-NUMSTD]++;
}
int main(void)
{
    FS_FILE_REFERENCE a=FS_FILE_REFERENCE_INIT,b=FS_FILE_REFERENCE_INIT;
    FS_FILE_REFERENCE empty=FS_FILE_REFERENCE_INIT;
    FS_CLOSE_REPORT report;
    FS_CLEANUP_TOKEN token;
    UQUAD incarnation;
    unsigned i;
    memset(&a,0,sizeof(a)); memset(&b,0,sizeof(b)); memset(&empty,0,sizeof(empty));
    for(i=0;i<NUMSTD;i++) owner.p_uft[i]=-1;
    open_file(); incarnation=sft[0].f_incarnation;
    CHECK(fs_reference_acquire((KPROC *)&owner,NUMSTD,incarnation+1,&a)==EIHNDL);
    CHECK(!memcmp(&a,&empty,sizeof(a)) && sft[0].f_use==1);
    CHECK(fs_reference_acquire((KPROC *)&file,NUMSTD,incarnation,&a)==EACCDN);
    retiring=TRUE;
    CHECK(fs_reference_acquire((KPROC *)&owner,NUMSTD,incarnation,&a)==EACCDN);
    retiring=FALSE; sft[0].f_close_busy=TRUE;
    CHECK(fs_reference_acquire((KPROC *)&owner,NUMSTD,incarnation,&a)==EIHNDL);
    sft[0].f_close_busy=FALSE;
    sft[0].f_cleanup_state=FS_CLEANUP_INVALIDATED;
    CHECK(fs_reference_acquire((KPROC *)&owner,NUMSTD,incarnation,&a)==EIHNDL);
    sft[0].f_cleanup_state=0;
    sft[0].f_use=(int)((unsigned int)~0U>>1);
    CHECK(fs_reference_acquire((KPROC *)&owner,NUMSTD,incarnation,&a)==EAGAIN);
    sft[0].f_use=1;
    CHECK(fs_reference_acquire((KPROC *)&owner,NUMSTD,incarnation,&a)==E_OK);
    CHECK(a.owned && a.handle==NUMSTD && a.incarnation==incarnation &&
          sft[0].f_use==2 && sft[0].f_staged_refs==1 && sft[0].f_ofd==&file);
    CHECK(fs_reference_acquire((KPROC *)&owner,NUMSTD,incarnation,&a)==EINVAL);
    CHECK(ixforce(0,NUMSTD,&owner)==E_OK);
    CHECK(fs_reference_acquire((KPROC *)&owner,0,incarnation,&b)==E_OK);
    CHECK(sft[0].f_use==4 && sft[0].f_staged_refs==2);
    CHECK(fs_close_bounded(0,FALSE,&report)==E_OK);
    CHECK(fs_close_bounded(NUMSTD,FALSE,&report)==E_OK && sft[0].f_use==2);
    /* Staged-only reference is not an explicit source binding or raw close. */
    CHECK(fs_reference_acquire((KPROC *)&owner,NUMSTD,incarnation,&empty)==EACCDN);
    CHECK(fs_close_once(NUMSTD,FALSE,&report)==EIHNDL && sft[0].f_use==2);
    source_live=FALSE; retiring=TRUE;
    CHECK(fs_reference_acquire((KPROC *)&owner,NUMSTD,incarnation,&empty)==EACCDN);
    CHECK(fs_reference_close(&a,FALSE,&report)==E_OK && !a.owned && sft[0].f_use==1);
    CHECK(fs_reference_close(&a,TRUE,&report)==EIHNDL);
    /* The remaining live reference owns backend state, not a live PROCESS. */
    buffer.b_dirty=TRUE; fail_disk=TRUE;
    CHECK(fs_reference_close(&b,FALSE,&report)==EWRITF && b.owned &&
          report.disposition==FS_CLOSE_RETAINED && sft[0].f_staged_refs==1);
    CHECK(fs_reference_close(&b,TRUE,&report)==EWRITF && !b.owned &&
          report.disposition==FS_CLOSE_TRANSFERRED && !sft[0].f_staged_refs &&
          sft[0].f_cleanup_refs==1);
    token=report.token;
    CHECK(fs_reference_close(&b,TRUE,&report)==EIHNDL);
    fail_disk=FALSE;
    CHECK(fs_cleanup_retry(&token,&report)==E_OK && !sft[0].f_use && freed==1);
    CHECK(fs_cleanup_retry(&token,&report)==EIHNDL);

    source_live=TRUE; retiring=FALSE; open_file(); incarnation=sft[0].f_incarnation;
    CHECK(fs_reference_acquire((KPROC *)&owner,NUMSTD,incarnation,&a)==E_OK);
    file.o_dmd=&media;
    ofd_invalidate_drive(&media);
    CHECK(!sft[0].f_staged_refs && !sft[0].f_own);
    CHECK(fs_reference_close(&a,TRUE,&report)==E_CHNG && !a.owned && freed==2);
    open_file(); incarnation=sft[0].f_incarnation;
    CHECK(fs_reference_acquire((KPROC *)&owner,NUMSTD,incarnation,&a)==E_OK);
    file.o_dmd=&media; ofd_invalidate_drive(&media);
    open_file();
    CHECK(fs_reference_close(&a,TRUE,&report)==EIHNDL && !a.owned &&
          sft[0].f_use==1 && !sft[0].f_staged_refs);
    CHECK(fs_close_bounded(NUMSTD,FALSE,&report)==E_OK);

    /* Same PFS cookie/position; a negative consuming close clears its owner. */
    CHECK(fs_sft_claim(0)); sft[0].f_own=&owner; sft[0].f_use=1;
    driver.close=driver_close; driver.release_file=driver_release_file;
    sft[0].f_pfs.fs=&driver; sft[0].f_pfs.pos=17;
    CHECK(fs_reference_acquire((KPROC *)&owner,NUMSTD,sft[0].f_incarnation,&a)==E_OK);
    sft[0].f_pfs.pos++;
    CHECK(sft[a.handle-NUMSTD].f_pfs.pos==18);
    CHECK(fs_close_bounded(NUMSTD,FALSE,&report)==E_OK && sft[0].f_use==1);
    pfs_error=EWRITF; release_error=EWRITF;
    a.closing=TRUE;
    CHECK(fs_reference_close(&a,TRUE,&report)==EACCDN && a.owned);
    a.closing=FALSE;
    CHECK(fs_reference_close(&a,TRUE,&report)==EWRITF && !a.owned &&
          report.disposition==FS_CLOSE_CONSUMED && !sft[0].f_use && !sft[0].f_staged_refs);
    CHECK(fs_reference_close(&a,TRUE,&report)==EIHNDL);
    puts("Production checked live SFT refs: authenticated acquire, alias counts, retirement-independent ownership, exact close/transfer, invalidation and PFS errors: PASS (host)");
    return 0;
}
