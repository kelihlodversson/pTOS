/* Test-only interpreted PE_LOAD staging. The existing KPROC heap record owns
 * the provisional extent; the original private root remains intact until the
 * ordinary clone publication succeeds. No file parser or allocator lives here. */
static BOOL shared_overlap(VADDR a,VADDR bytes,VADDR b,VADDR length)
{ return bytes && length && a<b+length && b<a+bytes; }

/* Fixed ET_EXEC placement uses the same loader heap record, reservation birth
 * and private-page factory as heap_alloc. Authenticate all existing lazy and
 * resident owners before publishing any leaf or loader record. */
static VADDR shared_interp_fixed(KPROC *source,PD *child,VADDR va,ULONG bytes)
{
    KASPACE_HEAP *heap=kaspace_heap(source->active_space);
    ULONG i,index=0;VADDR page;UQUAD birth;
    if(!bytes || (va&(USER_PAGE_SIZE-1)) || (bytes&(USER_PAGE_SIZE-1)) ||
        va>=USER_VA_LIMIT || bytes>USER_VA_LIMIT-va ||
        !heap_room(source) || !kaspace_mapping_can_change(source->aspace,va,bytes)) return 0;
    for(i=0;i<source->nheap;i++)
        if(shared_overlap(va,bytes,source->heap[i].va,(VADDR)source->heap[i].pages*USER_PAGE_SIZE)) return 0;
    for(i=0;i<heap->count;i++)
        if(shared_overlap(va,bytes,heap->blocks[i].va,(VADDR)heap->blocks[i].pages*USER_PAGE_SIZE)) return 0;
    if(!source->legacy && shared_overlap(va,bytes,USER_STACK_TOP-stack_bytes(source)-USER_PAGE_SIZE,
                                        stack_bytes(source)+USER_PAGE_SIZE)) return 0;
#if CONF_WITH_MMAP
    {
        MMAP_REGION *r;
        for(r=kaspace_mmap(source->active_space)->regions;r;r=r->next)
            if(shared_overlap(va,bytes,r->address,r->length)) return 0;
    }
#endif
    for(page=va;page<va+bytes;page+=USER_PAGE_SIZE)
        if(aspace_translate(source->aspace,page,NULL,NULL)) return 0;
    birth=kaspace_reservation_next();if(!birth) return 0;
    if(!aspace_map_private(source->aspace,va,bytes,ASPACE_PROT_USER|ASPACE_PROT_WRITE)) return 0;
    while(index<source->nheap && source->heap[index].va<va) index++;
    memmove(&source->heap[index+1],&source->heap[index],(source->nheap-index)*sizeof(source->heap[0]));
    source->heap[index].va=va;source->heap[index].pages=bytes/USER_PAGE_SIZE;
    source->heap[index].kind=UH_LOADED;source->heap[index].pd=child;
    source->heap[index].reservation=birth;source->heap[index].shared_generation=0;source->nheap++;
    return va;
}
static BOOL shared_interp_copy(ASPACE *from,ASPACE *to,const DL_IMAGE *plan,
                               ULONG old_bias,ULONG bias,PROG_LAYOUT *layout,UBYTE *scratch)
{
    ULONG i;VADDR at,count;
    memset(layout,0,sizeof(*layout));
    layout->entry=(VADDR)plan->entry+bias;layout->end=(VADDR)plan->base+bias+plan->size;
    layout->nseg=plan->nload;
    for(i=0;i<plan->nload;i++) {
        const DL_LOAD *load=&plan->loads[i];PROG_SEGMENT *seg=&layout->seg[i];
        seg->vaddr=(VADDR)load->address+bias;seg->memsz=load->memsz;
        seg->filesz=load->filesz;seg->flags=load->flags;seg->src=NULL;
        for(at=0;at<load->memsz;at+=count) {
            count=load->memsz-at;if(count>USER_PAGE_SIZE) count=USER_PAGE_SIZE;
            if(!aspace_copy_from_user(from,scratch,(VADDR)load->address+old_bias+at,count) ||
                !aspace_copy_to_user(to,(VADDR)load->address+bias+at,scratch,count)) return FALSE;
        }
    }
    return TRUE;
}
static LONG shared_interp_prepare(KPROC *source,KPROC *target,struct shared_exec_regions *set)
{
    const struct shared_interp_recipe *recipe=target->shared_interp_recipe;
    const DL_IMAGE *program=&recipe->program,*interp=&recipe->interpreter;
    UQUAD bytes,at,bias,ibias,end;VADDR va;UBYTE *scratch;
    PROG_LAYOUT layout,interpreter;
    struct uheap_block *block;
    set->interpreted=TRUE;
    set->original_layout=target->layout;set->original_interpreter=target->interpreter_layout;
    set->original_pd=*target->pd;set->original_initial_rsp=target->initial_rsp;
    set->original_interp_entry=target->interp_entry;set->original_interp_bytes=target->interp_bytes;
    set->original_stack_low=target->stack_low;set->original_entry=target->entry;
    set->original_stack_top=target->stack_top;
    /* The existing owning reference moves to the transaction, never to the
     * source root. Abort puts exactly this reference and metadata back. */
    set->original_pending=target->pending_space;target->pending_space=NULL;target->pending_as=NULL;
    if(program->type==3) {
        bytes=(UQUAD)program->size+interp->size+program->align-1+interp->align-1;
        bytes=(bytes+USER_PAGE_SIZE-1)&~(UQUAD)(USER_PAGE_SIZE-1);
        if(bytes>USER_HEAP_LIMIT-USER_HEAP_BASE || bytes>(ULONG)~0UL) return ENSMEM;
        va=heap_alloc(source,(ULONG)bytes,UH_LOADED,target->pd,TRUE);
        if(!va) return ENSMEM;
        bias=((UQUAD)va-program->base+program->align-1)&~(UQUAD)(program->align-1);
    } else {
        bias=0;at=(UQUAD)program->base+program->size;
        if(at<interp->base) at=interp->base;
        ibias=(at-interp->base+interp->align-1)&~(UQUAD)(interp->align-1);
        end=(UQUAD)interp->base+ibias+interp->size;bytes=end-program->base;
        if(end>=USER_VA_LIMIT || bytes>(ULONG)~0UL) return ENSMEM;
        va=shared_interp_fixed(source,target->pd,program->base,(ULONG)bytes);
        if(!va) return EACCDN;
    }
    block=heap_find(source,va);
    if(!block) panic("interpreted provisional image missing");
    set->provisional_va=va;set->provisional_pages=block->pages;
    at=(UQUAD)program->base+bias+program->size;
    if(at<interp->base) at=interp->base;
    ibias=(at-interp->base+interp->align-1)&~(UQUAD)(interp->align-1);
    end=(UQUAD)interp->base+ibias+interp->size;
    if(bias>0xffffffffUL || ibias>0xffffffffUL || end>(UQUAD)va+bytes) return EPLFMT;
    scratch=kalloc(USER_PAGE_SIZE);if(!scratch) return ENSMEM;
    if(!shared_interp_copy(kaspace_backend(set->original_pending),source->aspace,program,recipe->bias,(ULONG)bias,&layout,scratch) ||
       !shared_interp_copy(kaspace_backend(set->original_pending),source->aspace,interp,recipe->ibias,(ULONG)ibias,&interpreter,scratch)) {
        kfree(scratch);return EIMBA;
    }
    kfree(scratch);
    /* Padding and holes retain ownership while granting no user access. */
    if(!aspace_protect_private(source->aspace,va,bytes,0) ||
       !protect_layout(source->aspace,&layout) || !protect_layout(source->aspace,&interpreter)) return EACCDN;
    set->relro_address[0]=(VADDR)program->relro+bias;set->relro_bytes[0]=program->relro_size;
    set->relro_address[1]=(VADDR)interp->relro+ibias;set->relro_bytes[1]=interp->relro_size;
    target->layout=layout;target->interpreter_layout=interpreter;
    target->interp_entry=(VADDR)interp->entry+ibias;target->entry=target->interp_entry;
    target->load_parent=source->pd;target->load_va=va;target->load_pages=block->pages;
    target->pd->p_tbase=PTR_TO_USERPTR_UNCHECKED((UBYTE *)(uintptr_t)layout.entry);
    target->pd->p_dbase=target->pd->p_bbase=PTR_TO_USERPTR_UNCHECKED((UBYTE *)(uintptr_t)layout.end);
    return E_OK;
}
static LONG shared_interp_stack(KPROC *target,struct shared_exec_regions *set,VADDR body)
{
    const struct shared_interp_recipe *recipe=target->shared_interp_recipe;
    DL_START_INFO info;uint32_t rsp=0,used=0;UBYTE *stack;int status;
    ULONG bias=(ULONG)(target->layout.entry-recipe->program.entry);
    ULONG ibias=(ULONG)(target->interp_entry-recipe->interpreter.entry);
    stack=kalloc(DL_START_CAPACITY);if(!stack) return ENSMEM;
    memset(&info,0,sizeof(info));info.phdr=recipe->program.phdr+bias;
    info.phnum=recipe->program.phnum;info.entry=target->layout.entry;
    info.base=ibias;info.pagesz=USER_PAGE_SIZE;info.basepage=(ULONG)(uintptr_t)target->pd;
    info.bootdrive=bootdev;info.load_bias=bias;
#if CONF_WITH_LIBAES
    info.capture_launch=1;info.environment=(ULONG)(uintptr_t)target->env_start;
#endif
    status=dl_start_stack(stack,DL_START_CAPACITY,body+set->stack_bytes,
        recipe->filename,strlen(recipe->filename)+1,(const UBYTE *)target->pd->p_cmdlin,
        sizeof(target->pd->p_cmdlin),target->env_start,target->env_end-target->env_start,&info,&rsp,&used);
    if(status!=DL_OK || !aspace_copy_to_user(kaspace_backend(set->space),rsp,
                    stack+DL_START_CAPACITY-used,used)) {
        kfree(stack);return status==DL_NO_SPACE?ENSMEM:EPLFMT;
    }
    kfree(stack);set->initial_sp=rsp;target->initial_rsp=rsp;
    target->stack_top=rsp;target->stack_low=body;return E_OK;
}
static void shared_interp_abort(KPROC *target,struct shared_exec_regions *set)
{
    KPROC *source=set->parent;struct uheap_block *block;
    if(!set->interpreted || set->committed) return;
    if(set->provisional_va) {
        block=heap_find(source,set->provisional_va);
        if(!block || block->kind!=UH_LOADED || block->pd!=target->pd ||
            block->pages!=set->provisional_pages || heap_release(source,block)!=E_OK)
            panic("interpreted provisional image rollback changed");
    }
    target->pending_space=set->original_pending;set->original_pending=NULL;
    target->pending_as=kaspace_backend(target->pending_space);
    target->layout=set->original_layout;target->interpreter_layout=set->original_interpreter;
    target->initial_rsp=set->original_initial_rsp;target->interp_entry=set->original_interp_entry;
    target->interp_bytes=set->original_interp_bytes;target->stack_low=set->original_stack_low;
    target->entry=set->original_entry;target->stack_top=set->original_stack_top;
    target->load_parent=NULL;target->load_va=0;target->load_pages=0;
#define RESTORE_PD(field) target->pd->field=set->original_pd.field
    RESTORE_PD(p_tbase);RESTORE_PD(p_tlen);RESTORE_PD(p_dbase);RESTORE_PD(p_dlen);RESTORE_PD(p_bbase);RESTORE_PD(p_blen);
#undef RESTORE_PD
}
