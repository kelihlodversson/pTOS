/* Real loader/KPROC staging and MMU/allocator, with hosted physical providers. */
#define TEST_PHYSICAL_PAGES 2048 /* actual 8 MiB RAM, including production reserve */
#define main borrow_regression_main
#include "procmem_borrow.c"
#undef main
#include "../bdos/kproc.c"
WORD bootdev=2;
#if CONF_WITH_MMAP
void fileback_put(FILE_BACKING *backing) { (void)backing;abort(); }
void fileback_hold(FILE_BACKING *backing) { (void)backing;abort(); }
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
int main(void)
{
    KPROC source={0},target={0};PD source_pd={0},target_pd={0},original_pd;
    struct shared_interp_recipe recipe;
    struct shared_exec_regions set;
    KASPACE *pending;ASPACE *original;UBYTE data[128],copied[128];
    ULONG i;LONG error;VADDR ordinary;static UBYTE environment[2]={0,0};
    PROG_LAYOUT original_layout={0};
    window_phys=(PADDR)(uintptr_t)window;free_pages=WINDOW_PAGES;
    source.number=123;
    source.pd=&source_pd;source.aspace=aspace_create();source.active_space=kaspace_adopt(source.aspace);
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
    kaspace_put(pending);kaspace_put(source.active_space);kfree(source.heap);
    CHECK(!metadata_live && !physical_live);
    puts("Production interpreted staging: copy/rebase, protected holes, allocation rollback, pending root/PD retry, rebuilt argc/argv/auxv, exact RELRO authority after owner retirement, ET_EXEC collision and fixed-record heap gaps: PASS (host MMU)");
    return 0;
}
