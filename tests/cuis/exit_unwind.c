/* Exercise the production finalization boundary, including forbidden unwinds. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "fs.h"
#include "proc.h"
#include "kproc.h"
#include "mem.h"
#include "procmem.h"
#include "../../bdos/proc.c"
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"exit unwind:%d: %s\n",__LINE__,#c); abort(); } } while (0)
static PD child;
static SBYTE files[NUMSTD];
static UBYTE dirs[NUMCURDIR];
static unsigned phase, commits, publications, releases, destroyed, successors;
static BOOL disposing, managed_completion, wait_visible;
static unsigned observer_publications;
static jmp_buf fatal, original_error;
jmp_buf errbuf;
long errcode;
int errdrv;
FTAB sft[OPNFILES];
DIRTBL_ENTRY dirtbl[NCURDIR];
MPB pmd, pmdalt;
int has_alt_ram;
static void inject(unsigned at) { if (phase == at) longjmp(errbuf, 1); }
BOOL kproc_commit_exit_handoff(KPROC *p, PD **pd)
{
    CHECK(p == (KPROC *)&child && !disposing);
    disposing = TRUE; commits++; *pd = &child; return TRUE;
}
BOOL ksched_finalizing_process(const KPROC *p)
{ CHECK(p==(KPROC *)&child); return managed_completion; }
void kproc_publish_observers(KPROC *p)
{ CHECK(p==(KPROC *)&child && destroyed && successors && (!managed_completion || wait_visible)); observer_publications++; }
void kproc_complete(PD *p, UWORD status)
{ CHECK(p == &child && status == 37 && disposing); publications++; }
void ksched_exiting(PD *p, UWORD status)
{ CHECK(p == &child && status == 37); inject(1); }
void ksched_exited_identity(KPROC *p, PD *pd)
{ CHECK(p == (KPROC *)&child && pd == &child && destroyed); successors++; inject(4); }
void kproc_release(KPROC *p) { CHECK(p == (KPROC *)&child && disposing); releases++; }
void panic(const char *fmt, ...)
{
    CHECK(strstr(fmt, "cleanup callback escaped") != NULL);
    CHECK(!proc_resident_pd && !proc_resident_len && releases == 1);
    longjmp(fatal, 1);
}
SBYTE *kproc_uft(PD *p) { CHECK(p == &child && !destroyed); return files; }
UBYTE *kproc_curdir(PD *p) { CHECK(p == &child && !destroyed); return dirs; }
void kproc_standard_file_refs(ULONG *refs) { memset(refs, 0, OPNFILES * sizeof(*refs)); }
LONG fs_close_bounded(WORD h,BOOL dispose,FS_CLOSE_REPORT *out)
{ (void)h;(void)dispose;(void)out; abort(); }
void kproc_note_file_cleanup(PD *p,LONG error,BOOL transferred)
{ (void)p;(void)error;(void)transferred; abort(); }
long kdos_fclose(long handle) { (void)handle; abort(); }
void decr_curdir_usage(int n) { CHECK(n == 1); inject(2); }
void kproc_destroy(PD *p) { CHECK(p == &child && disposing); destroyed++; inject(3); }
void procmem_free_owned(const void *p, void (*before)(void *)) { CHECK(p == &child); (void)before; }
BOOL kproc_unlaunched(PD *p) { (void)p; return FALSE; }
BOOL kproc_discard(PD *p) { (void)p; return FALSE; }
void kproc_unborrow(void *base) { (void)base; }
void freeit(MD *m, MPB *mpb) { (void)m; (void)mpb; abort(); }
const void *procmem_owner(const void *p) { CHECK(p == &child); return p; }
void procmem_keep(const void *p, ULONG len, void (*before)(void *))
{ CHECK(p == &child && len == 4096); (void)before; }
int main(void)
{
    /* Re-entered cleanup is never attempted, even after notification or
     * retirement. The retained identity, not a reconstructed PD, is released. */
    unsigned scheduler_case;
    for (scheduler_case=0;scheduler_case<2;scheduler_case++)
    for (phase = 0; phase <= 4; phase++) {
        commits = publications = releases = destroyed = successors = 0;
        disposing = FALSE; observer_publications=0;
        managed_completion=scheduler_case!=0; wait_visible=FALSE;
        dirs[0] = 1;
        memset(errbuf, 0x5a, sizeof(errbuf));
        memcpy(original_error, errbuf, sizeof(errbuf));
        if (!setjmp(fatal)) {
            proc_finalize_exit((KPROC *)&child, 37, TRUE, 4096);
            CHECK(!phase && !releases && !proc_resident_pd);
            CHECK(observer_publications==(managed_completion ? 0U:1U));
            if (managed_completion) {
                wait_visible=TRUE; /* Existing scheduler ZOMBIE/DONE transition. */
                kproc_publish_observers((KPROC *)&child);
            }
            CHECK(observer_publications==1);
            kproc_release((KPROC *)&child);
        } else {
            CHECK(phase && !proc_resident_pd && !proc_resident_len && !observer_publications);
        }
        CHECK(!memcmp(original_error, errbuf, sizeof(errbuf)));
        CHECK(commits == 1 && publications == 1 && releases == 1);
        CHECK(successors == (phase == 0 || phase == 4));
    }
    puts("Finalization callback unwind contract: PASS");
    return 0;
}

void kproc_discard_after_free(PD *pd) { (void)kproc_discard(pd); }
