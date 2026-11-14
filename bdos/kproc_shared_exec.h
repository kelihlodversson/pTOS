static LONG shared_logical(struct shared_exec_regions *set,KPROC *source,VADDR va,VADDR bytes,UWORD access)
{
    KASPACE_RESERVATION_INFO info;
    LONG error;
    if(!bytes) return E_OK;
    if(set->nlogical==SHARED_REGIONS) return ENSMEM;
    error=kproc_reservation_info(source->pd,va,bytes,&info);
    if(!error) {
        KPROC *owner=kproc_retain_live(source);
#if CONF_WITH_SHARED_VM_PEXEC_TEST
        /* A fresh authenticated image capture may retain its allocation
         * without granting access across protected padding or mixed pages. */
        if(!access && (info.flags&KASPACE_RESERVATION_CAPTURED) &&
            !(info.flags&KASPACE_RESERVATION_LAZY)) {
            info.rights=0;info.flags|=KASPACE_RESERVATION_INACCESSIBLE;
        }
#endif
        if(!owner) return ENSMEM;
        /* Ordinary heap/mmap metadata owns its logical reservation in this
         * KASPACE, including FILE_BACKING and file-COW materialization. Only
         * KPROC-specific views require an immutable captured receipt. */
        if(info.flags&KASPACE_RESERVATION_CAPTURED)
            error=kaspace_reservation_hold_captured(set->space,&info,va,bytes,access,
                        kproc_ref_number(owner),owner,&set->logical[set->nlogical]);
        else error=kaspace_reservation_hold(set->space,info.identity,va,bytes,access,
                        kproc_ref_number(owner),owner,&set->logical[set->nlogical]);
        if(error) kproc_release(owner);else set->nlogical++;
    }
    return error;
}
static LONG shared_source_images(struct shared_exec_regions *set,KPROC *source)
{
    ULONG i;LONG error;
    for(i=0;i<source->nheap;i++)
        if(source->heap[i].kind==UH_SHARED_IMAGE || source->heap[i].kind==UH_IMAGE) {
            /* The exact allocation includes protected gaps. Its lifetime
             * reference conveys no USER grant; live segment/entry witnesses
             * and mapping-owned permissions remain authoritative. */
            error=shared_logical(set,source,source->heap[i].va,
                    (VADDR)source->heap[i].pages*USER_PAGE_SIZE,0);
            if(error) return error;
        }
    return E_OK;
}
/* Generic Pclone compatibility metadata is deliberately supervisor-only.
 * Retain its exact allocation/mapping lifetime without inventing USER rights. */
static BOOL shared_supervisor_compat(KPROC *source,void *base,UQUAD generation)
{
    VADDR page,address=(VADDR)(uintptr_t)base,bytes=procmem_size(base);
    if(!source->clone_compat || source->shared_exec || !source->clone_guarded ||
       source->clone_compat!=source->active_space || !bytes ||
       procmem_owner(base)!=source->pd || procmem_gen(base)!=generation ||
       !((base==source->user_start && generation==source->tpa_gen && (source->clone_maps&1)) ||
         (base==source->env_start && generation==source->env_gen && (source->clone_maps&2)))) return FALSE;
    for(page=address;page<address+bytes;page+=USER_PAGE_SIZE) {
        PADDR physical;UWORD rights;
        if(!aspace_translate(source->aspace,page,&physical,&rights) ||
           physical!=procmem_phys_of(page) || (rights&ASPACE_PROT_USER) ||
           !(rights&ASPACE_PROT_WRITE)) return FALSE;
    }
    return TRUE;
}
static LONG shared_raw(struct shared_exec_regions *set,KPROC *source,void *base,UQUAD generation)
{
    LONG error;ULONG i;
    if(set->nraw==SHARED_RAW) return ENSMEM;
    for(i=0;i<set->nraw;i++) if(set->raw[i].token.base==(VADDR)(uintptr_t)base) return E_OK;
#if CONF_WITH_SHARED_VM_PEXEC_TEST
    if(shared_supervisor_compat(source,base,generation)) {
        KPROC *owner=kproc_retain_live(source);
        if(!owner) return ENSMEM;
        error=procmem_hold_lifetime(base,generation,source->pd,kproc_ref_number(owner),owner,
                                    &set->raw[set->nraw].token)?E_OK:ENSMEM;
        if(error) kproc_release(owner);
    } else
#endif
    if(procmem_owner(base)==source->pd)
        error=kproc_allocation_hold(source->pd,base,generation,0,procmem_size(base),ASPACE_PROT_USER,
                                   &set->raw[set->nraw]);
    else {
        error=EACCDN;
        if(source->shared_exec) for(i=0;i<source->shared_exec->nraw;i++)
            if(source->shared_exec->raw[i].token.base==(VADDR)(uintptr_t)base &&
                source->shared_exec->raw[i].token.allocation==generation) {
                error=kproc_allocation_duplicate(&source->shared_exec->raw[i],&set->raw[set->nraw]);break;
            }
    }
    if(!error) set->nraw++;
    return error;
}
static LONG shared_compat_logical(struct shared_exec_regions *set,KPROC *source,
                                  void *base,UQUAD generation,UQUAD *birth)
{
    KASPACE_RESERVATION_INFO info;KPROC *owner;LONG error;
    VADDR bytes=base==source->user_start?(VADDR)(source->user_end-source->user_start):
                                      (VADDR)(source->env_end-source->env_start);
    if(!shared_supervisor_compat(source,base,generation))
        return shared_logical(set,source,(VADDR)(uintptr_t)base,bytes,ASPACE_PROT_USER);
    if(set->nlogical==SHARED_REGIONS) return ENSMEM;
    if(!*birth && !(*birth=kaspace_reservation_next())) return ENSMEM;
    owner=kproc_retain_live(source);if(!owner) return ENSMEM;
    info.address=(VADDR)(uintptr_t)base;info.bytes=bytes;info.identity=*birth;
    info.rights=0;info.flags=KASPACE_RESERVATION_CAPTURED|KASPACE_RESERVATION_INACCESSIBLE;
    error=kaspace_reservation_hold_captured(set->space,&info,info.address,bytes,0,
                kproc_ref_number(owner),owner,&set->logical[set->nlogical]);
    if(error) kproc_release(owner);else set->nlogical++;
    return error;
}
static void shared_retire_stack(struct shared_exec_regions *set)
{
    KASPACE_HEAP *heap;ULONG i,n;
    if(!set->stack) return;
    heap=kaspace_heap(set->space);
    for(n=0;n<(set->guard_birth?2UL:1UL);n++) {
        VADDR address=set->stack+(n?USER_PAGE_SIZE:0);
        UQUAD birth=(set->guard_birth && !n)?set->guard_birth:set->stack_birth;
        ULONG pages=set->guard_birth?(n?set->stack_pages-1:1):set->stack_pages;
        for(i=0;i<heap->count;i++) if(heap->blocks[i].va==address && heap->blocks[i].reservation==birth) break;
        if(i==heap->count || heap->blocks[i].pages!=pages ||
            !kaspace_retire_heap(set->space,address,birth))
            panic("shared exec stack lost its exact reservation");
    }
    set->stack=0;
}
void kproc_shared_exec_abort(KPROC *process)
{
    struct shared_exec_regions *set=process?process->shared_exec:NULL;
    if(!set) return;
    if(set->committed && process->threads) panic("shared exec release while THREADs can resume");
    process->shared_exec=NULL; /* unpublish before releases may reclaim anything */
    shared_retire_stack(set);
    while(set->nlogical) if(!kproc_reservation_release(&set->logical[--set->nlogical]))
        panic("shared exec reservation released twice");
    kaspace_permission_finish(&set->permissions,FALSE);
#if KPROC_SHARED_INTERP
    shared_interp_abort(process,set);
#endif
    while(set->nraw) if(!kproc_allocation_release(&set->raw[--set->nraw]))
        panic("shared exec allocation released twice");
    kproc_release(set->parent);kaspace_put(set->space);kfree(set);
}
BOOL kproc_shared_exec_ready(const KPROC *p,KTHREAD *thread)
{
    return p && p->shared_exec && !p->shared_exec->committed && p->pd &&
        !p->started && !p->completed && p->disposal_state==PROCESS_LIVE &&
        !p->active_space && !p->pending_space && !p->kstack_phys && !p->clone_identity_only &&
        (thread ? p->threads==thread && !*kthread_next_link(thread) : !p->threads) &&
        procmem_gen(p->user_start)==p->tpa_gen && procmem_gen(p->env_start)==p->env_gen;
}
BOOL kproc_shared_exec_active(PD *pd)
{
    KPROC *p=kproc_find(pd);
    return p && p->shared_exec && p->shared_exec->committed && p->started && !p->completed;
}
/* Source retirement must detach only its mapping view. It does not own an
 * already-running child just because the child's PD allocation was staged. */
BOOL kproc_shared_exec_retire_source(void *base)
{
    KPROC *p;ULONG i;UQUAD generation=procmem_gen(base);
    for(p=kproc_list;p;p=p->next) if(p->shared_exec && p->shared_exec->committed)
        for(i=0;i<p->shared_exec->nraw;i++)
            if(p->shared_exec->raw[i].token.base==(VADDR)(uintptr_t)base &&
                p->shared_exec->raw[i].token.allocation==generation) {
                retire_discard_borrow(base);return TRUE;
            }
    return FALSE;
}
LONG kproc_shared_exec_prepare(KPROC *source,KPROC *target,THREAD_STARTUP_ARGS *startup,
                              KASPACE_PERMISSION_TRANSACTION **permissions)
{
    struct shared_exec_regions *set;
    KASPACE_PROTECTION policy[PROG_MAX_SEGMENTS];
    KASPACE_HEAP *heap;
    struct uheap_block *image=NULL;
    ULONG i;UWORD count=0;LONG error=EACCDN;BOOL legacy=FALSE,interpreted=FALSE;
    VADDR body;UQUAD guard_birth;
#if KPROC_SHARED_INTERP
    interpreted=target && target->standard_entry && target->shared_interp_recipe &&
        target->pending_space && target->pending_as==kaspace_backend(target->pending_space) &&
        !target->load_parent;
#endif
    if(!source || source->pd!=run || !target || target==source || !startup ||
        !permissions || *permissions || !source->active_space || !target->pd ||
        target->creator!=source->pd || target->started || target->completed ||
        target->disposal_state!=PROCESS_LIVE || target->threads || target->shared_exec ||
        target->active_space || (target->pending_space && !interpreted) || target->kstack_phys || target->parent_identity ||
        procmem_gen(target->user_start)!=target->tpa_gen ||
        procmem_gen(target->env_start)!=target->env_gen ||
        (VADDR)target->pd->p_lowtpa!=(VADDR)(uintptr_t)target->user_start ||
        (VADDR)target->pd->p_env!=(VADDR)(uintptr_t)target->env_start ||
        (VADDR)target->pd->p_hitpa<(VADDR)(uintptr_t)(target->user_start+sizeof(PD)) ||
        (VADDR)target->pd->p_hitpa>(VADDR)(uintptr_t)target->user_start+procmem_size(target->user_start)) return EIMBA;
#if KPROC_LEGACY_PRG
    legacy=target->legacy && target->legacy_text_recorded && !target->has_image &&
        !target->load_parent && target->legacy_text_bytes &&
        target->legacy_text_bytes<=(VADDR)(target->user_end-target->user_start)-sizeof(PD);
#endif
    if(!legacy && !interpreted) {
        if(!target->has_image || target->load_parent!=source->pd) return EINVFN;
        image=heap_find(source,target->load_va);
        if(!image || image->kind!=UH_LOADED || image->pd!=target->pd || image->pages!=target->load_pages ||
            !layout_valid_in(&target->layout,FALSE,image->va,image->va+(VADDR)image->pages*USER_PAGE_SIZE)) return EIMBA;
    }
    if(shared_failure_point()) return ENSMEM;
    set=kalloc(sizeof(*set));if(!set) return ENSMEM;memset(set,0,sizeof(*set));
    set->space=kaspace_share(source->active_space);set->parent=kproc_retain_live(source);
    set->legacy=legacy;
    set->inherited_stack=stack_bytes(source);
    set->inherited_core=source->lim_core;set->inherited_data=source->lim_data;
    set->inherited_priority=source->prio;
    target->shared_exec=set;
    if(!set->space || !set->parent) { error=ENSMEM;goto failed; }
    if(!kproc_user_access(source->pd,(VADDR)(uintptr_t)target->pd,sizeof(PD),TRUE)) {error=EACCDN;goto failed;}
#define SHARED_TRY(call) do {error=shared_failure_point()?ENSMEM:(call);if(error) goto failed;}while(0)
#if KPROC_SHARED_INTERP
    if(interpreted) SHARED_TRY(shared_interp_prepare(source,target,set));
#endif
    /* The borrowed-text transaction authenticates an already-owned R1 ticket.
     * Keep that exact physical allocation held until commit or full rollback. */
    SHARED_TRY(shared_raw(set,source,target->user_start,target->tpa_gen));
    if(legacy) {
#if KPROC_LEGACY_PRG
        VADDR base=(VADDR)(uintptr_t)target->user_start;
        VADDR bytes=procmem_size(target->user_start);
        VADDR text_end=(base+sizeof(PD)+target->legacy_text_bytes+USER_PAGE_SIZE-1)&~(VADDR)(USER_PAGE_SIZE-1);
        policy[0].address=base;policy[0].bytes=text_end-base;
        policy[0].rights=ASPACE_PROT_USER|ASPACE_PROT_WRITE|ASPACE_PROT_EXEC;count=1;
        if(text_end<base+bytes) {
            policy[1].address=text_end;policy[1].bytes=base+bytes-text_end;
            policy[1].rights=ASPACE_PROT_USER|ASPACE_PROT_WRITE;count++;
        }
        SHARED_TRY(kaspace_permission_prepare_procmem(set->space,&set->raw[0].token,
                    policy,count,&set->permissions));
#else
        error=EINVFN;goto failed;
#endif
    } else if(!interpreted) {
        for(i=0;i<target->layout.nseg;i++) {
            const PROG_SEGMENT *s=&target->layout.seg[i];
            if(!s->memsz) continue;
            policy[count].address=s->vaddr;policy[count].bytes=s->memsz;
            policy[count].rights=ASPACE_PROT_USER|((s->flags&1)?ASPACE_PROT_EXEC:0)|((s->flags&2)?ASPACE_PROT_WRITE:0);count++;
        }
        SHARED_TRY(kaspace_permission_prepare(set->space,policy,count,&set->permissions));
    }
    SHARED_TRY(shared_raw(set,source,target->env_start,target->env_gen));
    SHARED_TRY(shared_raw(set,source,source->user_start,source->tpa_gen));
    SHARED_TRY(shared_raw(set,source,source->env_start,source->env_gen));
    SHARED_TRY(shared_logical(set,source,(VADDR)(uintptr_t)target->user_start,
                              (VADDR)(target->user_end-target->user_start),ASPACE_PROT_USER));
    SHARED_TRY(shared_logical(set,source,(VADDR)(uintptr_t)target->env_start,
                              (VADDR)(target->env_end-target->env_start),ASPACE_PROT_USER));
#if KPROC_SHARED_INTERP
    if(interpreted) set->image_hold=set->nlogical;
#endif
    if(!legacy) {
        SHARED_TRY(shared_logical(set,source,target->load_va,
                    (VADDR)target->load_pages*USER_PAGE_SIZE,0));
        for(i=0;i<target->layout.nseg;i++) if(target->layout.seg[i].memsz)
            SHARED_TRY(shared_logical(set,source,target->layout.seg[i].vaddr,
                        target->layout.seg[i].memsz,ASPACE_PROT_USER));
#if CONF_WITH_ELF_SHARED
        if(interpreted) for(i=0;i<target->interpreter_layout.nseg;i++)
            if(target->interpreter_layout.seg[i].memsz)
                SHARED_TRY(shared_logical(set,source,target->interpreter_layout.seg[i].vaddr,
                            target->interpreter_layout.seg[i].memsz,ASPACE_PROT_USER));
#endif
    }
    SHARED_TRY(shared_compat_logical(set,source,source->user_start,source->tpa_gen,
                                   &source->reservation_birth[0]));
    SHARED_TRY(shared_compat_logical(set,source,source->env_start,source->env_gen,
                                   &source->reservation_birth[1]));
    if(!source->legacy && owns_top_stack(source)) SHARED_TRY(shared_logical(set,source,USER_STACK_TOP-stack_bytes(source),stack_bytes(source),ASPACE_PROT_USER));
    if(source->has_image) for(i=0;i<source->layout.nseg;i++)
        if(source->layout.seg[i].memsz) SHARED_TRY(shared_logical(set,source,source->layout.seg[i].vaddr,
                                                               source->layout.seg[i].memsz,ASPACE_PROT_USER));
#if CONF_WITH_ELF_SHARED
    if(source->has_image) for(i=0;i<source->interpreter_layout.nseg;i++)
        if(source->interpreter_layout.seg[i].memsz) SHARED_TRY(shared_logical(set,source,source->interpreter_layout.seg[i].vaddr,
                source->interpreter_layout.seg[i].memsz,ASPACE_PROT_USER));
#endif
    if(source->shared_exec) for(i=0;i<source->shared_exec->nlogical;i++) {
        if(set->nlogical==SHARED_REGIONS) {error=ENSMEM;goto failed;}
        SHARED_TRY(kproc_reservation_duplicate(&source->shared_exec->logical[i],&set->logical[set->nlogical]));set->nlogical++;
    }
    SHARED_TRY(shared_source_images(set,source));
    heap=kaspace_heap(set->space);
    for(i=0;i<heap->count;i++) SHARED_TRY(shared_logical(set,source,heap->blocks[i].va,
                                                     (VADDR)heap->blocks[i].pages*USER_PAGE_SIZE,
                                                     (heap->blocks[i].flags&KASPACE_HEAP_GUARD)?0:ASPACE_PROT_USER));
#if CONF_WITH_MMAP
    {
        MMAP_REGION *region;
        for(region=kaspace_mmap(set->space)->regions;region;region=region->next) {
            /* PROT_NONE is still a canonical reservation. Retain its exact
             * lazy/inaccessible ownership without acquiring any USER grant. */
            if(region->prot && !(region->prot&ASPACE_PROT_USER)) { error=EINVFN;goto failed; }
            SHARED_TRY(shared_logical(set,source,region->address,region->length,
                                      region->prot?ASPACE_PROT_USER:0));
        }
    }
#endif
    set->stack_bytes=SHARED_STACK_BYTES;
#if KPROC_SHARED_INTERP
    if(interpreted) set->stack_bytes+=DL_START_CAPACITY;
#endif
    if(set->stack_bytes>set->inherited_stack) set->stack_bytes=set->inherited_stack;
    set->stack_pages=set->stack_bytes/USER_PAGE_SIZE+1;
    set->stack=shared_failure_point()?0:kproc_uheap_alloc(source->pd,set->stack_pages*USER_PAGE_SIZE);
    if(!set->stack) {error=ENSMEM;goto failed;}
    heap=kaspace_heap(set->space);
    for(i=0;i<heap->count;i++) if(heap->blocks[i].va==set->stack) break;
    if(i==heap->count) panic("new shared exec stack missing");
    set->stack_birth=heap->blocks[i].reservation;
    if(shared_failure_point() || !ordinary_heap_room(source) || !(guard_birth=kaspace_reservation_next())) {error=ENSMEM;goto failed;}
    heap=kaspace_heap(set->space); /* growth may replace the vector */
    if(!aspace_release_lazy(kaspace_backend(set->space),set->stack,USER_PAGE_SIZE)) {error=EACCDN;goto failed;}
    body=set->stack+USER_PAGE_SIZE;
    memmove(&heap->blocks[i+1],&heap->blocks[i],(heap->count-i)*sizeof(heap->blocks[0]));heap->count++;
    heap->blocks[i].va=set->stack;heap->blocks[i].pages=1;heap->blocks[i].reservation=guard_birth;
    heap->blocks[i].flags=KASPACE_HEAP_GUARD;
    heap->blocks[i+1].va=body;heap->blocks[i+1].pages=set->stack_pages-1;
    set->guard_birth=guard_birth;
    for(i=0;i<set->stack_bytes/USER_PAGE_SIZE;i++) {
        VADDR page=body+(VADDR)i*USER_PAGE_SIZE;
        int backed=shared_failure_point()?-1:aspace_translate(kaspace_backend(set->space),page,NULL,NULL)?1:
            kaspace_heap_demand_page(set->space,page,ASPACE_PROT_WRITE);
        if(backed!=1) {error=ENSMEM;goto failed;}
    }
    SHARED_TRY(shared_logical(set,source,set->stack,USER_PAGE_SIZE,0));
    SHARED_TRY(shared_logical(set,source,body,set->stack_bytes,ASPACE_PROT_USER|ASPACE_PROT_WRITE));
    startup->entry=legacy?(VADDR)(uintptr_t)target->user_start+sizeof(PD):target->layout.entry;startup->argument=(VADDR)(uintptr_t)target->pd;
    startup->stack_base=body;startup->stack_size=set->stack_bytes;
#if KPROC_SHARED_INTERP
    if(interpreted) {
        SHARED_TRY(shared_interp_stack(target,set,body));
        startup->entry=target->interp_entry;
    }
#endif
    return E_OK;
failed:
    kproc_shared_exec_abort(target);return error;
#undef SHARED_TRY
}
static void shared_exec_commit(KPROC *target)
{
    struct shared_exec_regions *set=target->shared_exec;
    KPROC *source;struct uheap_block *image;
    if(!set || set->committed || !(source=set->parent) || !source->pd)
        panic("shared exec staging changed before publication");
    if(!set->legacy) {
        image=heap_find(source,target->load_va);
        if(!image || image->pd!=target->pd || image->kind!=UH_LOADED)
            panic("shared exec image changed before publication");
        image->kind=UH_SHARED_IMAGE;image->shared_generation=target->tpa_gen;
        target->load_parent=NULL;
    }
    target->stack_size=set->inherited_stack;
    target->lim_core=set->inherited_core;target->lim_data=set->inherited_data;
    target->prio=set->inherited_priority;
    target->parent_identity=set->parent;set->parent=NULL;
    set->committed=TRUE;
}
void kproc_shared_exec_release_base(void *base)
{
    KPROC *p;ULONG i;UQUAD generation=procmem_gen(base);
    for(p=kproc_list;p;p=p->next) for(i=0;i<p->nheap;i++) {
        struct uheap_block *b=&p->heap[i];
        if(b->kind==UH_SHARED_IMAGE && b->pd==base && b->shared_generation==generation) {
            if(!kaspace_retire_private(p->active_space,b->va,(VADDR)b->pages*USER_PAGE_SIZE))
                panic("shared image has no final retirement owner");
            memmove(b,b+1,(p->nheap-i-1)*sizeof(*b));p->nheap--;return;
        }
    }
}
#undef SHARED_RAW
#undef SHARED_REGIONS
#undef SHARED_STACK_BYTES

void kproc_shared_exec_finish(KPROC *process)
{
    if(!process || !process->shared_exec || !process->shared_exec->committed)
        panic("shared exec permission commit without publication");
    kaspace_permission_finish(&process->shared_exec->permissions,TRUE);
#if KPROC_SHARED_INTERP
    kaspace_put(process->shared_exec->original_pending);
    process->shared_exec->original_pending=NULL;
#endif
}
VADDR kproc_shared_exec_initial_sp(const KPROC *process)
{
#if KPROC_SHARED_INTERP
    if(process && process->shared_exec && process->shared_exec->interpreted)
        return process->shared_exec->initial_sp;
#else
    UNUSED(process);
#endif
    return 0;
}

BOOL kproc_shared_exec_relro_range(const KPROC *process,VADDR address,VADDR bytes)
{
#if KPROC_SHARED_INTERP
    ULONG i;KPROC *owner;VADDR image_bytes;
    const struct shared_exec_regions *set=process?process->shared_exec:NULL;
    if(!set || !set->committed || !set->interpreted || !process->standard_entry ||
        !process->started || process->completed || process->active_space!=set->space ||
        process->aspace!=kaspace_backend(set->space) || !bytes || address>=USER_VA_LIMIT || bytes>USER_VA_LIMIT-address ||
        set->image_hold>=set->nlogical || !set->provisional_birth) return FALSE;
    image_bytes=(VADDR)set->provisional_pages*USER_PAGE_SIZE;
    if(address<set->provisional_va || address-set->provisional_va>=image_bytes ||
        bytes>image_bytes-(address-set->provisional_va)) return FALSE;
    /* The exact ownership ticket survives original owner retirement. Its
     * identity prefix and canonical birth authenticate this allocation without
     * requiring a live source PD or changing any immutable reservation grant. */
    owner=kaspace_reservation_owner(&set->logical[set->image_hold]);
    if(!owner || kproc_ref_number(owner)!=set->image_owner ||
        !kaspace_reservation_matches(&set->logical[set->image_hold],set->space,
                    set->provisional_birth,set->provisional_va,image_bytes)) return FALSE;
    for(i=0;i<2;i++) if(address>=set->relro_address[i] &&
        address-set->relro_address[i]<set->relro_bytes[i] &&
        bytes<=set->relro_bytes[i]-(address-set->relro_address[i])) return TRUE;
#else
    UNUSED(process);UNUSED(address);UNUSED(bytes);
#endif
    return FALSE;
}

BOOL kproc_shared_exec_committed(const KPROC *process)
{ return process && process->shared_exec && process->shared_exec->committed; }
