/* Exercise the production FAT/PFS close, SFT owner and nonlocal error path. */
#include <stdio.h>
extern void abort(void) __attribute__((noreturn));
#include "emutos.h"
#include "fs.h"
#include "fs_close.h"
#include "console.h"
#include "mem.h"
#include "gemerror.h"
#include "string.h"
#include "../bios/virtio_9p_clunk.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"close:%d: %s\n",__LINE__,#x); abort(); } } while (0)
FTAB sft[OPNFILES];
BCB *bufl[2];
PD owner;
PD *run=&owner;
#ifdef FS_ERROR_H
#if CONF_WITH_MULTITHREADING
static FS_ERROR_STATE fixture_error;
FS_ERROR_STATE *fs_error_current(void) { return &fixture_error; }
#else
FS_ERROR_STATE fs_legacy_error;
#endif
#else
jmp_buf errbuf;
long errcode,rwerr;
int errdrv;
#endif
static unsigned freed,flushes,invalidations,closes,releases;
static BOOL fail_disk,fail_disk_once,probe_busy,rebind_standard,retiring;
static LONG pfs_error,release_error;
static BOOL chain_fixture;
static CLNO cluster_chain[4];
static OFD file;
static BCB buffer;
static DMD media;
void panic(const char *format, ...) { fprintf(stderr,"panic: %s\n",format); abort(); }
BOOL kproc_file_references_allowed(PD *pd) { return pd!=&owner || !retiring; }
SBYTE *kproc_uft(PD *pd) { return pd->p_uft; }
SBYTE get_default_handle(int i) { (void)i; return -1; }
void kproc_standard_file_invalidate(WORD handle)
{ int i; for(i=0;i<NUMSTD;i++) if(owner.p_uft[i]==handle) owner.p_uft[i]=-1; }
void mark_bcbs_invalid(int drive) {
#ifdef PROCESS_SEARCH_FIXTURE
    CHECK(drive==0 || drive==2);
#else
    CHECK(drive==0);
#endif
    invalidations++;
}
void xmfreblk(void *p) { CHECK(p==&file); freed++; }
void flush(BCB *b)
{
    CHECK(b==&buffer); flushes++;
    if (probe_busy) {
        FS_CLOSE_REPORT nested;
        CHECK(!fs_sft_accessible(0) && fs_sft_busy(0));
        if ((ULONG)sft[0].f_use>sft[0].f_cleanup_refs) {
            CHECK(fs_close_once(NUMSTD,FALSE,&nested)==EACCDN);
            CHECK(nested.disposition==FS_CLOSE_RETAINED);
        } else {
            CHECK(fs_close_once(NUMSTD,FALSE,&nested)==EIHNDL);
            CHECK(nested.disposition==FS_CLOSE_NOT_OWNED);
        }
    }
    if (fail_disk || fail_disk_once) { fail_disk_once=FALSE; errcode=EWRITF; errdrv=0; rwerr=EWRITF; longjmp(errbuf,1); }
    b->b_dirty=FALSE;
}
long ixlseek(OFD *fd,long n) { (void)fd;(void)n; abort(); }
long ixread(OFD *fd,long n,void *p) { (void)fd;(void)n;(void)p; abort(); }
long ixwrite(OFD *fd,long n,void *p) { (void)fd;(void)n;(void)p; abort(); }
CLNO getrealcl(CLNO n,DMD *d)
{ CHECK(chain_fixture && d==&media && (n==2 || n==3)); return cluster_chain[n]; }
void clfix(CLNO n,CLNO v,DMD *d)
{ CHECK(chain_fixture && d==&media && (n==2 || n==3) && v==FREECLUSTER);
  cluster_chain[n]=v; buffer.b_dirty=TRUE; buffer.b_dm=d; buffer.b_bufdrv=0; }
static LONG driver_close(PFSCOOKIE *cookie) { CHECK(cookie==&sft[0].f_pfs || cookie==&sft[1].f_pfs); closes++; return pfs_error; }
static void driver_release(PFSCOOKIE *cookie)
{
    CHECK((cookie==&sft[0].f_pfs && owner.p_uft[0]!=NUMSTD) ||
          (cookie==&sft[1].f_pfs && owner.p_uft[0]!=NUMSTD+1));
    releases++;
    if (rebind_standard) {
        CHECK(ixforce(0,NUMSTD+1,&owner)==E_OK);
        CHECK(owner.p_uft[0]==NUMSTD+1 && sft[1].f_use==2);
    }
}
#ifndef USE_PRODUCTION_PFS_IO
LONG pfs_handle_close(PFSCOOKIE *cookie) { return cookie->fs->close(cookie); }
#endif
static LONG driver_release_file(PFSCOOKIE *cookie)
{ driver_release(cookie); return release_error; }
static struct pfs_ops driver;
static void open_file(void)
{
    CHECK(fs_sft_claim(0));
    memset(&file,0,sizeof(file));
    file.o_dfd=&file.o_disk; file.o_disk.o_usecnt=1;
    file.o_refs=1;
    sft[0].f_ofd=&file; sft[0].f_own=&owner; sft[0].f_use=1;
    memset(&buffer,0,sizeof(buffer)); buffer.b_bufdrv=0; buffer.b_dirty=TRUE;
    bufl[0]=&buffer;
}
#if CONF_WITH_PROCESS_FILES && !defined(OBSERVER_WITH_CLOSE_FIXTURE)
#include "process_files.h"

/* These backend fixtures operate on legacy SFT aliases. */
void __attribute__((weak)) kproc_files_shutdown(void) { }
BOOL __attribute__((weak)) process_files_managed(PD *pd) { (void)pd; return FALSE; }
LONG __attribute__((weak)) process_files_duplicate(PD *source,WORD handle,PD *target,WORD destination)
{ (void)source; (void)handle; (void)target; (void)destination; CHECK(0); return EINVFN; }
LONG __attribute__((weak)) process_files_force_bios(PD *pd,WORD descriptor,WORD bios)
{ (void)pd; (void)descriptor; (void)bios; CHECK(0); return EINVFN; }
LONG __attribute__((weak)) process_files_resolve(PD *pd,WORD descriptor,WORD *backend,UQUAD *incarnation)
{ (void)pd; (void)descriptor; (void)backend; (void)incarnation; CHECK(0); return EINVFN; }
#endif

int main(void)
{
    FS_CLOSE_REPORT result;
    FS_CLEANUP_TOKEN old,second;
    UQUAD replacement;
    FS_CLEANUP_STATUS status;
    {
        FS_HANDLE handle;
        WORD alias;

        owner.p_uft[0] = H_Aux;
        alias = (WORD)xdup(0);
        CHECK(alias == NUMSTD && fs_handle_lookup(alias,&handle) == E_OK);
        CHECK(handle.slot == &sft[0] && handle.bios == H_Aux && !getofd(alias));
        CHECK(ixforce(1,alias,&owner) == E_OK && owner.p_uft[1] == H_Aux);
        CHECK(fs_close_once(alias,FALSE,&result) == E_OK && !sft[0].f_bios);
        CHECK(fs_handle_lookup(alias,&handle) == EIHNDL);
        CHECK(fs_handle_lookup(1,&handle) == E_OK && !handle.slot && handle.bios == H_Aux);
        owner.p_uft[0] = owner.p_uft[1] = H_Console;
    }
    {
        UBYTE reply[11]={7,0,0,0,121,0,0,0,0,0,0};
        CHECK(v9p_clunk_terminal(reply,7,0));
        CHECK(!v9p_clunk_terminal(reply,EWRITF,0));
        CHECK(!v9p_clunk_terminal(reply,6,0));
        reply[5]=1; CHECK(!v9p_clunk_terminal(reply,7,0)); reply[5]=0;
        reply[4]=7; CHECK(!v9p_clunk_terminal(reply,7,0));
        reply[0]=11; CHECK(v9p_clunk_terminal(reply,11,0));
        reply[4]=120; CHECK(!v9p_clunk_terminal(reply,11,0));
    }
    open_file(); probe_busy=TRUE; fail_disk=TRUE;
    CHECK(fs_close_bounded(NUMSTD,TRUE,&result)==EWRITF);
    CHECK(result.disposition==FS_CLOSE_TRANSFERRED && !freed);
    CHECK(result.first_disk_error && result.first_drive==0 && result.first_rw==EWRITF);
    CHECK(flushes==BLKDEVNUM+1 && invalidations==BLKDEVNUM+1);
    CHECK(sft[0].f_use==1 && sft[0].f_cleanup_refs==1 && !fs_sft_accessible(0));
    old=result.token;
    CHECK(fs_cleanup_snapshot(&old,&status)==0 && status.references==1 && status.first_error==EWRITF);
    CHECK(fs_close_once(NUMSTD,FALSE,&result)==EIHNDL);
    fail_disk=FALSE;
    CHECK(fs_cleanup_retry(&old,&result)==0 && result.disposition==FS_CLOSE_CONSUMED && freed==1);
    CHECK(fs_cleanup_snapshot(&old,&status)==0 && status.state==FS_CLEANUP_COMPLETE && !status.references);
    CHECK(fs_cleanup_retry(&old,&result)==EIHNDL && freed==1);
    /* Same storage/OFD address cannot revive the old cleanup capability. */
    open_file();
    CHECK(fs_cleanup_retry(&old,&result)==EIHNDL && sft[0].f_use==1);
    /* Normal-return FAT error retains the reference. */
    file.o_refs=0;
    CHECK(fs_close_bounded(NUMSTD,TRUE,&result)==EINTRN && result.disposition==FS_CLOSE_TRANSFERRED);
    CHECK(!result.disk_error && !result.first_disk_error); /* an ordinary failure is not a hard error */
    old=result.token; file.o_refs=1;
    CHECK(fs_cleanup_retry(&old,&result)==0 && freed==2);
    /* A live inherited alias and a residual owner release independently. */
    open_file(); sft[0].f_use=2; fail_disk=TRUE;
    CHECK(fs_close_bounded(NUMSTD,TRUE,&result)==EWRITF && result.disposition==FS_CLOSE_TRANSFERRED);
    old=result.token; fail_disk=FALSE;
    CHECK(fs_sft_accessible(0) && sft[0].f_cleanup_refs==1 && sft[0].f_use==2);
    CHECK(fs_close_bounded(NUMSTD,FALSE,&result)==0 && sft[0].f_use==1 && freed==2);
    CHECK(!fs_sft_accessible(0) && fs_cleanup_retry(&old,&result)==0 && freed==3);
    /* A failed standard-handle close retains the mapping until consumed. */
    open_file(); owner.p_uft[0]=NUMSTD; fail_disk=TRUE;
    CHECK(fs_close_bounded(0,FALSE,&result)==EWRITF && result.disposition==FS_CLOSE_RETAINED);
    CHECK(owner.p_uft[0]==NUMSTD && !sft[0].f_cleanup_refs);
    fail_disk=FALSE;
    CHECK(fs_close_bounded(0,FALSE,&result)==0 && owner.p_uft[0]==-1 && freed==4);
    /* Authoritative media invalidation, not the cleanup service, owns release. */
    open_file(); fail_disk=TRUE;
    CHECK(fs_close_bounded(NUMSTD,TRUE,&result)==EWRITF);
    old=result.token; replacement=sft[0].f_incarnation;
    fail_disk=FALSE;
    file.o_dmd=&media; owner.p_uft[0]=NUMSTD;
    ofd_invalidate_drive(&media);
    CHECK(owner.p_uft[0]==-1);
    CHECK(sft[0].f_incarnation==replacement && !sft[0].f_cleanup_refs);
    CHECK(fs_cleanup_snapshot(&old,&status)==0 && status.state==FS_CLEANUP_INVALIDATED);
    CHECK(fs_cleanup_retry(&old,&result)==EIHNDL && freed==5);
    CHECK(!sft[0].f_use && !sft[0].f_own && !sft[0].f_ofd);
    /* Recovered tickets cannot revive on a later transfer in the SAME slot. */
    open_file(); sft[0].f_use=2; fail_disk=TRUE;
    CHECK(fs_close_bounded(NUMSTD,TRUE,&result)==EWRITF); old=result.token;
    fail_disk=FALSE;
    CHECK(fs_cleanup_retry(&old,&result)==0 && sft[0].f_use==1 && freed==5);
    buffer.b_dirty=TRUE; fail_disk=TRUE;
    CHECK(fs_close_bounded(NUMSTD,TRUE,&result)==EWRITF); second=result.token;
    CHECK(second.incarnation==old.incarnation && second.reference>old.reference);
    CHECK(fs_cleanup_snapshot(&old,&status)==0 && !status.references && status.state==FS_CLEANUP_COMPLETE);
    CHECK(fs_cleanup_retry(&old,&result)==EIHNDL && sft[0].f_use==1 && sft[0].f_cleanup_refs==1);
    fail_disk=FALSE;
    CHECK(fs_cleanup_retry(&second,&result)==0 && freed==6);
    /* Multiple transfer rights stay distinct, bounded and FIFO. */
    open_file(); sft[0].f_use=2; fail_disk=TRUE;
    CHECK(fs_close_bounded(NUMSTD,TRUE,&result)==EWRITF); old=result.token;
    CHECK(fs_close_bounded(NUMSTD,TRUE,&result)==EWRITF); second=result.token;
    CHECK(fs_cleanup_retry(&second,&result)==EAGAIN && sft[0].f_cleanup_refs==2);
    fail_disk=FALSE;
    CHECK(fs_cleanup_retry(&old,&result)==0 && sft[0].f_use==1 && !fs_sft_accessible(0));
    CHECK(fs_cleanup_retry(&old,&result)==EIHNDL && sft[0].f_cleanup_refs==1);
    CHECK(fs_cleanup_retry(&second,&result)==0 && freed==7);
    /* PFS errors consume a reference despite a negative return. */
    CHECK(fs_sft_claim(0));
    driver.close=driver_close; driver.release=driver_release;
    sft[0].f_own=&owner; sft[0].f_use=2; sft[0].f_pfs.fs=&driver;
    pfs_error=EWRITF; owner.p_uft[0]=NUMSTD;
    CHECK(fs_close_bounded(NUMSTD,TRUE,&result)==EWRITF && result.disposition==FS_CLOSE_CONSUMED);
    CHECK(sft[0].f_use==1 && !sft[0].f_cleanup_refs && closes==1 && !releases);
    CHECK(fs_close_bounded(NUMSTD,TRUE,&result)==EWRITF && result.disposition==FS_CLOSE_CONSUMED);
    CHECK(closes==2 && releases==1 && !sft[0].f_use && !sft[0].f_own && owner.p_uft[0]<0);
    CHECK(fs_sft_claim(0));
    sft[0].f_own=&owner; sft[0].f_use=1; sft[0].f_pfs.fs=&driver;
    driver.release_file=driver_release_file; pfs_error=0; release_error=EWRITF;
    CHECK(fs_close_bounded(NUMSTD,TRUE,&result)==EWRITF && result.disposition==FS_CLOSE_CONSUMED);
    CHECK(releases==2 && closes==3 && !sft[0].f_use && !sft[0].f_cleanup_refs);
    CHECK(fs_sft_claim(0));
    sft[0].f_own=&owner; sft[0].f_use=1;
    sft[0].f_ofd=NULL; sft[0].f_pfs.fs=NULL;
    CHECK(fs_close_bounded(NUMSTD,TRUE,&result)==EIHNDL && result.disposition==FS_CLOSE_CONSUMED);
    CHECK(!sft[0].f_own && !sft[0].f_use && !sft[0].f_cleanup_refs);
    /* A real consuming PFS finalizer redirects the just-unpublished alias.
     * The outer standard close must not discard that new live reference. */
    CHECK(fs_sft_claim(0) && fs_sft_claim(1));
    sft[0].f_own=&owner; sft[0].f_use=1; sft[0].f_pfs.fs=&driver;
    sft[1].f_own=&owner; sft[1].f_use=1; sft[1].f_pfs.fs=&driver;
    owner.p_uft[0]=NUMSTD; rebind_standard=TRUE;
    CHECK(fs_close_bounded(0,FALSE,&result)==EWRITF &&
          result.disposition==FS_CLOSE_CONSUMED);
    CHECK(!sft[0].f_use && !sft[0].f_own && owner.p_uft[0]==NUMSTD+1 &&
          sft[1].f_use==2 && closes==4 && releases==3);
    rebind_standard=FALSE;
    CHECK(fs_close_bounded(0,FALSE,&result)==E_OK &&
          result.disposition==FS_CLOSE_CONSUMED && owner.p_uft[0]<0 && sft[1].f_use==1);
    CHECK(fs_close_bounded(NUMSTD+1,FALSE,&result)==EWRITF &&
          result.disposition==FS_CLOSE_CONSUMED && !sft[1].f_use && !sft[1].f_own &&
          closes==6 && releases==4);
    open_file();
    CHECK(ixforce(0,NUMSTD,&owner)==E_OK);
    fail_disk=TRUE;
    CHECK(fs_close_bounded(NUMSTD,TRUE,&result)==EWRITF && sft[0].f_cleanup_refs==1);
    second=result.token; fail_disk=FALSE;
    CHECK(fs_close_once(NUMSTD,FALSE,&result)==E_OK && sft[0].f_use==1 &&
          sft[0].f_cleanup_refs==1 && owner.p_uft[0]==get_default_handle(0));
    CHECK(fs_cleanup_retry(&second,&result)==E_OK && !sft[0].f_use);
    /* A later attempt can succeed (another drive's buffers interrupted the
     * first): the first hard error is still what a caller with its own cleanup
     * must rethrow to the GEMDOS recovery, even though the last step is clean. */
    open_file(); fail_disk_once=TRUE;
    CHECK(fs_close_bounded(NUMSTD,FALSE,&result)==EWRITF && result.disposition==FS_CLOSE_CONSUMED);
    CHECK(!result.disk_error && result.first_disk_error && result.first_drive==0 && result.first_rw==EWRITF);
    CHECK(!sft[0].f_use && !sft[0].f_own);
    puts("Bounded close reports its first hard error after a later attempt succeeds: PASS");
    retiring=TRUE;
    CHECK(!fs_sft_claim(0));
    CHECK(ixforce(0,-1,&owner)==EACCDN);
    puts("Retiring PROCESS cannot admit new SFT or standard references: PASS");
    puts("PFS finalizer rebind preserves the replacement standard alias and reference: PASS");
    puts("Production FAT/PFS close ownership, bounded residual recovery and incarnation protection: PASS");
    return 0;
}
