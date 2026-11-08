/* Real Pterm/scheduler ordering with disk errors in owner/child cleanup. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "fs.h"
#include "proc.h"
#include "procmem.h"
#include "mem.h"
#include "bdosstub.h"
#include "scheduler_irq_model.h"
#include "../../bdos/native_sched.c"

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"deferred exit:%d: %s\n",__LINE__,#c); abort(); } } while (0)
static PD owner, parent, child, successor, other;
static KERNEL_TASK tasks[KSCHED_LIMIT];
static SBYTE owner_files[NUMSTD], child_files[NUMSTD], successor_files[NUMSTD];
static UBYTE owner_dirs[NUMCURDIR], child_dirs[NUMCURDIR];
static BOOL owner_record, child_record, owner_memory, nested, returned_error, parent_retired;
static unsigned references[4], directories, close_errors, injected, attempts;
static unsigned fail_handle, invalidations, term_handlers, completions;
static unsigned successor_aliases, explicit_open;
static unsigned identity_completions, cleanup_commits;
static unsigned thread_destroys;
static jmp_buf exit_resume;

jmp_buf errbuf;
long errcode;
int errdrv;
FTAB sft[OPNFILES];
DIRTBL_ENTRY dirtbl[NCURDIR];
MPB pmd, pmdalt;
int has_alt_ram;

static void still_deferred(void)
{
    CHECK(tasks[1].state == TASK_DEFERRED && tasks[1].deferred_owner == &owner);
    CHECK(tasks[2].state == TASK_DEFERRED && tasks[2].deferred_owner == &other);
}
SBYTE *kproc_uft(PD *p)
{
    CHECK((p == &owner && owner_record) || (p == &child && child_record));
    return p == &owner ? owner_files : child_files;
}
void kproc_standard_file_refs(ULONG *refs)
{
    unsigned i;
    memset(refs,0,OPNFILES*sizeof(*refs));
    for (i = 0; i < NUMSTD; i++) {
        if (owner_record && owner_files[i] >= NUMSTD) refs[owner_files[i]-NUMSTD]++;
        if (child_record && child_files[i] >= NUMSTD) refs[child_files[i]-NUMSTD]++;
        if (successor_files[i] >= NUMSTD) refs[successor_files[i]-NUMSTD]++;
    }
}
UBYTE *kproc_curdir(PD *p)
{
    CHECK((p == &owner && owner_record) || (p == &child && child_record));
    return p == &owner ? owner_dirs : child_dirs;
}
PD *kproc_get_parent(PD *p) { CHECK(p == &owner && owner_record); return &parent; }
PD *kproc_notification_parent(PD *p)
{
    return parent_retired ? NULL : kproc_get_parent(p);
}
WORD kproc_pid(PD *p) { CHECK(p == &owner && owner_record); return 11; }
void kproc_publish_observers(KPROC *p) { (void)p; }
void kproc_complete(PD *p, UWORD status)
{
    CHECK(p == &owner && owner_record && status == 37 && cleanup_commits == 1);
    CHECK(!identity_completions++);
    still_deferred();
}
void kproc_usage(PD *p, ULONG usage[8])
{
    CHECK(p == &owner && owner_record); still_deferred();
    memset(usage,0,8*sizeof(*usage)); usage[1] = 3; usage[3] = 5;
}
void cuis_child_completed(PD *p, WORD pid, UWORD status)
{
    CHECK(nested && p == &parent && pid == 11 && status == 37);
    still_deferred(); completions++;
}
void kproc_destroy(PD *p)
{
    still_deferred();
    CHECK(p == &owner && owner_record && run == &parent);
    CHECK(references[0] == successor_aliases && !references[1] && !references[3] && directories == 2);
    owner_record = FALSE;
}
BOOL kproc_unlaunched(PD *p) { return p == &child && child_record; }
KPROC *kproc_retain(PD *p) { return (KPROC *)p; }
void kproc_release(KPROC *p) { (void)p; }
BOOL kproc_begin_disposal(KPROC *p, PD **pd) { *pd = (PD *)p; return TRUE; }
BOOL kproc_pending_completion(const KPROC *p, UWORD *status) { (void)p; (void)status; return FALSE; }
BOOL kproc_begin_exit(PD *p)
{
    CHECK(p == &owner && owner_record && !identity_completions && !attempts);
    CHECK(!cleanup_commits++);
    return TRUE;
}
void kproc_require_no_threads(PD *p) { (void)p; }
KPROC *kthread_process(const KTHREAD *t) { (void)t; abort(); }
BOOL kproc_thread_is_sole(KPROC *p, KTHREAD *t) { (void)p; (void)t; abort(); }
void kproc_require_exit_thread(PD *p, KTHREAD *thread) { (void)p; CHECK(!thread); }
BOOL kthread_is_executing(const KTHREAD *thread) { (void)thread; return FALSE; }
void x86_64_memtest_disposal_committed(PD *p) { (void)p; }
BOOL proc_defer_exit(PD *p, UWORD status, BOOL resident, ULONG len)
{ (void)p; (void)status; (void)resident; (void)len; return FALSE; }
void kthread_destroy(KTHREAD *thread)
{
    CHECK(thread == (KTHREAD *)&successor && tasks[1].state == TASK_DEFERRED);
    thread_destroys++;
}
void panic(const char *fmt, ...) { (void)fmt; abort(); }
BOOL kproc_discard(PD *p)
{
    still_deferred();
    if (p == &child) {
        CHECK(child_record && !references[2] && directories == 3);
        child_record = FALSE;
        return TRUE;
    }
    CHECK(p == &owner && !owner_record);
    return FALSE;
}
void kproc_unborrow(void *p) { CHECK(p == &child || p == &owner); still_deferred(); }
const void *procmem_owner(const void *p) { abort(); }
void procmem_keep(const void *p, ULONG bytes, void (*before)(void *)) { abort(); }
void procmem_free_owned(const void *p, void (*before)(void *))
{
    still_deferred();
    CHECK(p == &owner && !owner_record && child_record && owner_memory);
    before(&child); before(&owner);
    CHECK(!child_record && !references[2] && directories == 3);
    owner_memory = FALSE;
}
void freeit(MD *block, MPB *pool) { abort(); }
long xclose(WORD handle)
{
    still_deferred();
    CHECK(handle >= NUMSTD && handle < NUMSTD+4 && run == &parent);
    attempts++;
    if ((unsigned)handle == fail_handle && injected < close_errors) {
        errdrv = injected++; errcode = EWRITF;
        longjmp(errbuf,1);
    }
    CHECK(references[handle-NUMSTD] > 0);
    references[handle-NUMSTD]--;
    sft[handle-NUMSTD].f_use--;
    if (!sft[handle-NUMSTD].f_use) sft[handle-NUMSTD].f_own = NULL;
    return returned_error ? EWRITF : E_OK;
}
void mark_bcbs_invalid(int drive) { CHECK(drive == (int)invalidations); invalidations++; }
void decr_curdir_usage(int index)
{
    still_deferred(); CHECK(index >= 3 && index <= 5);
    CHECK(dirtbl[index].use == 1); dirtbl[index].use--; directories++;
}
static LONG termination_handler(void)
{
    still_deferred(); CHECK(run == &owner && owner_record && owner_memory);
    term_handlers++; return 0;
}
long kernel_trap(long call, long a, long b, long c, long d)
{
    CHECK(call == ((13L << 32) | 5) && a == 0x102 && b == -1 && !c && !d);
    return (long)termination_handler;
}
void termuser(void)
{
    CHECK(run == &parent && parent.p_dreg[0] == 37);
    CHECK(!owner_record && !child_record && !owner_memory && term_handlers == 1);
    CHECK(identity_completions == 1 && cleanup_commits == 1);
    CHECK(tasks[1].state == TASK_NEW && !tasks[1].deferred_owner);
    CHECK(tasks[1].thread == (KTHREAD *)&successor && !thread_destroys);
    CHECK(tasks[2].state == TASK_DEFERRED && tasks[2].deferred_owner == &other);
    CHECK(completions == (unsigned)(nested && !parent_retired));
    CHECK(references[0] == successor_aliases && sft[0].f_use == (int)successor_aliases);
    if (successor_aliases)
        CHECK(sft[0].f_own && sft[0].f_own != &owner);
    if (!nested) CHECK(tasks[0].result == 37 && tasks[0].usage[0] == 8);
    longjmp(exit_resume,1);
}

static void exit_check(void)
{
    unsigned i;
    memset(tasks,0,sizeof(tasks));
    scheduler.tasks = tasks; initialize_task_table(); scheduler.active = TRUE;
    tasks[0].program = nested ? &parent : &owner;
    tasks[0].process = (KPROC *)tasks[0].program;
    tasks[0].state = TASK_RUNNING; tasks[0].asynchronous = TRUE;
    tasks[1].program = &successor; tasks[1].deferred_owner = &owner; tasks[1].state = TASK_DEFERRED;
    tasks[1].thread = (KTHREAD *)&successor;
    tasks[2].deferred_owner = &other; tasks[2].state = TASK_DEFERRED;
    memset(owner_files,0,sizeof(owner_files));
    memset(successor_files,0,sizeof(successor_files));
    memset(sft,0,sizeof(sft));
    owner_files[0] = NUMSTD; owner_files[1] = NUMSTD+1; child_files[0] = NUMSTD+2;
    sft[3].f_own = &owner;
    owner_dirs[0] = 3; owner_dirs[2] = 4; child_dirs[0] = 5;
    for (i = 0; i < 4; i++) references[i] = sft[i].f_use = 1;
    if (successor_aliases) {
        /* The same slot belongs to the owner even after its explicit close.
         * Both the owner and the prepared successor have standard aliases. */
        sft[0].f_own = &owner;
        successor_files[1] = NUMSTD;
        if (successor_aliases == 2) {
            owner_files[2] = successor_files[2] = NUMSTD;
            references[0]++;
        }
        references[0] += successor_aliases + explicit_open;
        sft[0].f_use = references[0];
    }
    for (i = 3; i <= 5; i++) dirtbl[i].use = 1;
    owner_record = child_record = owner_memory = TRUE;
    directories = injected = invalidations = attempts = term_handlers = completions = 0;
    identity_completions = cleanup_commits = 0;
    thread_destroys = 0;
    run = &owner;
    if (!setjmp(exit_resume)) {
        /* Returning to osif's disk-error handler would resume the caller. */
        CHECK(!setjmp(errbuf));
        xterm(37);
    }
    CHECK(injected == close_errors && invalidations == close_errors &&
          attempts == 4+close_errors+(successor_aliases == 2)+explicit_open);
}
int main(void)
{
    unsigned nesting, returned;
    for (nesting = 0; nesting < 2; nesting++) {
        nested = nesting;
        for (returned = 0; returned < 2; returned++) {
            returned_error = returned;
            for (successor_aliases = 0; successor_aliases <= 2; successor_aliases++)
                for (explicit_open = 0; explicit_open <= (successor_aliases ? 1 : 0); explicit_open++)
                    for (fail_handle = NUMSTD; fail_handle < NUMSTD+4; fail_handle++)
                        for (close_errors = 0; close_errors <= 2; close_errors++) exit_check();
        }
    }
    nested = TRUE;
    parent_retired = TRUE;
    returned_error = FALSE;
    successor_aliases = explicit_open = close_errors = 0;
    fail_handle = NUMSTD;
    exit_check();
    puts("Deferred successor waits for complete owner/child cleanup despite close errors: PASS");
    puts("Exit sweep preserves successor stdout aliases with explicit handle open or closed: PASS");
    return 0;
}

/* Legacy scheduler fixtures must never acquire clone startup ownership. */
void thread_startup_abort(THREAD_STARTUP_DRAFT **startup)
{
    if (startup && *startup) abort();
}

void thread_startup_reclaim(THREAD_STARTUP_DRAFT **startup)
{
    (void)startup;
    abort(); /* no published clone exists in this legacy fixture */
}

void ksched_admission_owner_exited(KPROC *owner) { (void)owner; }
BOOL ksched_admission_abandoned(const KSCHED_ADMISSION *h) { (void)h; return FALSE; }
void ksched_admission_release(KSCHED_ADMISSION *h) { h->identity=0; }
