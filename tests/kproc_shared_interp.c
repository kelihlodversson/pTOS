/* Real loader/KPROC staging and MMU/allocator, with hosted physical providers. */
#define TEST_PHYSICAL_PAGES 2048 /* actual 8 MiB RAM, including production reserve */
#define main borrow_regression_main
#include "procmem_borrow.c"
#undef main
#include "../bdos/kproc.c"
WORD bootdev=2;
/* Scheduling is modeled here; native regression checks owner/ticket nesting. */
void ksched_cleanup_begin(KSCHED_CLEANUP_SCOPE *scope)
{ CHECK(!scope->active);scope->active=TRUE; }
void ksched_cleanup_end(KSCHED_CLEANUP_SCOPE *scope)
{ CHECK(scope->active);scope->active=FALSE; }
UQUAD ksched_context_id(void) { return 1; }
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
static void stage_plan(DL_IMAGE *plan,ULONG size,ULONG alignment)
{
    memset(plan,0,sizeof(*plan));plan->size=size;plan->align=alignment;
    plan->entry=16;plan->phdr=64;plan->phnum=2;plan->type=3;plan->nload=1;
    plan->loads[0].address=0;plan->loads[0].filesz=128;plan->loads[0].memsz=128;
    plan->loads[0].flags=DL_PF_R|DL_PF_X;
}
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
int main(void)
{
    KPROC source={0},target={0};PD source_pd={0},target_pd={0},original_pd;
    struct shared_interp_recipe recipe;
    struct shared_exec_regions set;
    KASPACE *pending;ASPACE *original;UBYTE data[128],copied[128];
    ULONG i;LONG error;VADDR ordinary;static UBYTE environment[2]={0,0};
    PROG_LAYOUT original_layout={0};
    window_phys=(PADDR)(uintptr_t)window;free_pages=WINDOW_PAGES;
    retiring_construction_borrows();
    supervisor_clone_metadata();
    runtime_mapping_policy();
    source.number=123;source.references=target.references=1;
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
#if CONF_WITH_MMAP
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
    source.legacy=TRUE;target.pd=&target_pd;
    original=aspace_create();pending=kaspace_adopt(original);target.pending_space=pending;target.pending_as=original;
    memset(&recipe,0,sizeof(recipe));stage_plan(&recipe.program,2*TEST_PAGE,2*TEST_PAGE);
    stage_plan(&recipe.interpreter,TEST_PAGE,TEST_PAGE);recipe.bias=USER_IMAGE_BASE;recipe.ibias=INTERPRETER_BASE;
    strcpy(recipe.filename,"demo.elf");target.env_start=environment;target.env_end=environment+2;
    recipe.program.relro=TEST_PAGE;recipe.program.relro_size=TEST_PAGE;
    recipe.program.nload=2;recipe.program.loads[1].address=TEST_PAGE;
    recipe.program.loads[1].filesz=64;recipe.program.loads[1].memsz=TEST_PAGE;
    recipe.program.loads[1].flags=DL_PF_R|DL_PF_W;
    target.shared_interp_recipe=&recipe;target.standard_entry=target.has_image=TRUE;
    target.layout=original_layout;target.initial_rsp=USER_STACK_TOP-32;target.interp_entry=INTERPRETER_BASE+16;
    target_pd.p_tbase=PTR_TO_USERPTR_UNCHECKED((UBYTE *)(uintptr_t)(USER_IMAGE_BASE+16));
    target_pd.p_dbase=target_pd.p_bbase=PTR_TO_USERPTR_UNCHECKED((UBYTE *)(uintptr_t)(USER_IMAGE_BASE+2*TEST_PAGE));
    original_pd=target_pd;CHECK(heap_room(&source));
    CHECK(aspace_map_private(original,USER_IMAGE_BASE,2*TEST_PAGE,ASPACE_PROT_USER|ASPACE_PROT_WRITE));
    CHECK(aspace_map_private(original,INTERPRETER_BASE,TEST_PAGE,ASPACE_PROT_USER|ASPACE_PROT_WRITE));
    memset(data,0x5a,sizeof(data));CHECK(aspace_copy_to_user(original,USER_IMAGE_BASE,data,sizeof(data)));
    memset(data,0x96,sizeof(data));CHECK(aspace_copy_to_user(original,INTERPRETER_BASE,data,sizeof(data)));
    for(i=1;i<=9;i++) {
        memset(&set,0,sizeof(set));set.space=source.active_space;set.parent=&source;
        fail_metadata=allocation_call+i;error=shared_interp_prepare(&source,&target,&set);reset_failure();
        CHECK(error==E_OK || error==ENSMEM);shared_interp_abort(&target,&set);
        CHECK(target.pending_space==pending && target.pending_as==original && !source.nheap);
        CHECK(!target.load_parent && !target.load_va && !memcmp(&target.layout,&original_layout,sizeof(original_layout)));
        CHECK(target.initial_rsp==USER_STACK_TOP-32 && target.interp_entry==INTERPRETER_BASE+16);
        CHECK(!memcmp(&target_pd,&original_pd,sizeof(target_pd)));
        CHECK(aspace_copy_from_user(original,copied,USER_IMAGE_BASE,sizeof(copied)) && copied[0]==0x5a);
    }
    memset(&set,0,sizeof(set));set.space=source.active_space;set.parent=&source;
    CHECK(shared_interp_prepare(&source,&target,&set)==E_OK);
    CHECK(!target.pending_space && source.nheap==1 && target.load_parent==source.pd);
    CHECK(target.layout.entry%recipe.program.align==16);
    CHECK(aspace_copy_from_user(source.aspace,copied,target.layout.entry-16,sizeof(copied)) && copied[0]==0x5a);
    CHECK(aspace_copy_from_user(source.aspace,copied,target.interp_entry-16,sizeof(copied)) && copied[0]==0x96);
    {
        UWORD rights;VADDR tail=target.load_va+(VADDR)target.load_pages*TEST_PAGE-TEST_PAGE;
        CHECK(aspace_translate(source.aspace,tail,NULL,&rights) && !rights);
    }
    {
        struct shared_exec_regions peer={0};struct uheap_block *image=heap_find(&source,target.load_va);
        UBYTE kind;void *identity=NULL;PADDR old_physical,new_physical;UWORD old_rights,new_rights;
        CHECK(image && aspace_translate(source.aspace,target.layout.entry,&old_physical,&old_rights));kind=image->kind;
        source.references=1;kproc_list=&source;image->kind=UH_SHARED_IMAGE;
        peer.space=source.active_space;peer.parent=&source;
        CHECK(shared_source_images(&peer,&source)==E_OK && peer.nlogical==1 && source.references==2);
        CHECK(kaspace_reservation_matches(&peer.logical[0],source.active_space,
                    image->reservation,image->va,(VADDR)image->pages*TEST_PAGE));
        CHECK(!kaspace_mapping_can_change(source.aspace,image->va,(VADDR)image->pages*TEST_PAGE));
        CHECK(aspace_translate(source.aspace,target.layout.entry,&new_physical,&new_rights) &&
                    new_physical==old_physical && new_rights==old_rights);
        CHECK(kaspace_reservation_release(&peer.logical[0],&identity) && identity==&source);
        kproc_release(&source);CHECK(source.references==1);
        image->kind=kind;kproc_list=NULL;
    }
    {
        KASPACE_RESERVATION_INFO info={0};void *released=NULL;UQUAD ticket,birth;
        VADDR relro=set.relro_address[0];
        info.address=set.provisional_va;info.bytes=(VADDR)set.provisional_pages*TEST_PAGE;
        info.identity=set.provisional_birth;info.flags=KASPACE_RESERVATION_CAPTURED|KASPACE_RESERVATION_INACCESSIBLE;
        CHECK(kaspace_reservation_hold_captured(set.space,&info,info.address,info.bytes,0,
                    source.number,&source,&set.logical[0])==E_OK);
        set.nlogical=1;set.image_hold=0;target.shared_exec=&set;
        target.active_space=set.space;target.aspace=source.aspace;target.started=TRUE;
        CHECK(!kproc_shared_exec_relro_range(&target,relro,TEST_PAGE));
        set.committed=TRUE;
        CHECK(kproc_shared_exec_relro_range(&target,relro,TEST_PAGE));
        /* Exercise the actual current-PROCESS syscall owner as well as the
         * pure provenance projection; general image/heap/mmap stay excluded. */
        kproc_list=&target;
        CHECK(kproc_mprot(target.pd,relro,TEST_PAGE,PROT_READ)==E_OK);
        CHECK(kproc_mprot(target.pd,relro,TEST_PAGE,PROT_READ|PROT_WRITE)==EACCDN);
        CHECK(kproc_mprot(target.pd,relro-TEST_PAGE,TEST_PAGE,PROT_READ)==EACCDN);
        CHECK(aspace_map_private(source.aspace,PTOS_MMAP_BASE,TEST_PAGE,ASPACE_PROT_USER|ASPACE_PROT_WRITE));
        CHECK(kproc_mprot(target.pd,PTOS_MMAP_BASE,TEST_PAGE,PROT_READ)==EIMBA);
        CHECK(aspace_unmap_private(source.aspace,PTOS_MMAP_BASE,TEST_PAGE));
        kproc_list=NULL;
        CHECK(!kproc_shared_exec_relro_range(&target,relro-TEST_PAGE,TEST_PAGE));
        CHECK(!kproc_shared_exec_relro_range(&target,relro,2*TEST_PAGE));
        ticket=set.logical[0].ticket;set.logical[0].ticket++;
        CHECK(!kproc_shared_exec_relro_range(&target,relro,TEST_PAGE));set.logical[0].ticket=ticket;
        birth=set.provisional_birth;set.provisional_birth++;
        CHECK(!kproc_shared_exec_relro_range(&target,relro,TEST_PAGE));set.provisional_birth=birth;
        {
            KASPACE_RESERVATION_HOLD saved=set.logical[0],wrong={0};
            KASPACE_RESERVATION_INFO other=info;
            KASPACE *peer=kaspace_adopt(aspace_create());CHECK(peer);
            other.address+=TEST_PAGE;other.bytes=TEST_PAGE;
            CHECK(kaspace_reservation_hold_captured(set.space,&other,other.address,other.bytes,0,
                    source.number,&source,&wrong)==E_OK);
            set.logical[0]=wrong;CHECK(!kproc_shared_exec_relro_range(&target,relro,TEST_PAGE));
            set.logical[0]=saved;released=NULL;CHECK(kaspace_reservation_release(&wrong,&released));
            other=info;other.identity=kaspace_reservation_next();
            CHECK(kaspace_reservation_hold_captured(set.space,&other,other.address,other.bytes,0,
                    source.number,&source,&wrong)==E_OK);
            set.logical[0]=wrong;CHECK(!kproc_shared_exec_relro_range(&target,relro,TEST_PAGE));
            set.logical[0]=saved;released=NULL;CHECK(kaspace_reservation_release(&wrong,&released));
            CHECK(kaspace_reservation_hold_captured(peer,&info,info.address,info.bytes,0,
                    source.number,&source,&wrong)==E_OK);
            set.logical[0]=wrong;CHECK(!kproc_shared_exec_relro_range(&target,relro,TEST_PAGE));
            set.logical[0]=saved;released=NULL;CHECK(kaspace_reservation_release(&wrong,&released));
            kaspace_put(peer);released=NULL;
        }
        source.pd=NULL;CHECK(kproc_shared_exec_relro_range(&target,relro,TEST_PAGE));
        kproc_list=&target;CHECK(kproc_mprot(target.pd,relro,TEST_PAGE,PROT_READ)==E_OK);kproc_list=NULL;
        source.pd=&source_pd;
        CHECK(kaspace_reservation_release(&set.logical[0],&released) && released==&source);
        CHECK(!kproc_shared_exec_relro_range(&target,relro,TEST_PAGE));
        set.nlogical=0;set.committed=FALSE;target.shared_exec=NULL;
        target.active_space=NULL;target.aspace=NULL;target.started=FALSE;
    }
    set.stack_bytes=DL_START_CAPACITY+64UL*1024UL;
    ordinary=heap_alloc(&source,set.stack_bytes,UH_MALLOC,NULL,TRUE);CHECK(ordinary);
    fail_metadata=allocation_call+1;CHECK(shared_interp_stack(&target,&set,ordinary)==ENSMEM);reset_failure();
    CHECK(shared_interp_stack(&target,&set,ordinary)==E_OK);
    {
        uint32_t words[6];char name[9];
        CHECK(aspace_copy_from_user(source.aspace,words,set.initial_sp,sizeof(words)));
        CHECK(words[0]==1 && words[1]>=ordinary && words[1]<ordinary+set.stack_bytes);
        CHECK(words[2]==0 && words[3]==0 && words[4]==3);
        CHECK(words[5]==target.layout.entry-recipe.program.entry+recipe.program.phdr);
        CHECK(aspace_copy_from_user(source.aspace,name,words[1],sizeof(name)) && !strcmp(name,"demo.elf"));
    }
    {
        static UBYTE large_env[32768];
        ULONG original_bytes=set.stack_bytes;UQUAD saved_rsp=target.initial_rsp;
        memset(large_env,'a',sizeof(large_env));large_env[sizeof(large_env)-2]=0;
        large_env[sizeof(large_env)-1]=0;
        target.env_start=large_env;target.env_end=large_env+sizeof(large_env);
        set.stack_bytes=USER_STACK_MIN;
        CHECK(shared_interp_stack(&target,&set,ordinary)==ENSMEM && target.initial_rsp==saved_rsp);
        target.env_start=environment;target.env_end=environment+sizeof(environment);
        CHECK(shared_interp_stack(&target,&set,ordinary)==E_OK);
        CHECK(set.initial_sp>=ordinary+TEST_PAGE && set.initial_sp<ordinary+USER_STACK_MIN);
        set.stack_bytes=original_bytes;
    }
    CHECK(aspace_unmap_private(source.aspace,ordinary,set.stack_bytes));
    kaspace_heap(source.active_space)->count=0;
    shared_interp_abort(&target,&set);
    /* Fixed ET_EXEC collision is atomic, leaves original pending image retryable. */
    recipe.program.type=2;recipe.program.base=USER_IMAGE_BASE;recipe.program.entry+=USER_IMAGE_BASE;
    recipe.program.loads[0].address=USER_IMAGE_BASE;recipe.program.loads[1].address+=USER_IMAGE_BASE;
    recipe.program.relro+=USER_IMAGE_BASE;recipe.bias=0;
    CHECK(aspace_map_private(source.aspace,USER_IMAGE_BASE,TEST_PAGE,ASPACE_PROT_USER|ASPACE_PROT_WRITE));
    memset(&set,0,sizeof(set));set.space=source.active_space;set.parent=&source;
    CHECK(shared_interp_prepare(&source,&target,&set)==EACCDN);shared_interp_abort(&target,&set);
    CHECK(target.pending_space==pending && !source.nheap);
    CHECK(aspace_unmap_private(source.aspace,USER_IMAGE_BASE,TEST_PAGE));
    memset(&set,0,sizeof(set));set.space=source.active_space;set.parent=&source;
    CHECK(shared_interp_prepare(&source,&target,&set)==E_OK);
    CHECK(target.layout.entry==USER_IMAGE_BASE+16);
    ordinary=heap_alloc(&source,TEST_PAGE,UH_MALLOC,NULL,TRUE);
    CHECK(ordinary>=USER_HEAP_BASE);
    {
        KASPACE_HEAP *heap=kaspace_heap(source.active_space);
        CHECK(heap->count==1 && aspace_unmap_private(source.aspace,ordinary,TEST_PAGE));heap->count=0;
    }
    shared_interp_abort(&target,&set);
    CHECK(aspace_unmap_private(original,USER_IMAGE_BASE,2*TEST_PAGE));
    CHECK(aspace_unmap_private(original,INTERPRETER_BASE,TEST_PAGE));
    kaspace_put(pending);kaspace_member_release(&source.mapping_members);
    kaspace_put(source.active_space);kfree(source.heap);
    CHECK(!metadata_live && !physical_live);
    puts("Production interpreted staging: copy/rebase, protected holes, allocation rollback, pending root/PD retry, rebuilt argc/argv/auxv, exact RELRO authority after owner retirement, ET_EXEC collision and fixed-record heap gaps: PASS (host MMU)");
    return 0;
}
