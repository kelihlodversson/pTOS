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
long xclose(WORD handle)
{
    CHECK(handle == NUMSTD || handle == NUMSTD + 1);
    if (handle == NUMSTD) {
        CHECK(references == 2);
        first_closes++;
        references--;
        return E_OK;
    }
    CHECK(references == 1);
    if (++attempts == 1 || exhaust) {
        errdrv = 0;
        errcode = EWRITF;
        longjmp(errbuf, 1);
    }
    references--;
    return E_OK;
}
int main(void)
{
    record = allocations = 1;
    references = 2;
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
    sft[1].f_own = &child;
    sft[1].f_use = 1;
    proc_free_owned(&owner);
    CHECK(!allocations && !record && references == 1 && first_closes == 1 &&
          attempts == 2 * (BLKDEVNUM + 1) && invalidations == 2 * (BLKDEVNUM + 1));
    CHECK(sft[1].f_own && sft[1].f_own != &child);
    /* The final interrupted close still owns its reference. A later successful
     * close releases it once; the earlier handle is never closed twice. */
    exhaust = FALSE;
    CHECK(kdos_fclose(NUMSTD + 1) == E_OK && !references && first_closes == 1);
    puts("Exhausted close retries leave one reference without double release: PASS");
    return 0;
}
