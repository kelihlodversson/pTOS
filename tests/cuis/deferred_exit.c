/* Real Pterm/scheduler ordering with disk errors in owner/child cleanup. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "fs.h"
#include "proc.h"
#include "procmem.h"
#include "mem.h"
#include "bdosstub.h"
#include "../../bdos/arch/x86_64/native_sched.c"

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"deferred exit:%d: %s\n",__LINE__,#c); abort(); } } while (0)
static PD owner, parent, child, successor, other;
static KERNEL_TASK tasks[KSCHED_LIMIT];
static SBYTE owner_files[NUMSTD], child_files[NUMSTD];
static UBYTE owner_dirs[NUMCURDIR], child_dirs[NUMCURDIR];
static BOOL owner_record, child_record, owner_memory, nested, returned_error;
static unsigned references[4], directories, close_errors, injected, attempts;
static unsigned fail_handle, invalidations, term_handlers, completions;
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
UBYTE *kproc_curdir(PD *p)
{
    CHECK((p == &owner && owner_record) || (p == &child && child_record));
    return p == &owner ? owner_dirs : child_dirs;
}
PD *kproc_get_parent(PD *p) { CHECK(p == &owner && owner_record); return &parent; }
WORD kproc_pid(PD *p) { CHECK(p == &owner && owner_record); return 11; }
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
    CHECK(!references[0] && !references[1] && !references[3] && directories == 2);
    owner_record = FALSE;
}
BOOL kproc_unlaunched(PD *p) { return p == &child && child_record; }
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
    CHECK(references[handle-NUMSTD] == 1);
    references[handle-NUMSTD]--;
    if (handle == NUMSTD+3) sft[3].f_own = NULL;
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
    CHECK(tasks[1].state == TASK_NEW && !tasks[1].deferred_owner);
    CHECK(tasks[2].state == TASK_DEFERRED && tasks[2].deferred_owner == &other);
    CHECK(completions == (unsigned)nested);
    if (!nested) CHECK(tasks[0].result == 37 && tasks[0].usage[0] == 8);
    longjmp(exit_resume,1);
}

static void exit_check(void)
{
    unsigned i;
    memset(tasks,0,sizeof(tasks));
    scheduler.tasks = tasks; scheduler.active = TRUE;
    tasks[0].program = nested ? &parent : &owner;
    tasks[0].state = TASK_RUNNING; tasks[0].asynchronous = TRUE;
    tasks[1].program = &successor; tasks[1].deferred_owner = &owner; tasks[1].state = TASK_DEFERRED;
    tasks[2].deferred_owner = &other; tasks[2].state = TASK_DEFERRED;
    owner_files[0] = NUMSTD; owner_files[1] = NUMSTD+1; child_files[0] = NUMSTD+2;
    sft[3].f_own = &owner;
    owner_dirs[0] = 3; owner_dirs[2] = 4; child_dirs[0] = 5;
    for (i = 0; i < 4; i++) references[i] = 1;
    for (i = 3; i <= 5; i++) dirtbl[i].use = 1;
    owner_record = child_record = owner_memory = TRUE;
    directories = injected = invalidations = attempts = term_handlers = completions = 0;
    run = &owner;
    if (!setjmp(exit_resume)) {
        /* Returning to osif's disk-error handler would resume the caller. */
        CHECK(!setjmp(errbuf));
        xterm(37);
    }
    CHECK(injected == close_errors && invalidations == close_errors && attempts == 4+close_errors);
}
int main(void)
{
    unsigned nesting, returned;
    for (nesting = 0; nesting < 2; nesting++) {
        nested = nesting;
        for (returned = 0; returned < 2; returned++) {
            returned_error = returned;
            for (fail_handle = NUMSTD; fail_handle < NUMSTD+4; fail_handle++)
                for (close_errors = 0; close_errors <= 2; close_errors++) exit_check();
        }
    }
    puts("Deferred successor waits for complete owner/child cleanup despite close errors: PASS");
    return 0;
}
