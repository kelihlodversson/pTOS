/* Exercise Pexec's deferred rejection with real disposal and disk wrappers. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "string.h"
#include "fs.h"
#include "gemerror.h"
#include "bdosstub.h"
#include "mem.h"
#include "proc.h"
#include "kproc.h"
#include "ksched.h"
#include "procmem.h"
#include "process.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"deferred rollback:%d: %s\n",__LINE__,#c); abort(); } } while (0)

/* Static, non-PIE blocks fit in the GEMDOS basepage's 32-bit pointers. */
static PD parent, pending;
static UBYTE image_area[sizeof(PD)+16] __attribute__((aligned(16)));
#define image (*(PD *)image_area)
static char environment[128];
static const void *owners[2];
static SBYTE parent_files[NUMSTD], child_files[NUMSTD];
static UBYTE parent_dirs[NUMCURDIR], child_dirs[NUMCURDIR];
static unsigned allocations, records, disposals, attempts, interrupted;
static unsigned references[2], invalidations, directory_releases;
static unsigned inject_errors;
static BOOL return_close_error;
static BOOL fake_completion;
static unsigned completion_stage;
static PD *pending_request;
static LONG rejection;

jmp_buf errbuf;
long errcode;
int errdrv;
FTAB sft[OPNFILES];
DIRTBL_ENTRY dirtbl[NCURDIR];
MPB pmd, pmdalt;
int has_alt_ram;

void bzero(void *p, size_t size) { memset(p,0,size); }
void *procmem_alloc(ULONG bytes, UWORD flags)
{
    void *block = allocations ? (void *)&image : (void *)environment;
    CHECK(allocations < 2 && flags == PROCMEM_ZERO);
    CHECK(bytes <= (allocations ? sizeof(image_area) : sizeof(environment)));
    memset(block,0,bytes);
    owners[allocations++] = NULL;
    return block;
}
BOOL procmem_contains(const void *p) { return p == &image || p == environment; }
BOOL procmem_free(void *p) { CHECK(procmem_contains(p) && allocations); allocations--; return TRUE; }
const void *procmem_owner(const void *p) { return owners[p == &image ? 1 : 0]; }
ULONG procmem_pins(const void *p) { return 0; }
void set_owner(void *p, PD *owner) { CHECK(procmem_contains(p)); owners[p == &image ? 1 : 0] = owner; }
void procmem_free_owned(const void *owner, void (*before)(void *))
{
    CHECK(owner == &image && !records && allocations == 2);
    CHECK(owners[0] == owner && owners[1] == owner);
    before(environment); before(&image);
    allocations = 0;
    disposals++;
}
void procmem_keep(const void *p, ULONG bytes, void (*before)(void *))
{ (void)p; (void)bytes; (void)before; abort(); }
SBYTE *kproc_uft(PD *p) { CHECK(p == &parent || (p == &image && records)); return p == &parent ? parent_files : child_files; }
void kproc_standard_file_refs(ULONG *refs)
{
    unsigned i;
    memset(refs,0,OPNFILES*sizeof(*refs));
    for (i = 0; i < NUMSTD; i++) {
        if (parent_files[i] >= NUMSTD) refs[parent_files[i]-NUMSTD]++;
        if (records && child_files[i] >= NUMSTD) refs[child_files[i]-NUMSTD]++;
    }
}
UBYTE *kproc_curdir(PD *p) { CHECK(p == &parent || (p == &image && records)); return p == &parent ? parent_dirs : child_dirs; }
BOOL kproc_create(PD *p) { CHECK(p == &image && !records); records++; return TRUE; }
void kproc_destroy(PD *p)
{
    CHECK(p == &image && records == 1);
    /* A close error must not jump past the remaining references. */
    CHECK(references[0] == 2 && references[1] == 1);
    CHECK(directory_releases == 2 && dirtbl[3].use == 1 && dirtbl[4].use == 1);
    records--;
}
BOOL kproc_unlaunched(PD *p) { return p == &image && records; }
KPROC *kproc_retain(PD *p) { return (KPROC *)p; }
void kproc_release(KPROC *p) { (void)p; }
BOOL kproc_begin_disposal(KPROC *p, PD **pd) { *pd = (PD *)p; return TRUE; }
BOOL kproc_pending_completion(const KPROC *p, UWORD *status)
{
    CHECK(p == (KPROC *)&image);
    if (!fake_completion) return FALSE;
    *status = 37;
    return TRUE;
}
void kproc_complete(PD *p, UWORD status)
{
    CHECK(fake_completion && p == &image && status == 37 && !completion_stage++);
}
void ksched_exiting(PD *p, UWORD status)
{
    CHECK(fake_completion && p == &image && status == 37 && completion_stage++ == 1 && records);
}
void ksched_exited(PD *p)
{
    CHECK(fake_completion && p == &image && completion_stage++ == 2 && !records && !allocations);
}
void kproc_require_no_threads(PD *p) { (void)p; }
void x86_64_memtest_disposal_committed(PD *p) { (void)p; }
BOOL kproc_discard(PD *p) { CHECK(!records); return FALSE; }
BOOL kproc_borrow(PD *p, void *block) { CHECK(p == &parent && procmem_contains(block)); return TRUE; }
void kproc_unborrow(void *block) { CHECK(procmem_contains(block)); }
ULONG kproc_borrow_count(PD *p, void *block) { return 0; }
void kproc_set_executable_name(PD *p, const char *path) { CHECK(p == &image && path); }

long ixforce(int standard, int handle, PD *p)
{
    CHECK(p == &image && handle >= NUMSTD && handle < NUMSTD+2);
    child_files[standard] = handle;
    references[handle-NUMSTD]++;
    return E_OK;
}
void decr_curdir_usage(int index)
{
    CHECK(index == 3 || index == 4);
    CHECK(dirtbl[index].use == 2);
    dirtbl[index].use--;
    directory_releases++;
}
long xclose(WORD handle)
{
    CHECK(handle >= NUMSTD && handle < NUMSTD+2);
    attempts++;
    if (interrupted < inject_errors) {
        errdrv = interrupted++;
        errcode = EWRITF;
        longjmp(errbuf,1);  /* Handle still live; the disk wrapper must retry. */
    }
    CHECK(references[handle-NUMSTD] > 0);
    references[handle-NUMSTD]--;
    /* A returned error already released its reference and must not retry. */
    return return_close_error ? EWRITF : E_OK;
}
void mark_bcbs_invalid(int drive) { CHECK(drive == (int)invalidations); invalidations++; }
long ixsfirst(char *path, WORD attr, DTAINFO *dta) { return E_OK; }
LONG kpgmhdrld(char *path, PGMHDR01 *header, FH *handle)
{
    memset(header,0,sizeof(*header));
    *handle = NUMSTD+2;
    return E_OK;
}
LONG kpgmld(PD *p, FH handle, PGMHDR01 *header, const char *path)
{
    CHECK(p == &image && records == 1 && handle == NUMSTD+2);
    return E_OK;
}
BOOL ksched_spawn_allowed(void) { return TRUE; }
LONG ksched_defer(PD *p, ULONG session)
{
    CHECK(p == &image && session == 23 && pending_request == &pending);
    CHECK(references[0] == 4 && references[1] == 2);
    CHECK(dirtbl[3].use == 2 && dirtbl[4].use == 2);
    return rejection; /* No task slot, or preparation failed before commit. */
}
LONG fs_set_process_directory(char *directory, PD *process)
{
    CHECK(process == &image && !strcmp(directory,"C:\\MISSING"));
    CHECK(run == &parent && pending_request == &pending);
    return EPTHNF;
}
void elf_set_load_into_caller(BOOL wanted) { CHECK(!wanted); }
void proc_align_stack(PD *p) { CHECK(p == &image); }
void invalidate_instruction_cache(const UBYTE *address, ULONG length) { }

/* Other Pexec modes must not run during this regression. */
BOOL ksched_active(void) { abort(); }
LONG ksched_spawn(PD *p) { abort(); }
LONG ksched_spawn_observed(PD *p, ULONG session, ULONG token) { abort(); }
LONG ksched_exec(PD *p, BOOL take, LONG *result) { abort(); }
BOOL kproc_check_launch(PD *p, PD *caller) { abort(); }
ASPACE *kproc_user_aspace(PD *p) { abort(); }
UQUAD kproc_user_root(PD *p) { abort(); }
BOOL kproc_prepare_user(PD *p, PD *caller) { abort(); }
void kproc_set_creator(PD *p, PD *caller) { abort(); }
void kproc_hand_over(PD *caller, PD *p) { abort(); }
void kproc_set_give_back(PD *p, BOOL give_back) { abort(); }
void kproc_set_parent(PD *p, PD *caller) { abort(); }
void *kproc_clone_startup(PD *p) { abort(); }
UQUAD kproc_ancestors_va(PD *p) { abort(); }
void kproc_mark_started(PD *p) { abort(); }
void proc_launch_prepare(PD *p, void *stack) { abort(); }
void gouser_entry(WORD entry) { abort(); }
void freeit(MD *block, MPB *pool) { abort(); }
long xmfree(void *block) { abort(); }
int kcprintf(const char *format, ...) { abort(); }

int main(void)
{
    unsigned errors, returned_error, failure;
    static const LONG failures[] = { ENSMEM, EACCDN };
    char tail[2] = { 0,0 }, env[2] = { 0,0 };
    run = &parent;
    parent_files[0] = parent_files[1] = NUMSTD;
    parent_files[2] = NUMSTD+1;
    parent_dirs[0] = 3; parent_dirs[2] = 4;
    pending_request = &pending;
    for (failure = 0; failure < ARRAY_SIZE(failures); failure++) {
        rejection = failures[failure];
        for (errors = 0; errors <= 2; errors++) {
            for (returned_error = 0; returned_error < 2; returned_error++) {
                CHECK(!allocations && !records);
                references[0] = 2; references[1] = 1;
                dirtbl[3].use = dirtbl[4].use = 1;
                directory_releases = attempts = interrupted = invalidations = 0;
                inject_errors = errors; return_close_error = returned_error;
                CHECK(xexec_deferred("NEXT.PRG",tail,env,23) == rejection);
                CHECK(pending_request == &pending && run == &parent);
                CHECK(!allocations && !records && attempts == errors+3);
                CHECK(interrupted == errors && invalidations == errors);
            }
        }
    }
    CHECK(disposals == 12);
    references[0] = 2; references[1] = 1;
    dirtbl[3].use = dirtbl[4].use = 1;
    directory_releases = attempts = interrupted = invalidations = 0;
    inject_errors = 0; return_close_error = FALSE;
    CHECK(xexec_directory("NEXT.PRG",tail,env,23,0,TRUE,"C:\\MISSING") == EPTHNF);
    CHECK(pending_request == &pending && run == &parent);
    CHECK(!allocations && !records && disposals == 13);
    /* Exercise notification ordering for an already committed completion;
     * the real KPROC tests cover the started/stopped eligibility checks. */
    fake_completion = TRUE;
    references[0] = 2; references[1] = 1;
    dirtbl[3].use = dirtbl[4].use = 1;
    directory_releases = attempts = interrupted = invalidations = 0;
    CHECK(xexec_deferred("NEXT.PRG",tail,env,23) == rejection);
    CHECK(completion_stage == 3 && !allocations && !records);
    puts("Deferred rejection releases all references after interrupted close: PASS");
    return 0;
}

PD *ksched_launch_controller(PD *p) { (void)p; return NULL; }
