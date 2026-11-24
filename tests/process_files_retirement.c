/* Actual KPROC registry/destroy plus canonical descriptor and FS owners.
 * Reuse only the existing hosted absent-MMU and disk/FAT/PFS providers. */
#define main observer_fixture_main
#define OBSERVER_WITH_CLOSE_FIXTURE
#include "process_observer.c"
#undef main
#ifndef PROCESS_SEARCH_FIXTURE
/* This fixture retires descriptors only; its PFS searches are empty. */
void pfs_search_close(KPROC *process,DTAINFO *address)
{ (void)process;(void)address; }
#endif
#define main close_fixture_main
#define run close_fixture_run
#define panic close_fixture_panic
#define get_default_handle close_fixture_default
#define kproc_uft close_fixture_uft
#define kproc_file_references_allowed close_fixture_admission
#define kproc_standard_file_invalidate close_fixture_invalidate
#include "fs_close.c"
#undef kproc_standard_file_invalidate
#undef kproc_file_references_allowed
#undef kproc_uft
#undef get_default_handle
#undef panic
#undef run
#undef main
static unsigned retirement_callbacks;
static LONG retirement_close(PFSCOOKIE *cookie)
{ CHECK(cookie==&sft[0].f_pfs);return E_OK; }
static LONG retirement_release(PFSCOOKIE *cookie)
{
    CHECK(cookie==&sft[0].f_pfs && !kproc_find(&child));
    CHECK(!kproc_file_references_allowed(&child));
    retirement_callbacks++;
    kproc_destroy(&child); /* same PD must already be detached */
    kproc_destroy(&caller); /* preceding list element/cursor may disappear */
    return E_OK;
}
#ifdef PROCESS_SEARCH_FIXTURE
#define main descriptor_retirement_main
#endif
int main(void)
{
    struct pfs_ops operations={0};
    WORD descriptor;
    /* The predecessor owns the live-list link used to locate child. */
    CHECK(kproc_create(&child) && kproc_create(&caller));run=&child;
    CHECK(fs_sft_claim(0));sft[0].f_own=&child;sft[0].f_use=1;
    operations.close=retirement_close;operations.release_file=retirement_release;
    sft[0].f_pfs.fs=&operations;
    descriptor=(WORD)process_files_open_begin(&child,NUMSTD);
    CHECK(descriptor>=NUMSTD && process_files_open_end(&child,descriptor,NUMSTD)==E_OK);
    kproc_destroy(&child);
    CHECK(retirement_callbacks==1 && !kproc_find(&child) && !kproc_find(&caller));
    CHECK(!kproc_list && !retired_processes && allocations==frees);
    CHECK(!sft[0].f_use && !sft[0].f_staged_refs && !sft[0].f_cleanup_refs && !sft[0].f_own);
    puts("Actual KPROC descriptor retirement survives self and predecessor destroy callbacks with exact final ownership: PASS");
    return 0;
}
