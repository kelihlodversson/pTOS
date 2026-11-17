/* Real KPROC/KTHREAD/KASPACE ownership with the existing counted MMU model.
 * CPU/scheduler execution is not modeled here; native tests cover publication. */
#define kalloc image_owner_alloc
#define main original_mapping_fixture_main
#include "shared_root_mapping.c"
#undef main
#undef kalloc
#include "kproc_image_policy.h"
#include "ksched_lifetime.h"

static unsigned cleanup_depth;
static BOOL poison_next_allocation;
void *kalloc(ULONG bytes)
{
    void *memory=image_owner_alloc(bytes);
    if (memory && poison_next_allocation) { memset(memory,0xa5,bytes);poison_next_allocation=FALSE; }
    return memory;
}
UQUAD ksched_context_id(void) { return 1; }
void ksched_cleanup_begin(KSCHED_CLEANUP_SCOPE *scope)
{ CHECK(!scope->active);scope->active=TRUE;cleanup_depth++; }
void ksched_cleanup_end(KSCHED_CLEANUP_SCOPE *scope)
{ CHECK(scope->active && cleanup_depth);scope->active=FALSE;cleanup_depth--; }
BOOL ksched_wait_ready(BOOL (*ready)(void *),void *argument)
{ CHECK(ready(argument));return TRUE; }
void proc_exec_owner_exit(KPROC *process) { (void)process; }
void proc_dispose_process(KPROC *process) { (void)process;CHECK(0); }
BOOL ksched_pid_reserved(WORD pid) { (void)pid;return FALSE; }
void procmem_pin(VADDR address,VADDR bytes,int delta)
{ (void)address;(void)bytes;(void)delta;CHECK(0); }
BOOL ksched_test_clone_masked(void) { return FALSE; }
LONG proc_create_clone_process(KPROC *source,UWORD resources,KPROC **result)
{ (void)source;(void)resources;(void)result;CHECK(0);return EINVFN; }
void thread_startup_abort(THREAD_STARTUP_DRAFT **draft) { CHECK(!draft || !*draft); }
BOOL procmem_hold_info(const PROCMEM_HOLD *hold,PROCMEM_HOLD_INFO *info)
{ (void)hold;(void)info;CHECK(0);return FALSE; }
BOOL procmem_hold_release(PROCMEM_HOLD *hold,void **owner)
{ (void)hold;(void)owner;CHECK(0);return FALSE; }
BOOL aspace_protect_private(ASPACE *as,VADDR address,VADDR bytes,UWORD protection)
{
    VADDR page;
    if (!kaspace_mapping_change(as,address,bytes)) return FALSE;
    for(page=address;page-address<bytes;page+=USER_PAGE_SIZE) {
        struct model_page *mapped=model_page(as,page);
        CHECK(mapped);mapped->protection=protection;
    }
    return TRUE;
}

static void source_image(KPROC *source)
{
    source->started=TRUE;source->has_image=TRUE;
    source->layout.nseg=2;source->layout.entry=USER_IMAGE_BASE+16;
    source->layout.end=USER_IMAGE_BASE+2*USER_PAGE_SIZE;
    source->layout.seg[0].vaddr=USER_IMAGE_BASE;
    source->layout.seg[0].memsz=USER_PAGE_SIZE;source->layout.seg[0].flags=5;
    source->layout.seg[0].src=(void *)(uintptr_t)0xdeadbeefUL;
    source->layout.seg[0].filesz=123;
    source->layout.seg[1].vaddr=USER_IMAGE_BASE+USER_PAGE_SIZE;
    source->layout.seg[1].memsz=USER_PAGE_SIZE;source->layout.seg[1].flags=6;
    source->entry=source->layout.entry;source->stack_top=USER_STACK_TOP-32;
#if CONF_WITH_ELF_SHARED
    source->standard_entry=TRUE;
    source->interpreter_layout.nseg=1;
    source->interpreter_layout.seg[0].vaddr=USER_IMAGE_BASE+8*USER_PAGE_SIZE;
    source->interpreter_layout.seg[0].memsz=USER_PAGE_SIZE;source->interpreter_layout.seg[0].flags=5;
    source->interp_entry=USER_IMAGE_BASE+8*USER_PAGE_SIZE+32;source->interp_bytes=USER_PAGE_SIZE;
    source->entry=source->interp_entry;source->initial_rsp=USER_STACK_TOP-64;
#endif
}

static KTHREAD *initial_thread(KPROC *target,KPROC *source,KASPACE *space,VADDR stack,
                              KASPACE_MEMBER **members)
{
    KTHREAD *thread;
    KASPACE_LEASE *hold=NULL;
    CHECK(kproc_clone_prepare_mapping_owners(target,source,space,FALSE,members)==E_OK);
    thread=kthread_construct_identity(target,space);CHECK(thread);
    CHECK(kaspace_stack_hold_acquire(space,stack,USER_PAGE_SIZE,&hold)==E_OK);
    CHECK(kthread_take_stack_hold(thread,&hold) && !hold && kthread_clone_run_ready(thread));
    return thread;
}

int main(void)
{
    ASPACE backend={0};
    KASPACE *space=kaspace_adopt(&backend);
    PD source_pd={0},target_pd={0};
    KPROC *source,*target;
    KPROC_IMAGE_POLICY policy,unchanged,invalid;
    KPROC_IMAGE_PREPARATION *prepared=NULL,*other=NULL;
    KASPACE_MEMBER *members=NULL;
    KTHREAD *thread;
    KASPACE_RESERVATION_HOLD denied={0};
    LONG stack;
    unsigned before,refs,roots;
    ULONG tid;
    CHECK(space && kproc_create(&source_pd) && kproc_test_share_active(&source_pd,space));
    source=kproc_find(&source_pd);source_image(source);
    stack=kproc_uheap_alloc(&source_pd,USER_PAGE_SIZE);CHECK(stack>0);
    CHECK(kaspace_heap_demand_page(space,stack,ASPACE_PROT_WRITE)==1);
    CHECK(kproc_image_policy_capture(source,&policy)==E_OK);
    CHECK(policy.has_image && policy.program.nseg==2 && policy.program.seg[0].memsz==USER_PAGE_SIZE);
    CHECK(policy.program.seg[0].flags==5 && policy.program.entry==USER_IMAGE_BASE+16);
#if CONF_WITH_ELF_SHARED
    CHECK(policy.entry==source->interp_entry && policy.interpreter.entry==source->interp_entry &&
          policy.interpreter.end==USER_IMAGE_BASE+9*USER_PAGE_SIZE && policy.interpreter_bytes==USER_PAGE_SIZE);
#endif
    {
        PROC_CLONE_ARGS args={0};PROC_CLONE_TRANSACTION *transaction=NULL;
        unsigned references=source->references,root_refs=space->refs;
        unsigned allocated=allocations-frees,stage;
        args.source=source;args.process_policy=PROC_CLONE_SAME_PROCESS;args.vm_policy=PROC_CLONE_SHARE_VM;
        /* Actual transaction allocation is poisoned. Early source/target/root
         * failures and ordinary no-policy abort must see an empty receipt. */
        for (stage=1;stage<=4;stage++) {
            proc_clone_test_fail_after(stage);poison_next_allocation=TRUE;
            CHECK(proc_clone(&args,&transaction)==ENSMEM && !transaction && !poison_next_allocation);
            CHECK(source->references==references && space->refs==root_refs && allocations-frees==allocated);
        }
        poison_next_allocation=TRUE;
        CHECK(proc_clone(&args,&transaction)==E_OK && transaction && !poison_next_allocation);
        proc_clone_abort(&transaction);CHECK(!transaction);
        CHECK(source->references==references && space->refs==root_refs && allocations-frees==allocated);
    }
    /* Capture an unstarted pending loader image, without copying ownership. */
    source->started=FALSE;source->pending_space=source->active_space;source->active_space=NULL;
    source->pending_as=source->aspace;source->aspace=NULL;
    CHECK(kproc_image_policy_capture(source,&unchanged)==E_OK && !memcmp(&policy,&unchanged,sizeof(policy)));
    source->active_space=source->pending_space;source->pending_space=NULL;
    source->aspace=source->pending_as;source->pending_as=NULL;source->started=TRUE;
    memset(&unchanged,0xa5,sizeof(unchanged));invalid=unchanged;
    source->layout.seg[1].vaddr=source->layout.seg[0].vaddr;
    CHECK(kproc_image_policy_capture(source,&unchanged)==EACCDN && !memcmp(&invalid,&unchanged,sizeof(invalid)));
    source->layout.seg[1].vaddr=USER_IMAGE_BASE+USER_PAGE_SIZE;
#if CONF_WITH_ELF_SHARED
    {
        UQUAD entry=source->interp_entry;
        source->interp_entry=USER_VA_LIMIT+entry;
        CHECK(kproc_image_policy_capture(source,&unchanged)==EACCDN && !memcmp(&invalid,&unchanged,sizeof(invalid)));
        source->interp_entry=entry;
    }
#endif
    source->completed=TRUE;
    CHECK(kproc_image_policy_capture(source,&unchanged)==EACCDN && !memcmp(&invalid,&unchanged,sizeof(invalid)));
    source->completed=FALSE;
    CHECK(kproc_create(&target_pd));target=kproc_find(&target_pd);
    thread=initial_thread(target,source,space,stack,&members);tid=kthread_tid(thread);
    refs=target->references;roots=space->refs;before=allocations-frees;
    invalid=policy;invalid.program.seg[0].memsz=~(ULONG)0;
    CHECK(kproc_image_policy_prepare(target,thread,&invalid,&prepared)==EINVAL && !prepared);
#if CONF_WITH_ELF_SHARED
    invalid=policy;invalid.interpreter.seg[0].vaddr=USER_IMAGE_BASE;
    invalid.interpreter.entry=invalid.entry=USER_IMAGE_BASE+32;
    invalid.interpreter.end=USER_IMAGE_BASE+USER_PAGE_SIZE;
    CHECK(kproc_image_policy_prepare(target,thread,&invalid,&prepared)==EINVAL && !prepared);
#endif
    target->stack_top=USER_STACK_TOP-8;
    CHECK(kproc_image_policy_prepare(target,thread,&policy,&prepared)==EACCDN && !prepared);
    target->stack_top=0;
    target->started=TRUE;
    CHECK(kproc_image_policy_prepare(target,thread,&policy,&prepared)==EACCDN && !prepared);
    target->started=FALSE;
    fail_allocation=1;
    CHECK(kproc_image_policy_prepare(target,thread,&policy,&prepared)==ENSMEM && !prepared);
    fail_allocation=0;
    CHECK(target->references==refs && space->refs==roots && allocations-frees==before && !target->image_preparation);
    CHECK(kproc_image_policy_prepare(target,thread,&policy,&prepared)==E_OK);
    CHECK(target->references==refs+1 && space->refs==roots && !target->has_image && !target->entry);
    CHECK(kproc_image_policy_prepare(target,thread,&policy,&other)==EACCDN && !other);
    CHECK(kproc_image_policy_prepare(target,thread,&policy,&prepared)==EINVAL);
    CHECK(kproc_image_policy_validate(prepared)==E_OK);
    /* No borrowed-member dereference while its cleanup callback may run. */
    target->mapping_preparation=MAPPING_ABORTING;
    {
        KASPACE_MEMBER *saved=target->prepared_mapping_members;
        target->prepared_mapping_members=(void *)(uintptr_t)1;
        CHECK(kproc_image_policy_validate(prepared)==EACCDN);
        target->prepared_mapping_members=saved;
    }
    target->mapping_preparation=MAPPING_PREPARED;
    kproc_image_policy_release(&prepared);
    CHECK(!prepared && !target->image_preparation && target->references==refs && allocations-frees==before);
    CHECK(kproc_image_policy_prepare(target,thread,&policy,&prepared)==E_OK);
    /* The external policy and source can change after prepare; the token owns
     * a value, never either source's mutable metadata or source identity. */
    invalid=policy;policy.program.seg[0].flags=7;source->layout.seg[0].flags=7;
    kproc_clone_commit_mapping_owners(target,space,&members);
    image_policy_commit(target); /* metadata boundary; native covers real activation */
    CHECK(!target->image_preparation && target->has_image && target->image_policy_only);
    CHECK(target->layout.seg[0].flags==5 && !target->layout.seg[0].src && !target->layout.seg[0].filesz);
    CHECK(!target->pending_space && !target->active_space && !target->load_parent && !target->lender);
    CHECK(!target->kstack_phys && !target->stack_top && !target->stack_low && target->threads==thread && kthread_tid(thread)==tid);
    kproc_image_policy_release(&prepared);CHECK(target->references==refs);
    target->active_space=kaspace_share(space);target->aspace=&backend;target->started=TRUE;
    backend.page[backend.pages].address=USER_IMAGE_BASE;
    backend.page[backend.pages++].protection=ASPACE_PROT_USER|ASPACE_PROT_EXEC;
    /* Readable/executable PTEs plus copied policy are not captured backing. */
    CHECK(kproc_reservation_hold(&target_pd,
                USER_IMAGE_BASE,16,ASPACE_PROT_USER|ASPACE_PROT_EXEC,FALSE,&denied)==EACCDN && !denied.ticket);
    CHECK(!target->reservation_birth[3]);
#if CONF_WITH_ELF_SHARED
    CHECK(kproc_mprot(&target_pd,USER_IMAGE_BASE,USER_PAGE_SIZE,PROT_READ)==EIMBA);
    CHECK(backend.page[backend.pages-1].protection==(ASPACE_PROT_USER|ASPACE_PROT_EXEC));
    CHECK(!target->initial_rsp && target->standard_entry && target->interp_entry==policy.entry);
#endif
    kthread_destroy(thread);dispose(&target_pd);
    CHECK(!backend.destroyed && source->references==1);
    /* Real target retirement must cancel the external preparation before
     * mutable payload is wiped, while preserving the exact retained identity. */
    policy=invalid;CHECK(kproc_create(&target_pd));target=kproc_find(&target_pd);
    thread=initial_thread(target,source,space,stack,&members);
    CHECK(kproc_image_policy_prepare(target,thread,&policy,&prepared)==E_OK);
    kproc_abort_mapping_owners(target,space,&members);
    kthread_destroy(thread);dispose(&target_pd);
    CHECK(prepared->state==IMAGE_CANCELLED && !target->pd && target->retired);
    CHECK(kproc_image_policy_validate(prepared)==EACCDN);
    kproc_image_policy_release(&prepared);
    /* Source-first metadata lifetime: exact target membership, THREAD and
     * stack remain, but no preparation reference keeps the source alive. */
    CHECK(kproc_create(&target_pd));target=kproc_find(&target_pd);
    thread=initial_thread(target,source,space,stack,&members);
    CHECK(kproc_image_policy_prepare(target,thread,&policy,&prepared)==E_OK && source->references==1);
    dispose(&source_pd);CHECK(!kproc_find(&source_pd) && kproc_count()==1 && !backend.destroyed);
    CHECK(kproc_image_policy_validate(prepared)==E_OK);
    kproc_clone_commit_mapping_owners(target,space,&members);image_policy_commit(target);
    kproc_image_policy_release(&prepared);
    target->active_space=kaspace_share(space);target->aspace=&backend;target->started=TRUE;
    CHECK(kproc_image_policy_capture(target,&unchanged)==E_OK && !memcmp(&policy,&unchanged,sizeof(policy)));
    kthread_destroy(thread);dispose(&target_pd);
    CHECK(!kproc_count() && !reservations(space) && !file.references && !cleanup_depth && !backend.destroyed);
    kaspace_put(space);CHECK(backend.destroyed && allocations==frees);
    puts("Immutable image policy: live/pending capture, empty-target preparation, rollback, source-first value isolation, no captured authority, retirement and exact references: PASS (production owners; hosted MMU)");
    return 0;
}
