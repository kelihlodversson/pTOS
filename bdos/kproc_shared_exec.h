static struct shared_exec_regions *shared_regions(const KPROC *p)
{
    if(p->shared_exec && p->shared_inherited) panic("duplicate special execution region owner");
    return p->shared_exec?p->shared_exec:p->shared_inherited;
}
#if CONF_WITH_SHARED_VM_PEXEC || CONF_WITH_PRIVATE_CLONE
static LONG shared_logical(struct shared_exec_regions *set,KPROC *source,VADDR va,VADDR bytes,UWORD access)
{
    KASPACE_RESERVATION_INFO info;
    LONG error;
    if(!bytes) return E_OK;
    if(set->nlogical==SHARED_REGIONS) return ENSMEM;
    error=kproc_reservation_info(source->pd,va,bytes,&info);
    if(!error) {
        KPROC *owner=kproc_retain_live(source);
        /* A fresh authenticated image capture may retain its allocation
         * without granting access across protected padding or mixed pages. */
        if(!access && (info.flags&KASPACE_RESERVATION_CAPTURED) &&
            !(info.flags&KASPACE_RESERVATION_LAZY)) {
            info.rights=0;info.flags|=KASPACE_RESERVATION_INACCESSIBLE;
        }
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
/* bytes: the held extent from the start of the allocation, 0 for all of it */
static LONG shared_raw_extent(struct shared_exec_regions *set,KPROC *source,void *base,
                              UQUAD generation,VADDR bytes)
{
    LONG error;ULONG i;
    struct shared_exec_regions *inherited=shared_regions(source);
    for(i=0;i<set->nraw;i++) if(set->raw[i].token.base==(VADDR)(uintptr_t)base)
        return set->raw[i].token.allocation==generation?E_OK:EACCDN;
    if(set->nraw==SHARED_RAW) return ENSMEM;
    if(shared_supervisor_compat(source,base,generation)) {
        KPROC *owner=kproc_retain_live(source);
        if(!owner) return ENSMEM;
        error=procmem_hold_lifetime(base,generation,source->pd,kproc_ref_number(owner),owner,
                                    &set->raw[set->nraw].token)?E_OK:ENSMEM;
        if(error) kproc_release(owner);
    } else
    if(procmem_owner(base)==source->pd)
        error=kproc_allocation_hold(source->pd,base,generation,0,bytes?bytes:procmem_size(base),
                                   ASPACE_PROT_USER,&set->raw[set->nraw]);
    else {
        error=EACCDN;
        if(inherited) for(i=0;i<inherited->nraw;i++)
            if(inherited->raw[i].token.base==(VADDR)(uintptr_t)base &&
                inherited->raw[i].token.allocation==generation) {
                error=kproc_allocation_duplicate(&inherited->raw[i],&set->raw[set->nraw]);break;
            }
    }
    if(!error) set->nraw++;
    return error;
}
static LONG shared_raw(struct shared_exec_regions *set,KPROC *source,void *base,UQUAD generation)
{
    return shared_raw_extent(set,source,base,generation,0);
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
#endif
static void shared_regions_release(KPROC *process,struct shared_exec_regions *set)
{
    if(!set) return;
    if(set->committed && process->threads) panic("shared exec release while THREADs can resume");
    while(set->nlogical) if(!kproc_reservation_release(&set->logical[--set->nlogical]))
        panic("shared exec reservation released twice");
    kaspace_permission_finish(&set->permissions,FALSE);
    while(set->nraw) if(!kproc_allocation_release(&set->raw[--set->nraw]))
        panic("shared exec allocation released twice");
    kproc_abort_mapping_owners(process,set->space,&set->mapping_members);
    kproc_release(set->parent);kaspace_put(set->space);kfree(set);
}
void kproc_shared_exec_abort(KPROC *process)
{
    struct shared_exec_regions *set=process?shared_regions(process):NULL;
    if(!set) return;
    process->shared_exec=process->shared_inherited=NULL;
    shared_regions_release(process,set); /* close admission before callbacks */
}
#if CONF_WITH_SHARED_VM_PEXEC || CONF_WITH_PRIVATE_CLONE
LONG kproc_shared_dependencies_prepare(KPROC *target,KPROC *source)
{
    struct shared_exec_regions *set,*inherited;
    ULONG i;
    LONG error=ENSMEM;
    if(!target || !source || target==source || target->shared_exec || target->shared_inherited ||
        target->threads || target->mapping_preparation!=MAPPING_PREPARED ||
        !source->active_space || !mapping_owner_live(source)) return EACCDN;
#if !CONF_WITH_SHARED_VM_PEXEC
    /* Native private snapshots carry release authority for compatibility
     * pages and an optional lazy top stack. A new PROCESS sharing that
     * root must retain those dependencies before the snapshot owner exits. */
    if (!source->shared_inherited && !kproc_private_image_addresses(source,NULL,NULL,NULL))
        return E_OK;
#endif
    set=shared_dependency_point()?NULL:kalloc(sizeof(*set));if(!set) return ENSMEM;
    memset(set,0,sizeof(*set));
    set->space=shared_dependency_point()?NULL:kaspace_share(source->active_space);
    if(!set->space) { kfree(set);return ENSMEM; }
    inherited=shared_regions(source);
    if(inherited) {
        for(i=0;i<inherited->nraw;i++) {
            error=shared_dependency_point()?ENSMEM:
                kproc_allocation_duplicate(&inherited->raw[i],&set->raw[set->nraw]);
            if(error) goto failed;
            set->nraw++;
        }
        for(i=0;i<inherited->nlogical;i++) {
            error=shared_dependency_point()?ENSMEM:
                kproc_reservation_duplicate(&inherited->logical[i],&set->logical[set->nlogical]);
            if(error) goto failed;
            set->nlogical++;
        }
    }
#define DEPENDENCY(call) do {error=shared_dependency_point()?ENSMEM:(call);if(error) goto failed;}while(0)
    /* A generic source's own compatibility PD/env are distinct from its
     * ancestor's receipts. Own exactly those dependencies as well. */
    DEPENDENCY(shared_raw(set,source,source->user_start,source->tpa_gen));
    DEPENDENCY(shared_raw(set,source,source->env_start,source->env_gen));
    DEPENDENCY(shared_compat_logical(set,source,source->user_start,source->tpa_gen,&source->reservation_birth[0]));
    DEPENDENCY(shared_compat_logical(set,source,source->env_start,source->env_gen,&source->reservation_birth[1]));
#if CONF_WITH_PRIVATE_CLONE
    /* Private snapshots own original compatibility pages as private frames,
     * separately from their kernel PD/env allocations. Keep their logical
     * authority across shared descendants and source retirement. */
    if(source->private_image && source->private_image->committed) {
        if(source->logical_bp_bytes)
            DEPENDENCY(shared_logical(set,source,source->logical_basepage,source->logical_bp_bytes,ASPACE_PROT_USER));
        if(source->logical_env_bytes)
            DEPENDENCY(shared_logical(set,source,source->logical_environment,source->logical_env_bytes,ASPACE_PROT_USER));
    }
#endif
    if(!inherited) {
        /* A private PRG snapshot preserves legacy image policy as well as
         * its independently owned lazy top-stack authority. Image format
         * must not discard that authority on a subsequent shared clone. */
        if(owns_top_stack(source) && (!source->legacy
#if CONF_WITH_PRIVATE_CLONE
            || (source->private_image && source->private_image->committed)
#endif
            ))
            DEPENDENCY(shared_logical(set,source,USER_STACK_TOP-stack_bytes(source),stack_bytes(source),ASPACE_PROT_USER));
    }
    if(source->has_image) {
        for(i=0;i<source->layout.nseg;i++) if(source->layout.seg[i].memsz)
            DEPENDENCY(shared_logical(set,source,source->layout.seg[i].vaddr,
                                        source->layout.seg[i].memsz,ASPACE_PROT_USER));
#if CONF_WITH_ELF_SHARED
        for(i=0;i<source->interpreter_layout.nseg;i++) if(source->interpreter_layout.seg[i].memsz)
            DEPENDENCY(shared_logical(set,source,source->interpreter_layout.seg[i].vaddr,
                                        source->interpreter_layout.seg[i].memsz,ASPACE_PROT_USER));
#endif
    }
    if(shared_dependency_point()) {error=ENSMEM;goto failed;}
    target->shared_inherited=set; /* prepared, not committed/runnable */
    return E_OK;
failed:
    shared_regions_release(target,set);
    return error;
#undef DEPENDENCY
}
#endif
BOOL kproc_shared_exec_ready(const KPROC *p,KTHREAD *thread)
{
    BOOL pending_ok=p && !p->pending_space;
    return p && p->shared_exec && !p->shared_exec->committed && p->pd &&
        !p->started && !p->completed && p->disposal_state==PROCESS_LIVE &&
        !p->active_space && pending_ok && !p->kstack_phys && !p->clone_identity_only &&
        (thread ? p->threads==thread && !*kthread_next_link(thread) : !p->threads) &&
        procmem_gen(p->user_start)==p->tpa_gen && procmem_gen(p->env_start)==p->env_gen;
}
LONG kproc_shared_exec_take_mapping_owners(KPROC *p,KASPACE_MEMBER **result)
{
    struct shared_exec_regions *set=p?p->shared_exec:NULL;
    if(!set || set->preparing || set->committed || !result || *result ||
        !set->mapping_members || p->prepared_mapping_members!=set->mapping_members ||
        p->mapping_preparation!=MAPPING_PREPARED) return EACCDN;
    *result=set->mapping_members;set->mapping_members=NULL;
    return E_OK;
}
BOOL kproc_shared_exec_constructible(const KPROC *p)
{
    return kproc_shared_exec_ready(p,NULL) && !p->shared_exec->preparing &&
        p->shared_exec->mapping_members &&
        p->prepared_mapping_members==p->shared_exec->mapping_members &&
        p->mapping_preparation==MAPPING_PREPARED;
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
    for(p=kproc_list;p;p=p->next) {
        struct shared_exec_regions *set=shared_regions(p);
        if(!set || !set->committed) continue;
        for(i=0;i<set->nraw;i++)
            if(set->raw[i].token.base==(VADDR)(uintptr_t)base &&
                set->raw[i].token.allocation==generation) {
                retire_discard_borrow(base);return TRUE;
            }
    }
    return FALSE;
}
#if CONF_WITH_SHARED_VM_PEXEC
#define SHARED_START_FRAME 0x28UL /* above the startup sp: return PC, basepage */
LONG kproc_shared_exec_prepare(KPROC *source,KPROC *target,THREAD_STARTUP_ARGS *startup,
                              KASPACE_PERMISSION_TRANSACTION **permissions)
{
    struct shared_exec_regions *set,*inherited;
    KASPACE_PROTECTION policy[PROG_MAX_SEGMENTS];
    ULONG i;UWORD count=0;LONG error=EACCDN;BOOL legacy=FALSE;
    VADDR held;
    if(!source || source->pd!=run || !target || target==source || !startup ||
        !permissions || *permissions || !source->active_space || !target->pd ||
        target->creator!=source->pd || target->started || target->completed ||
        target->disposal_state!=PROCESS_LIVE || target->threads || target->shared_exec ||
        target->active_space || target->pending_space || target->kstack_phys || target->parent_identity ||
        procmem_gen(target->user_start)!=target->tpa_gen ||
        procmem_gen(target->env_start)!=target->env_gen ||
        (VADDR)target->pd->p_lowtpa!=(VADDR)(uintptr_t)target->user_start ||
        (VADDR)target->pd->p_env!=(VADDR)(uintptr_t)target->env_start ||
        (VADDR)target->pd->p_hitpa<(VADDR)(uintptr_t)(target->user_start+sizeof(PD)) ||
        (VADDR)target->pd->p_hitpa>(VADDR)(uintptr_t)target->user_start+procmem_size(target->user_start)) return EIMBA;
#if KPROC_LEGACY_PRG
    legacy=target->legacy && target->legacy_text_recorded && !target->has_image &&
        target->legacy_text_bytes &&
        target->legacy_text_bytes<=(VADDR)(target->user_end-target->user_start)-sizeof(PD);
#endif
    /* #1018: GO enters with the classic PRG convention, the basepage as its
     * argument. A staged PRG (mode 3) starts at its loader-recorded text; a
     * basepage the caller made (mode 5/7, as MiNTLib's tfork() does) at its
     * p_tbase, which must already be executable in the shared space. */
    if(!legacy && (target->has_image
#if KPROC_LEGACY_PRG
        || target->legacy_text_recorded
#endif
        )) return EINVFN;
    if(shared_failure_point()) return ENSMEM;
    set=kalloc(sizeof(*set));if(!set) return ENSMEM;memset(set,0,sizeof(*set));
    set->space=kaspace_share(source->active_space);set->parent=kproc_retain_live(source);
    set->legacy=legacy;
    set->preparing=TRUE;
    set->inherited_stack=stack_bytes(source);
    set->inherited_core=source->lim_core;set->inherited_data=source->lim_data;
    set->inherited_priority=source->prio;
    target->shared_exec=set;
    if(!set->space || !set->parent) { error=ENSMEM;goto failed; }
    /* Mapping admission may wait for an enclosing mutation. Do it before
     * borrowing, image staging or opening the no-yield permission journal. */
    error=shared_failure_point()?ENSMEM:
        kproc_prepare_loaded_mapping_owners(target,source,set->space,&set->mapping_members);
    if(error) goto failed;
    if(!kproc_shared_exec_ready(target,NULL) || source->pd!=run ||
        source->active_space!=set->space || target->creator!=source->pd ||
        (VADDR)target->pd->p_lowtpa!=(VADDR)(uintptr_t)target->user_start ||
        (VADDR)target->pd->p_env!=(VADDR)(uintptr_t)target->env_start ||
        (VADDR)target->pd->p_hitpa<(VADDR)(uintptr_t)(target->user_start+sizeof(PD)) ||
        (VADDR)target->pd->p_hitpa>(VADDR)(uintptr_t)target->user_start+procmem_size(target->user_start)) {
        error=EIMBA;goto failed;
    }
    if(!kproc_user_access(source->pd,(VADDR)(uintptr_t)target->pd,sizeof(PD),TRUE)) {error=EACCDN;goto failed;}
    /* What the child keeps of its TPA. A staged PRG gets the free memory, as
     * on TOS, and its startup Mshrink()s the rest: hold only the loaded
     * basepage, text, data and bss, so that shrink can succeed. The lengths
     * past the text are the caller's to state; a short extent only exposes
     * the child's own tail. */
    held=procmem_size(target->user_start);
#if KPROC_LEGACY_PRG
    if(legacy) {
        VADDR image=sizeof(PD)+target->legacy_text_bytes;
        ULONG dlen=target->pd->p_dlen,blen=target->pd->p_blen;
        if(dlen<held && blen<held && image+dlen+blen<held)
            held=(image+dlen+blen+USER_PAGE_SIZE-1)&~(VADDR)(USER_PAGE_SIZE-1);
        if(held>procmem_size(target->user_start)) held=procmem_size(target->user_start);
    } else if(target->legacy) {
        /* A basepage from mode 5/7: the caller put the code in the TPA (or
         * points p_tbase at its own text, as tfork() does) and stated the
         * segments. Hold the basepage and whatever of them lies in the TPA,
         * so the child can still Mshrink() the rest. */
        VADDR base=(VADDR)(uintptr_t)target->user_start,size=held,end=sizeof(PD);
        VADDR seg[3],len[3];
        seg[0]=(VADDR)(uintptr_t)USERPTR_TO_PTR(target->pd->p_tbase);len[0]=target->pd->p_tlen;
        seg[1]=(VADDR)(uintptr_t)USERPTR_TO_PTR(target->pd->p_dbase);len[1]=target->pd->p_dlen;
        seg[2]=(VADDR)(uintptr_t)USERPTR_TO_PTR(target->pd->p_bbase);len[2]=target->pd->p_blen;
        for(i=0;i<3;i++) {
            if(seg[i]<base+sizeof(PD) || seg[i]>=base+size) continue;
            if(len[i]>base+size-seg[i]) end=size;
            else if(seg[i]+len[i]-base>end) end=seg[i]+len[i]-base;
        }
        if(USERPTR_TO_PTR(target->pd->p_tbase)) { /* entry inside the TPA counts */
            VADDR entry=seg[0];
            if(entry>=base+sizeof(PD) && entry<base+size && entry+2-base>end) end=entry+2-base;
        }
        held=(end+USER_PAGE_SIZE-1)&~(VADDR)(USER_PAGE_SIZE-1);
        if(held>size) held=size;
    }
#endif
#define SHARED_TRY(call) do {error=shared_failure_point()?ENSMEM:(call);if(error) goto failed;}while(0)
    /* The borrowed-text transaction authenticates an already-owned R1 ticket.
     * Keep that exact physical allocation held until commit or full rollback. */
    SHARED_TRY(shared_raw_extent(set,source,target->user_start,target->tpa_gen,held));
    inherited=shared_regions(source);
    if(inherited) for(i=0;i<inherited->nraw;i++) {
        if(set->nraw==SHARED_RAW) {error=ENSMEM;goto failed;}
        SHARED_TRY(kproc_allocation_duplicate(&inherited->raw[i],&set->raw[set->nraw]));
        set->nraw++;
    }
    if(legacy) {
#if KPROC_LEGACY_PRG
        VADDR base=(VADDR)(uintptr_t)target->user_start;
        VADDR bytes=held;
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
    }
#if KPROC_LEGACY_PRG
    else if(target->legacy) {
        /* As on TOS, what a mode 5/7 caller placed in the TPA may be run. */
        policy[0].address=(VADDR)(uintptr_t)target->user_start;policy[0].bytes=held;
        policy[0].rights=ASPACE_PROT_USER|ASPACE_PROT_WRITE|ASPACE_PROT_EXEC;count=1;
        SHARED_TRY(kaspace_permission_prepare_procmem(set->space,&set->raw[0].token,
                    policy,count,&set->permissions));
    }
#endif
    SHARED_TRY(shared_raw(set,source,target->env_start,target->env_gen));
    SHARED_TRY(shared_raw(set,source,source->user_start,source->tpa_gen));
    SHARED_TRY(shared_raw(set,source,source->env_start,source->env_gen));
    SHARED_TRY(shared_logical(set,source,(VADDR)(uintptr_t)target->user_start,
                              held<(VADDR)(target->user_end-target->user_start)?held:
                              (VADDR)(target->user_end-target->user_start),ASPACE_PROT_USER));
    SHARED_TRY(shared_logical(set,source,(VADDR)(uintptr_t)target->env_start,
                              (VADDR)(target->env_end-target->env_start),ASPACE_PROT_USER));
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
    inherited=shared_regions(source);
    if(inherited) for(i=0;i<inherited->nlogical;i++) {
        if(set->nlogical==SHARED_REGIONS) {error=ENSMEM;goto failed;}
        SHARED_TRY(kproc_reservation_duplicate(&inherited->logical[i],&set->logical[set->nlogical]));set->nlogical++;
    }
    /* Ordinary heap/mmap lifetime is owned solely by #968 membership and
     * exact attachments, not a second R2 sweep of all visible mappings. */
    /* Started as FreeMiNT's exec_region() does: at p_tbase, read once here,
     * with the basepage at 4(sp) and sp = p_hitpa - 0x28, in the TPA. The
     * child normally moves to its own stack and Mshrink()s that tail away,
     * so nothing holds it. A p_tbase that is not executable is not refused
     * here: the child faults on it, as it would on FreeMiNT. */
    {
        VADDR top=((VADDR)(uintptr_t)USERPTR_TO_PTR(target->pd->p_hitpa)-0x20)&~(VADDR)1;
        VADDR base=(top-SHARED_START_FRAME)&~(VADDR)(CONF_PCLONE_STACK_ALIGNMENT-1);
        if(base<(VADDR)(uintptr_t)target->user_start) {error=EIMBA;goto failed;}
        startup->entry=(VADDR)(uintptr_t)USERPTR_TO_PTR(target->pd->p_tbase);
        startup->argument=(VADDR)(uintptr_t)target->pd;
        startup->stack_base=base;startup->stack_size=(ULONG)(top-base);
        startup->flags=THREAD_STARTUP_ENTRY_UNCHECKED|THREAD_STARTUP_PRG_ENTRY;
    }
    set->preparing=FALSE;
    return E_OK;
failed:
    kproc_shared_exec_abort(target);return error;
#undef SHARED_TRY
}
#endif
static void shared_exec_commit(KPROC *target)
{
    struct shared_exec_regions *set=target->shared_exec;
    KPROC *source;
    if(!set || set->committed || !(source=set->parent) || !source->pd)
        panic("shared exec staging changed before publication");
    target->stack_size=set->inherited_stack;
    target->lim_core=set->inherited_core;target->lim_data=set->inherited_data;
    target->prio=set->inherited_priority;
    target->parent_identity=set->parent;set->parent=NULL;
    set->committed=TRUE;
}
#undef SHARED_RAW
#undef SHARED_REGIONS
#undef SHARED_START_FRAME

#if CONF_WITH_SHARED_VM_PEXEC
void kproc_shared_exec_finish(KPROC *process)
{
    if(!process || !process->shared_exec || !process->shared_exec->committed)
        panic("shared exec permission commit without publication");
    kaspace_permission_finish(&process->shared_exec->permissions,TRUE);
}
#endif
BOOL kproc_shared_exec_committed(const KPROC *process)
{ return process && process->shared_exec && process->shared_exec->committed; }
