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
    CHECK(kproc_munmap(&child,address,2*USER_PAGE_SIZE)==E_OK);
    kproc_destroy(&child);kaspace_put(space);
    CHECK(allocations==frees);
    puts("PROCESS user-copy operations: independent rights, lazy backing, unmap/teardown exclusion, checked failures and exact conservation: PASS (MMU model)");
    return 0;
}
