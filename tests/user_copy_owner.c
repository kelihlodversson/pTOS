/* Real PROCESS, mapping and lease owners. Reuse counted MMU/file endpoints;
 * this checks operation rights and rollback, not native CPU execution. */
#define main mapping_owner_fixture_main
#include "shared_root_mapping.c"
#undef main

static ULONG stored;
static BOOL fail_copy;
BOOL aspace_user_range_ok(const ASPACE *as,VADDR first,VADDR bytes,BOOL write)
{
    VADDR page,last;
    if(!bytes || first>=USER_VA_LIMIT || bytes>USER_VA_LIMIT-first)return FALSE;
    last=(first+bytes-1)&~(VADDR)(USER_PAGE_SIZE-1);
    for(page=first&~(VADDR)(USER_PAGE_SIZE-1);;page+=USER_PAGE_SIZE) {
        UWORD rights;
        if(!aspace_translate(as,page,NULL,&rights) || !(rights&ASPACE_PROT_USER) ||
           (write && !(rights&ASPACE_PROT_WRITE)))return FALSE;
        if(page==last)return TRUE;
    }
}
BOOL aspace_copy_to_user(const ASPACE *as,VADDR address,const void *source,ULONG bytes)
{
    CHECK(!kowner_locked());
    if(fail_copy || bytes!=sizeof(stored) || !aspace_user_range_ok(as,address,bytes,TRUE))return FALSE;
    memcpy(&stored,source,bytes);return TRUE;
}
BOOL aspace_copy_from_user(const ASPACE *as,void *destination,VADDR address,ULONG bytes)
{
    CHECK(!kowner_locked());
    if(fail_copy || bytes!=sizeof(stored) || !aspace_user_range_ok(as,address,bytes,FALSE))return FALSE;
    memcpy(destination,&stored,bytes);return TRUE;
}
int kcprintf(const char *message,...) { (void)message;abort();return 0; }
void xterm(UWORD status) { (void)status;abort(); }
#if CONF_WITH_NATIVE_SCHED
/* Only CPU/continuation endpoints are modeled, never PROCESS/mapping owners. */
static UQUAD scope_clock,scope_top,actor=1;
UQUAD ksched_context_id(void) { return actor; }
void procmem_pin(VADDR address,VADDR bytes,int delta)
{ (void)address;(void)bytes;(void)delta;abort(); }
BOOL ksched_pid_reserved(WORD pid) { (void)pid;return FALSE; }
void ksched_cleanup_begin(KSCHED_CLEANUP_SCOPE *scope)
{
    CHECK(!kowner_locked() && !scope->active);
    scope->ticket=++scope_clock;scope->parent_ticket=scope_top;
    scope_top=scope->ticket;scope->active=TRUE;
}
void ksched_cleanup_end(KSCHED_CLEANUP_SCOPE *scope)
{
    CHECK(!kowner_locked() && scope->active && scope_top==scope->ticket);
    scope_top=scope->parent_ticket;memset(scope,0,sizeof(*scope));
}
BOOL ksched_wait_ready(BOOL (*ready)(void *),void *argument)
{ return ready(argument); }
void proc_exec_owner_exit(KPROC *p) { (void)p; }
void proc_dispose_process(KPROC *p) { (void)p;abort(); }
static unsigned executions_reaped;
static void copy_execution_reap(void *resource)
{ CHECK(resource && !kowner_locked());executions_reaped++; }
#endif
int main(void)
{
    ASPACE backend={0};
    KASPACE *space=kaspace_adopt(&backend);
    KPROC_USER_COPY copy={0};
    KPROC *process;
    PD *claimed=NULL;
    LONG address;
    ULONG input=0x7130c0de,output=0;
    unsigned baseline,i;
    CHECK(space && kproc_create(&child));
    CHECK(kproc_test_share_active(&child,space));
    process=kproc_find(&child);
    address=kproc_mmap(&child,0,2*USER_PAGE_SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,0,0);
    CHECK(address>0);
    CHECK(kaspace_mmap_demand_page(space,address,ASPACE_PROT_WRITE)==1);
    baseline=allocations-frees;
    for(i=0;i<64;i++) {
        CHECK(kproc_user_copy_begin(&copy,address,sizeof(input),TRUE)==E_OK);
        CHECK(process->user_copies==1 && copy.lease && kaspace_copy_valid(copy.lease));
        CHECK(!kaspace_lease_valid(copy.lease)); /* residency is not a general access grant */
        CHECK(kproc_munmap(&child,address,USER_PAGE_SIZE)==EACCDN);
        CHECK(kproc_mprot(&child,address,USER_PAGE_SIZE,PROT_READ)==EACCDN);
        CHECK(!kproc_begin_disposal(process,&claimed) && !claimed);
        kproc_destroy(&child);CHECK(kproc_find(&child)==process); /* no premature teardown */
        CHECK(kproc_user_copy_write(&copy,&input));
        CHECK(!kproc_user_copy_read(&copy,&output));
        kproc_user_copy_end(&copy);kproc_user_copy_end(&copy);
        CHECK(!process->user_copies && allocations-frees==baseline);
        CHECK(kproc_copy_from_user_checked(&output,address,sizeof(output))==E_OK && output==input);
        CHECK(allocations-frees==baseline);
    }
    fail_copy=TRUE;
    CHECK(kproc_copy_to_user_checked(address,&input,sizeof(input))==EIMBA);
    fail_copy=FALSE;
    CHECK(!process->user_copies && allocations-frees==baseline);
#if CONF_WITH_NATIVE_SCHED
    CHECK(kproc_user_copy_begin(&copy,address,sizeof(input),TRUE)==E_OK);
    actor=2;
    CHECK(!kaspace_copy_valid(copy.lease) && !kproc_user_copy_write(&copy,&input));
    CHECK(!kaspace_materialization_change(&backend,address,USER_PAGE_SIZE,ASPACE_PROT_USER|ASPACE_PROT_WRITE));
    actor=1;kproc_user_copy_end(&copy);CHECK(!scope_top);
#endif
    fail_allocation=1;
    CHECK(kproc_copy_to_user_checked(address,&input,sizeof(input))==ENSMEM);
    CHECK(!process->user_copies && allocations-frees==baseline);
    CHECK(kproc_user_copy_begin(&copy,address+USER_PAGE_SIZE,sizeof(input),TRUE)==E_OK);
    CHECK(kaspace_copy_valid(copy.lease)); /* lazy materialization under the copy fence */
    kproc_user_copy_end(&copy);
    /* The closing claim precedes service callbacks. A callback cannot reopen
     * live operation admission after destruction checked the in-flight count. */
    CHECK(kproc_claim_destroy(&child)==process);
    CHECK(!kproc_retain_live(process) && !kproc_thread_start_allowed(process));
    CHECK(!kthread_create(&child));
    CHECK(kproc_user_copy_begin(&copy,address,sizeof(input),TRUE)==EACCDN);
    CHECK(!process->user_copies && !copy.active);
    process->destroying_pd=NULL;process->file_cleanup_started=FALSE;
    process->disposal_state=PROCESS_LIVE; /* fixture returns before actual teardown */
    CHECK(kproc_munmap(&child,address,2*USER_PAGE_SIZE)==E_OK);
    {
        int foreign_identity;
        KASPACE_MEMBER *member=NULL;
        unsigned released=backend.releases;
        address=kproc_mmap(&child,0,USER_PAGE_SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,0,0);
        CHECK(address>0);
        CHECK(kaspace_member_new(space,777,&foreign_identity,&member)==E_OK);
        kaspace_mmap(space)->regions->scope=member->scope;
        CHECK(kproc_user_copy_begin(&copy,address,sizeof(input),TRUE)==E_OK);
        kaspace_member_release(&member); /* the last foreign mapping owner */
        CHECK(backend.releases==released && aspace_translate(&backend,address,NULL,NULL));
        CHECK(kaspace_copy_valid(copy.lease) && kproc_user_copy_write(&copy,&input));
        {
            KPROC_USER_COPY denied={0};
            CHECK(kproc_user_copy_begin(&denied,address,sizeof(input),FALSE)==EACCDN);
        }
        kproc_user_copy_end(&copy);
        CHECK(backend.releases==released+1 && !aspace_translate(&backend,address,NULL,NULL));
        CHECK(!kaspace_mmap(space)->regions && !process->user_copies);
    }
#if CONF_WITH_NATIVE_SCHED
    {
        int execution[3];
        UWORD status;
        KTHREAD *a=kthread_create(&child),*b=kthread_create(&child),*c;
        CHECK(a && b);
        CHECK(kthread_set_execution(a,&execution[0],copy_execution_reap));
        CHECK(kthread_set_execution(b,&execution[1],copy_execution_reap));
        CHECK(kthread_start(a) && kthread_start(b));
        process->started=TRUE;process->threaded=TRUE;
        address=kproc_mmap(&child,0,USER_PAGE_SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,0,0);
        CHECK(address>0 && kproc_user_copy_begin(&copy,address,sizeof(input),TRUE)==E_OK);
        CHECK(kproc_prepare_thread_exit_handoff(process,b));
        CHECK(kthread_finish_exit(b,7) && kthread_reap(b));
        CHECK(process->user_copies==1 && process->disposal_state==PROCESS_LIVE && executions_reaped==1);
        c=kthread_create(&child);CHECK(c);
        CHECK(kthread_set_execution(c,&execution[2],copy_execution_reap) && kthread_start(c));
        CHECK(kproc_request_group_exit(process,61) && kproc_request_group_exit(process,62));
        CHECK(kproc_group_status(process,&status) && status==61 && !kthread_create(&child));
        CHECK(kproc_prepare_thread_exit_handoff(process,c));
        CHECK(kthread_finish_exit(c,status) && kthread_reap(c));
        CHECK(executions_reaped==2 && !kproc_prepare_thread_exit_handoff(process,a));
        CHECK(process->disposal_state==PROCESS_LIVE && kproc_user_copy_write(&copy,&input));
        kproc_user_copy_end(&copy);
        CHECK(kproc_prepare_thread_exit_handoff(process,a));
        CHECK(kthread_finish_exit(a,status) && kthread_reap(a));
        CHECK(!process->threads && !process->user_copies && executions_reaped==3);
        puts("Retained copy permits ordinary/cancelled sibling stop, preserves first group status, and excludes final PROCESS handoff: PASS (production owners; CPU modeled)");
    }
#endif
    kproc_destroy(&child);kaspace_put(space);
    CHECK(allocations==frees);
    puts("PROCESS user-copy operations: independent rights, lazy backing, unmap/teardown exclusion, checked failures and exact conservation: PASS (MMU model)");
    return 0;
}
