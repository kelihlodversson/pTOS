/* Real proc_free_owned()/kdos_fclose() cleanup with an interrupted close. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "gemerror.h"
#include "fs.h"
#include "mem.h"
#include "proc.h"
#include "kproc.h"
#include "procmem.h"
#include "bdosstub.h"
#include "string.h"

#if CONF_WITH_NATIVE_SCHED
static FS_ERROR_STATE fixture_error;
FS_ERROR_STATE *fs_error_current(void) { return &fixture_error; }
#else
FS_ERROR_STATE fs_legacy_error;
#endif
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"non-CUIS cleanup:%d: %s\n",__LINE__,#c); abort(); } } while (0)
static PD owner, child;
static SBYTE child_files[NUMSTD];
static UBYTE child_dirs[NUMCURDIR];
static unsigned record, allocations, references, attempts, invalidations, directories;
static unsigned first_closes;
static BOOL exhaust;
static unsigned transferred;
void panic(const char *format, ...) { fprintf(stderr,"panic: %s\n",format); abort(); }
SBYTE get_default_handle(int i) { (void)i; return -1; }
void kproc_note_file_cleanup(PD *pd,LONG error,BOOL transfer)
{ CHECK(pd==&child); if (transfer) { CHECK(error==EWRITF); transferred++; } }
void kproc_standard_file_invalidate(WORD handle)
{ unsigned i; for(i=0;i<NUMSTD;i++) if(child_files[i]==handle) child_files[i]=-1; }
void kproc_standard_file_refs(ULONG *refs)
{ unsigned i; memset(refs,0,OPNFILES*sizeof(*refs));
  for (i=0;i<NUMSTD;i++) if (child_files[i]>=NUMSTD) refs[child_files[i]-NUMSTD]++; }

FTAB sft[OPNFILES];
DIRTBL_ENTRY dirtbl[NCURDIR];

SBYTE *kproc_uft(PD *pd) { CHECK(pd == &child && record); return child_files; }
UBYTE *kproc_curdir(PD *pd) { CHECK(pd == &child && record); return child_dirs; }
BOOL kproc_unlaunched(PD *pd) { return pd == &child && record; }
BOOL kproc_discard(PD *pd) { CHECK(pd == &child && record); record = 0; return TRUE; }
void kproc_unborrow(void *base) { CHECK(base == &child && !record); }
void procmem_free_owned(const void *pd, void (*before)(void *))
{
    CHECK(pd == &owner && allocations == 1);
    before(&child);
    CHECK(!record && references == (exhaust ? 1U : 0U) && directories == 1);
    allocations = 0;
}
void decr_curdir_usage(int index) { CHECK(index == 3); directories++; }
void mark_bcbs_invalid(int drive) { CHECK(drive == 0); invalidations++; }
long fs_close_backend(WORD handle,volatile BOOL *consumed)
{
    CHECK(handle == NUMSTD || handle == NUMSTD + 1);
    if (handle == NUMSTD) {
        CHECK(references == 2);
        first_closes++;
        references--;
        sft[0].f_use=0; sft[0].f_own=NULL; *consumed=TRUE;
        return E_OK;
    }
    CHECK(references == 1);
    if (++attempts == 1 || exhaust) {
        errdrv = 0;
        errcode = EWRITF;
        longjmp(errbuf, 1);
    }
    references--;
    sft[1].f_use=0; sft[1].f_own=NULL; *consumed=TRUE;
    return E_OK;
}
int main(void)
{
    record = allocations = 1;
    references = 2;
    CHECK(fs_sft_claim(0) && fs_sft_claim(1));
    sft[0].f_own=sft[1].f_own=&child; sft[0].f_use=sft[1].f_use=1;
    child_files[0] = NUMSTD;
    child_files[1] = NUMSTD + 1;
    child_dirs[0] = 3;
    proc_free_owned(&owner);
    CHECK(!allocations && !record && !references && first_closes == 1 &&
          attempts == 2 && invalidations == 1);
    puts("Non-CUIS process cleanup completes after a disk-error longjmp: PASS");
    record = allocations = 1;
    references = 2;
    attempts = invalidations = directories = first_closes = 0;
    exhaust = TRUE;
    CHECK(fs_sft_claim(0) && fs_sft_claim(1));
    sft[0].f_own=sft[1].f_own=&child; sft[0].f_use=sft[1].f_use=1;
    child_files[0]=NUMSTD; child_files[1]=NUMSTD+1;
    proc_free_owned(&owner);
    CHECK(!allocations && !record && references == 1 && first_closes == 1 &&
          attempts == BLKDEVNUM + 1 && invalidations == BLKDEVNUM + 1 && transferred == 1);
    CHECK(sft[1].f_own && sft[1].f_own != &child);
    /* The final interrupted close still owns its reference. A later successful
     * close releases it once; the earlier handle is never closed twice. */
    exhaust = FALSE;
    {
        FS_CLOSE_REPORT result;
        FS_CLEANUP_TOKEN token;
        FS_CLEANUP_STATUS status;
        token.handle=NUMSTD+1; token.incarnation=sft[1].f_incarnation;
        token.reference=sft[1].f_cleanup_issued;
        CHECK(fs_cleanup_snapshot(&token,&status)==E_OK && status.references==1);
        CHECK(kdos_fclose(NUMSTD+1)==EIHNDL && references==1);
        CHECK(fs_cleanup_retry(&token,&result)==E_OK && !references && first_closes==1);
        CHECK(fs_cleanup_retry(&token,&result)==EIHNDL && !references);
    }
    /* Production invalidation bookkeeping must unpublish aliases before reuse.
     * Then actual proc_free_owned must leave the replacement reference alone. */
    record=allocations=1; references=0; exhaust=FALSE;
    CHECK(fs_sft_claim(1)); sft[1].f_own=&child; sft[1].f_use=1;
    child_files[0]=NUMSTD+1; child_files[1]=0;
    fs_sft_invalidated(1); sft[1].f_own=NULL; sft[1].f_use=0;
    CHECK(child_files[0]<0 && fs_sft_claim(1));
    sft[1].f_own=&owner; sft[1].f_use=1;
    directories=0;
    proc_free_owned(&owner);
    CHECK(!allocations && !record && !references && sft[1].f_own==&owner && sft[1].f_use==1);
    puts("Exhausted close retries leave one reference without double release: PASS");
    return 0;
}

void kproc_discard_after_free(PD *pd) { (void)kproc_discard(pd); }
