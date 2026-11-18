/* Real KPROC runtime ownership and MMU/allocator, with hosted physical
 * providers. (#1018 removed the interpreted shared-VM staging it once held.) */
#define TEST_PHYSICAL_PAGES 2048 /* actual 8 MiB RAM, including production reserve */
#define main borrow_regression_main
#include "procmem_borrow.c"
#undef main
#include "../bdos/kproc.c"
WORD bootdev=2;
#if KPROC_LEGACY_PRG && CONF_WITH_IMAGE_POLICY
static void legacy_image_policy(void)
{
    KPROC source={0};
    static struct { PD pd;UBYTE text[TEST_PAGE]; } legacy;
    PD *pd;
    KPROC_IMAGE_POLICY first,second;
    ULONG metadata=metadata_live,ram=physical_live;
    pd=&legacy.pd;memset(&legacy,0,sizeof(legacy));
    source.pd=pd;source.references=1;source.number=301;kproc_list=&source;
    source.user_start=(UBYTE *)pd;source.user_end=(UBYTE *)(&legacy+1);kproc_mark_legacy(pd);
    pd->p_tbase=PTR_TO_USERPTR_UNCHECKED((UBYTE *)(pd+1));pd->p_tlen=128;
    CHECK(kproc_record_legacy_text(pd,128) && source.legacy_text_entry==(VADDR)(uintptr_t)(pd+1));
    CHECK(!source.entry); /* Classic launcher enters the TPA directly. */
    source.aspace=aspace_create();source.active_space=kaspace_adopt(source.aspace);source.started=TRUE;
    CHECK(source.aspace && source.active_space && kproc_image_policy_capture(&source,&first)==E_OK);
    CHECK(first.legacy && first.legacy_text_recorded && first.legacy_text_bytes==128 &&
          first.entry==(VADDR)(uintptr_t)(pd+1) && !first.has_image && !first.program.nseg);
    /* Writable public fields do not change the immutable loader snapshot. */
    pd->p_tbase=0;pd->p_tlen=0;
    CHECK(kproc_image_policy_capture(&source,&second)==E_OK && !memcmp(&first,&second,sizeof(first)));
    CHECK(!kproc_record_legacy_text(pd,0) && source.legacy_text_entry==first.entry);
    kproc_list=NULL;kaspace_put(source.active_space);
    CHECK(metadata_live==metadata && physical_live==ram);
    puts("Immutable PRG policy: authenticated loader entry/text, unchanged by writable basepage fields, exact cleanup: PASS");
}
#endif
/* Scheduling is modeled here; native regression checks owner/ticket nesting. */
void ksched_cleanup_begin(KSCHED_CLEANUP_SCOPE *scope)
{ CHECK(!scope->active);scope->active=TRUE; }
void ksched_cleanup_end(KSCHED_CLEANUP_SCOPE *scope)
{ CHECK(scope->active);scope->active=FALSE; }
BOOL ksched_wait_ready(BOOL (*ready)(void *),void *argument)
{ CHECK(ready(argument));return TRUE; }
struct kthread { KPROC *process;KASPACE *space;KTHREAD *next; };
KPROC *kthread_process(const KTHREAD *thread) {return thread->process;}
KASPACE *kthread_aspace(const KTHREAD *thread) {return thread->space;}
KTHREAD **kthread_next_link(KTHREAD *thread) {return &thread->next;}
/* This fixture has no file-backed reservations; an unexpected read is a bug. */
LONG fileback_read(FILE_BACKING *backing,ULONG offset,void *buffer,ULONG size)
{ (void)backing;(void)offset;(void)buffer;(void)size;abort();return EREADF; }
#if CONF_WITH_MMAP
void fileback_put(FILE_BACKING *backing) { if(backing) abort(); }
void fileback_hold(FILE_BACKING *backing) { if(backing) abort(); }
BOOL fileback_try_hold(FILE_BACKING *backing) { CHECK(!backing);return TRUE; }
LONG fileback_get(LONG handle,FILE_BACKING **backing)
{ (void)handle;(void)backing;abort();return EIHNDL; }
ULONG fileback_size(const FILE_BACKING *backing) { (void)backing;abort();return 0; }
#endif
#if defined(BORROW_GENERIC)
void aspace_hw_drop_tables(PADDR root,const PADDR *pages,ULONG count)
{
    struct modeled_root *r=(void *)(uintptr_t)root;ULONG i,j;
    for(i=0;i<r->count;) {
        for(j=0;j<count && r->groups[i].table!=pages[j];j++) ;
        if(j<count) {memmove(&r->groups[i],&r->groups[i+1],(--r->count-i)*sizeof(r->groups[0]));}
        else i++;
    }
}
#endif
static ULONG retired_allocation_notifications;
static void retired_allocation_freed(void *token)
{ CHECK(token==&retired_allocation_notifications);retired_allocation_notifications++; }
static void retiring_construction_borrows(void)
{
    unsigned running;
    for(running=0;running<2;running++) {
        KPROC owner={0};PD owner_pd={0};ASPACE *as;KASPACE *space,*peer;
        KASPACE_RESERVATION_HOLD held={0};KASPACE_RESERVATION_INFO info={0};
        PROCMEM_STATS before,after;void *block,*released=NULL;
        ULONG metadata=metadata_live,ram=physical_live,notices=retired_allocation_notifications;
        procmem_stats(&before);as=aspace_create();space=kaspace_adopt(as);peer=kaspace_share(space);
        CHECK(as && space && peer);selected_root=aspace_root(as);
        block=procmem_alloc(2*TEST_PAGE,0);CHECK(block && procmem_set_owner(block,&owner_pd));
        CHECK(procmem_on_free(block,retired_allocation_freed,&retired_allocation_notifications));
        CHECK(aspace_map_procmem(as,(VADDR)(uintptr_t)block,2*TEST_PAGE,ASPACE_PROT_WRITE));
        owner.pd=&owner_pd;owner.aspace=as;owner.active_space=space;
        owner.borrowed[0].va=(VADDR)(uintptr_t)block;owner.borrowed[0].bytes=2*TEST_PAGE;
        owner.borrowed[0].generation=procmem_gen(block);
        if(running) {
            CHECK(aspace_duplicate_borrow(as,(VADDR)(uintptr_t)block,2*TEST_PAGE));
            info.address=(VADDR)(uintptr_t)block;info.bytes=2*TEST_PAGE;
            info.identity=kaspace_reservation_next();info.rights=ASPACE_PROT_USER|ASPACE_PROT_WRITE;
            CHECK(kaspace_reservation_hold_captured(space,&info,info.address,info.bytes,
                        ASPACE_PROT_USER,42,&owner,&held)==E_OK);
        }
        retire_process_borrows(&owner);CHECK(!owner.borrowed[0].bytes);
        retire_process_borrows(&owner); /* consumed exact reference cannot be retired twice */
        procmem_free_owned(&owner_pd,NULL);
        if(running) {
            CHECK(procmem_size(block) && procmem_pins(block)==2 && !procmem_owner(block));
            CHECK(kaspace_retire_borrow(space,(VADDR)(uintptr_t)block,2*TEST_PAGE));
            CHECK(procmem_size(block) && procmem_pins(block)==2 && aspace_translate(as,info.address,NULL,NULL));
            CHECK(kaspace_reservation_release(&held,&released) && released==&owner);
        }
        CHECK(!procmem_size(block) && !aspace_translate(as,(VADDR)(uintptr_t)block,NULL,NULL));
        CHECK(retired_allocation_notifications==notices+1);
        procmem_stats(&after);CHECK(after.live_allocs==before.live_allocs && after.free_pages==before.free_pages);
        /* The independent same-root peer remains alive: root destruction did
         * not hide a pin or allocation leak. Cached tables remain root-owned. */
        kaspace_put(space);CHECK(kaspace_backend(peer)==as);kaspace_put(peer);
        CHECK(metadata_live==metadata && physical_live==ram);
    }
}
#if CONF_WITH_SHARED_VM_PEXEC
/* 68k shared-VM Pexec only (#1018). */
static void supervisor_clone_metadata(void)
{
    KPROC owner={0};PD pd={0};struct shared_exec_regions held={0};
    KASPACE_RESERVATION_INFO info={0};PROCMEM_HOLD_INFO raw;
    ULONG ram=physical_live,metadata=metadata_live;UQUAD free_before=free_pages;
    KPROC_ALLOCATION_HOLD refused={ {0,0,0} };
    owner.pd=&pd;owner.number=234;owner.references=1;owner.clone_guarded=TRUE;
    owner.aspace=aspace_create();owner.active_space=kaspace_adopt(owner.aspace);
    owner.clone_compat=owner.active_space;owner.clone_maps=3;
    owner.user_start=procmem_alloc(TEST_PAGE,0);owner.env_start=procmem_alloc(TEST_PAGE,0);
    CHECK(owner.user_start && owner.env_start);
    owner.user_end=owner.user_start+TEST_PAGE;owner.env_end=owner.env_start+TEST_PAGE;
    owner.tpa_gen=procmem_gen(owner.user_start);owner.env_gen=procmem_gen(owner.env_start);
    CHECK(procmem_set_owner(owner.user_start,&pd) && procmem_set_owner(owner.env_start,&pd));
    CHECK(aspace_borrow(owner.aspace,(VADDR)(uintptr_t)owner.user_start,TEST_PAGE));
    CHECK(aspace_borrow(owner.aspace,(VADDR)(uintptr_t)owner.env_start,TEST_PAGE));
    kproc_list=&owner;held.space=owner.active_space;
    CHECK(kproc_allocation_hold(&pd,owner.user_start,owner.tpa_gen,0,TEST_PAGE,
              ASPACE_PROT_USER,&refused)==EACCDN);
    CHECK(shared_raw(&held,&owner,owner.user_start,owner.tpa_gen)==E_OK);
    CHECK(shared_raw(&held,&owner,owner.env_start,owner.env_gen)==E_OK);
    CHECK(procmem_hold_info(&held.raw[0].token,&raw) && !raw.rights && raw.bytes==TEST_PAGE);
    CHECK(shared_compat_logical(&held,&owner,owner.user_start,owner.tpa_gen,&owner.reservation_birth[0])==E_OK);
    CHECK(shared_compat_logical(&held,&owner,owner.env_start,owner.env_gen,&owner.reservation_birth[1])==E_OK);
    CHECK(!owns_top_stack(&owner));
    CHECK(kproc_reservation_info(&pd,USER_STACK_TOP-TEST_PAGE,TEST_PAGE,&info)==EACCDN);
    CHECK(kaspace_reservation_info(owner.active_space,(VADDR)(uintptr_t)owner.user_start,TEST_PAGE,&info));
    CHECK(!info.rights && (info.flags&KASPACE_RESERVATION_INACCESSIBLE));
    {
        KTHREAD sibling={&owner,owner.active_space,NULL};
        owner.started=TRUE;owner.threads=&sibling;
        kproc_clone_commit_sibling(&owner,&sibling);
        CHECK(!owns_top_stack(&owner) && !owner.clone_top_stack);
        /* An ordinary image's original stack survives its first and later
         * SAME_PROCESS publications; a generic source never gains one. */
        owner.clone_guarded=FALSE;
        CHECK(kproc_reservation_info(&pd,USER_STACK_TOP-TEST_PAGE,TEST_PAGE,&info)==E_OK);
        kproc_clone_commit_sibling(&owner,&sibling);
        CHECK(owns_top_stack(&owner) && owner.clone_top_stack);
        CHECK(kproc_reservation_info(&pd,USER_STACK_TOP-2*TEST_PAGE,TEST_PAGE,&info)==E_OK);
        kproc_clone_commit_sibling(&owner,&sibling);
        CHECK(owns_top_stack(&owner));
        owner.threads=NULL;owner.started=FALSE;
    }
    {
        KPROC target={0};PD target_pd={0};unsigned prefix;LONG error;
        target.pd=&target_pd;target.number=235;target.references=1;
        target.mapping_preparation=MAPPING_PREPARED;
        for(prefix=1;prefix<16;prefix++) {
            ULONG before=metadata_live,refs=owner.references;
            PROCMEM_STATS a,b;procmem_stats(&a);
            fail_metadata=allocation_call+prefix;
            error=kproc_shared_dependencies_prepare(&target,&owner);fail_metadata=0;
            if(!error) kproc_shared_exec_abort(&target);
            else CHECK(error==ENSMEM && !target.shared_inherited);
            procmem_stats(&b);
            CHECK(metadata_live==before && owner.references==refs &&
                a.live_allocs==b.live_allocs && a.free_pages==b.free_pages);
            if(!error) break;
        }
        CHECK(prefix>4 && prefix<16 && !target.shared_inherited);
    }
    while(held.nlogical) CHECK(kproc_reservation_release(&held.logical[--held.nlogical]));
    while(held.nraw) CHECK(kproc_allocation_release(&held.raw[--held.nraw]));
    CHECK(owner.references==1);kproc_list=NULL;
    aspace_unborrow(owner.aspace,(VADDR)(uintptr_t)owner.user_start,TEST_PAGE);
    aspace_unborrow(owner.aspace,(VADDR)(uintptr_t)owner.env_start,TEST_PAGE);
    CHECK(procmem_free(owner.user_start) && procmem_free(owner.env_start));
    kaspace_put(owner.active_space);
    CHECK(ram==physical_live && metadata==metadata_live && free_before==free_pages);
    puts("Production generic-clone metadata: USER hold rejected, exact no-access lifetime, no phantom top stack and final conservation: PASS (host MMU)");
}
#endif
#if CONF_WITH_SHARED_VM_PEXEC
/* MAP_PTOS_RUNTIME scopes exist only with 68k shared-VM Pexec (#1018). */
/* Host installation adapters only: use the production mapping constructors,
 * syscall policy and final owner release with real counted MMU providers. */
static void runtime_install(KPROC *p,KASPACE *space)
{ p->active_space=kaspace_share(space);CHECK(p->active_space);p->aspace=kaspace_backend(space); }
static void runtime_drop(KPROC *p)
{
    KASPACE *space=p->active_space;
    kaspace_attachment_release_owner(space,p->number,p);
    kaspace_member_release(&p->mapping_members);
    p->allocation_scope=p->runtime_scope=NULL;p->active_space=NULL;p->aspace=NULL;
    kaspace_put(space);
}
static void runtime_mapping_policy(void)
{
    KPROC source={0},peer={0},loaded={0},private={0};
    PD pd[4]={{0}};
    KASPACE *space,*copy;
    KASPACE_MEMBER *members=NULL;
    LONG initial,late,general,own,probe,error;
    ULONG metadata=metadata_live,ram=physical_live,before;
    unsigned prefix;
    UWORD rights;
    space=kaspace_adopt(aspace_create());copy=kaspace_adopt(aspace_create());
    CHECK(space && copy);
    source.pd=&pd[0];peer.pd=&pd[1];loaded.pd=&pd[2];private.pd=&pd[3];
    source.number=901;peer.number=902;loaded.number=903;private.number=904;
    source.references=peer.references=loaded.references=private.references=1;
    source.next=&peer;peer.next=&loaded;loaded.next=&private;kproc_list=&source;
    runtime_install(&source,space);
    CHECK(kaspace_member_new(space,source.number,&source,&source.mapping_members)==E_OK);
    source.allocation_scope=kaspace_member_scope(source.mapping_members);
    CHECK(kaspace_member_add_runtime(space,source.number,&source,&source.mapping_members,&source.runtime_scope)==E_OK);
    /* Empty PRIVATE_VM snapshots preserve future runtime allocation ability. */
    CHECK(kproc_clone_prepare_mapping_owners(&private,&source,copy,TRUE,&members)==E_OK);
    kproc_clone_commit_mapping_owners(&private,copy,&members);runtime_install(&private,copy);
    CHECK(private.runtime_scope && private.runtime_scope!=source.runtime_scope);
    probe=kproc_mmap(private.pd,0,TEST_PAGE,PROT_READ|PROT_WRITE,
                    MAP_PRIVATE|MAP_ANONYMOUS|MAP_PTOS_RUNTIME,0,0);CHECK(probe>0);
    CHECK(kaspace_mmap_demand_page(copy,probe,ASPACE_PROT_WRITE)==1);
    runtime_drop(&private);CHECK(!kaspace_mmap(copy)->regions && !aspace_private_pages(kaspace_backend(copy)));
    kaspace_put(copy);
    initial=kproc_mmap(source.pd,0,TEST_PAGE,PROT_READ|PROT_WRITE,
                      MAP_PRIVATE|MAP_ANONYMOUS|MAP_PTOS_RUNTIME,0,0);CHECK(initial>0);
    CHECK(kproc_clone_prepare_mapping_owners(&peer,&source,space,FALSE,&members)==E_OK);
    kproc_clone_commit_mapping_owners(&peer,space,&members);runtime_install(&peer,space);
    loaded.standard_entry=TRUE;
    /* Every actual allocation prefix rolls back to the same live source. */
    for(prefix=1;prefix<16;prefix++) {
        before=metadata_live;fail_metadata=allocation_call+prefix;
        error=kproc_prepare_loaded_mapping_owners(&loaded,&source,space,&members);
        fail_metadata=0;
        if(!error) kproc_abort_mapping_owners(&loaded,space,&members);
        else CHECK(error==ENSMEM && !members && !loaded.mapping_preparation);
        CHECK(metadata_live==before && kaspace_mmap_protection(space,initial,&rights) &&
                rights==(ASPACE_PROT_USER|ASPACE_PROT_WRITE));
        if(!error) break;
    }
    CHECK(prefix>3 && prefix<16);
    CHECK(kproc_prepare_loaded_mapping_owners(&loaded,&source,space,&members)==E_OK);
    kproc_clone_commit_mapping_owners(&loaded,space,&members);runtime_install(&loaded,space);
    CHECK(loaded.allocation_scope==source.allocation_scope && loaded.runtime_scope!=source.runtime_scope);
    late=kproc_mmap(source.pd,0,TEST_PAGE,PROT_READ|PROT_WRITE,
                   MAP_PRIVATE|MAP_ANONYMOUS|MAP_PTOS_RUNTIME,0,0);
    general=kproc_mmap(source.pd,0,TEST_PAGE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,0,0);
    own=kproc_mmap(loaded.pd,0,TEST_PAGE,PROT_READ|PROT_WRITE,
                  MAP_PRIVATE|MAP_ANONYMOUS|MAP_PTOS_RUNTIME,0,0);
    CHECK(late>0 && general>0 && own>0);
    runtime_drop(&source);
    CHECK(kaspace_mmap_demand_page(space,late,ASPACE_PROT_WRITE)==1);
    runtime_drop(&peer);
    CHECK(!kaspace_mmap_protection(space,late,&rights));
    CHECK(kaspace_mmap_demand_page(space,initial,ASPACE_PROT_WRITE)==1);
    CHECK(kaspace_mmap_demand_page(space,general,ASPACE_PROT_WRITE)==1);
    CHECK(kaspace_mmap_demand_page(space,own,ASPACE_PROT_WRITE)==1);
    runtime_drop(&loaded);
    CHECK(!kaspace_mmap(space)->regions && !aspace_private_pages(kaspace_backend(space)));
    kproc_list=NULL;kaspace_put(space);
    CHECK(metadata_live==metadata && physical_live==ram);
    puts("Production common/runtime lifetime: late general sharing, exact runtime dependencies, generic future inheritance, empty private selector, allocation-prefix retry and final reclamation with surviving root: PASS (host MMU)");
}
#endif
int main(void)
{
    KPROC source={0};PD source_pd={0};
    window_phys=(PADDR)(uintptr_t)window;free_pages=WINDOW_PAGES;
#if KPROC_LEGACY_PRG && CONF_WITH_IMAGE_POLICY
    legacy_image_policy();
#endif
    retiring_construction_borrows();
#if CONF_WITH_SHARED_VM_PEXEC
    supervisor_clone_metadata();
#endif
#if CONF_WITH_SHARED_VM_PEXEC
    runtime_mapping_policy();
#endif
    source.number=123;source.references=1;
    source.pd=&source_pd;source.aspace=aspace_create();source.active_space=kaspace_adopt(source.aspace);
    CHECK(kaspace_member_new(source.active_space,source.number,&source,&source.mapping_members)==E_OK);
    source.allocation_scope=kaspace_member_scope(source.mapping_members);
    {
        struct shared_exec_regions stack_owner={0};UWORD rights=0;
        KASPACE_RESERVATION_INFO info={0};UQUAD before=free_pages;
        source.stack_size=1024UL*1024;source.shared_exec=&stack_owner;
        stack_owner.stack=USER_HEAP_BASE;stack_owner.stack_bytes=USER_STACK_MIN;
        kproc_list=&source;
        CHECK(kproc_psetlimit(source.pd,PSETLIMIT_STACK,-1)==1024UL*1024);
        CHECK(kproc_psetlimit(source.pd,PSETLIMIT_STACK,1024UL*1024)==1024UL*1024);
        CHECK(kproc_psetlimit(source.pd,PSETLIMIT_STACK,1024UL*1024+TEST_PAGE)==EACCDN);
        CHECK(!lazy_area(&source,USER_STACK_TOP-2*1024UL*1024,&rights));
        CHECK(!lazy_area(&source,USER_STACK_TOP-TEST_PAGE,&rights));
        CHECK(kproc_reservation_info(source.pd,USER_STACK_TOP-TEST_PAGE,TEST_PAGE,&info)==EACCDN);
        CHECK(!back_page(&source,USER_STACK_TOP-2*1024UL*1024,ASPACE_PROT_WRITE));
        CHECK(free_pages==before && source.stack_size==1024UL*1024);
        kproc_list=NULL;source.shared_exec=NULL;source.stack_size=0;
    }
#if CONF_WITH_MMAP && CONF_WITH_SHARED_VM_PEXEC
    {
        struct shared_exec_regions retained={0};void *identity=NULL;
        MMAP_REGION *region=kalloc(sizeof(*region));UWORD rights=ASPACE_PROT_USER|ASPACE_PROT_WRITE;
        CHECK(region);memset(region,0,sizeof(*region));
        region->address=PTOS_MMAP_BASE;region->length=TEST_PAGE;region->prot=rights;
        region->backing=(FILE_BACKING *)1;region->reservation=kaspace_reservation_next();
        kaspace_mmap(source.active_space)->regions=region;kaspace_mmap(source.active_space)->bytes=TEST_PAGE;
        source.references=1;kproc_list=&source;retained.space=source.active_space;
        CHECK(shared_logical(&retained,&source,region->address,TEST_PAGE,ASPACE_PROT_USER)==E_OK);
        CHECK(retained.nlogical==1 && source.references==2);
        CHECK(kaspace_materialization_change(source.aspace,region->address,TEST_PAGE,rights));
        CHECK(!kaspace_materialization_change(source.aspace,region->address,TEST_PAGE,ASPACE_PROT_USER));
        CHECK(!kaspace_mapping_can_change(source.aspace,region->address,TEST_PAGE));
        CHECK(kaspace_reservation_release(&retained.logical[0],&identity) && identity==&source);
        kproc_release(&source);CHECK(source.references==1);kproc_list=NULL;
        kaspace_mmap(source.active_space)->regions=NULL;kaspace_mmap(source.active_space)->bytes=0;kfree(region);
    }
#endif
    kaspace_member_release(&source.mapping_members);
    kaspace_put(source.active_space);kfree(source.heap);
    CHECK(!metadata_live && !physical_live);
    puts("Production KPROC runtime ownership: stack limits, retained borrows, mapping owners and exact final reclamation: PASS (host MMU)");
    return 0;
}
