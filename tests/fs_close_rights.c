/* Compose production PROCESS CLOSE with the production consuming PFS backend.
 * Loader/CPU ownership is modeled; the driver close/finalizer is injected. */
#define main close_backend_fixture_main
#include "fs_close.c"
#undef main
#include "../bdos/process_exec.c"
static struct process_exec_handle *rights;
static unsigned cleanup_depth, observer_releases, handle_frees;
static LONG cleanup_error;
static char observer_identity;
PD *kproc_live_pd(const KPROC *p) { CHECK(p==(KPROC *)&owner); return &owner; }
struct process_exec_handle **kproc_exec_handles(KPROC *p)
{ CHECK(p==(KPROC *)&owner); return &rights; }
void ksched_cleanup_begin(KSCHED_CLEANUP_SCOPE *scope)
{ CHECK(!scope->active); scope->active=TRUE; cleanup_depth++; }
void ksched_cleanup_end(KSCHED_CLEANUP_SCOPE *scope)
{ CHECK(scope->active && cleanup_depth); scope->active=FALSE; cleanup_depth--; }
void ksched_process_cancel(void **prepared)
{
    FS_CLOSE_REPORT result;
    CHECK(cleanup_depth && *prepared==&sft[0]);
    *prepared=NULL;
    CHECK(fs_close_bounded(NUMSTD,TRUE,&result)==EWRITF);
    CHECK(result.disposition==FS_CLOSE_CONSUMED && !sft[0].f_cleanup_refs);
    cleanup_error=result.error;
}
LONG kproc_ref_file_cleanup(const KPROC *p,ULONG *transferred)
{
    CHECK(p==(KPROC *)&observer_identity && !sft[0].f_use && !observer_releases);
    if (transferred) *transferred=0;
    return cleanup_error;
}
void process_observer_release(PROCESS_OBSERVER **p)
{ CHECK(*p==(KPROC *)&observer_identity); observer_releases++; *p=NULL; }
void kfree(void *p) { CHECK(p && observer_releases==1); handle_frees++; }
int main(void)
{
    struct process_exec_handle handle;
    memset(&handle,0,sizeof(handle));
    CHECK(fs_sft_claim(0));
    driver.close=driver_close; driver.release_file=driver_release_file;
    release_error=EWRITF;
    sft[0].f_own=&owner; sft[0].f_use=1; sft[0].f_pfs.fs=&driver;
    owner.p_uft[0]=NUMSTD;
    handle.number=1; handle.prepared=&sft[0];
    handle.observer=(KPROC *)&observer_identity; rights=&handle;
    CHECK(proc_exec_handle_close((KPROC *)&owner,1)==EWRITF);
    CHECK(!rights && !cleanup_depth && observer_releases==1 && handle_frees==1);
    CHECK(!sft[0].f_use && !sft[0].f_own && !sft[0].f_cleanup_refs &&
          owner.p_uft[0]<0 && closes==1 && releases==1);
    CHECK(proc_exec_handle_close((KPROC *)&owner,1)==EIHNDL &&
          observer_releases==1 && handle_frees==1 && closes==1 && releases==1);
    puts("Production prepared CLOSE: consuming PFS finalizer error, lost right and exactly-once release: PASS (host)");
    return 0;
}
