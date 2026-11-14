/* Short loader permission transaction. No yield/callback or public execution
 * is permitted until commit/abort. Existing root/lease lists remain canonical. */
struct kaspace_permission_page { VADDR address; PADDR physical; UWORD before,after; };
struct kaspace_permission_lease { UQUAD identity; BOOL revoked; };
struct kaspace_permission_transaction {
    KASPACE *space;
    PROCMEM_HOLD procmem; /* nonowning live ticket; caller retains R1 */
    ULONG pages,leases;
    struct kaspace_permission_page *page;
    struct kaspace_permission_lease *lease;
};
static void permission_free(KASPACE_PERMISSION_TRANSACTION *tx)
{
    kaspace_put(tx->space);kfree(tx->page);kfree(tx->lease);kfree(tx);
}
static LONG permission_prepare(KASPACE *space,const PROCMEM_HOLD *hold,
        const KASPACE_PROTECTION *ranges,UWORD count,KASPACE_PERMISSION_TRANSACTION **out)
{
    KASPACE_PERMISSION_TRANSACTION *tx;
    KASPACE_LEASE *lease;
    PROCMEM_HOLD_INFO held;
    ULONG n=0,i,j;
    UWORD r;
    VADDR page,last;
    if (!space || !ranges || !count || !out || *out) return EINVAL;
    if(hold && (!procmem_hold_info(hold,&held) || !(held.rights&ASPACE_PROT_USER))) return EACCDN;
    for(r=0;r<count;r++) {
        if (!ranges[r].bytes || ranges[r].address>=USER_VA_LIMIT ||
            ranges[r].bytes>USER_VA_LIMIT-ranges[r].address ||
            !(ranges[r].rights&ASPACE_PROT_USER) ||
            (ranges[r].rights&~(ASPACE_PROT_USER|ASPACE_PROT_WRITE|ASPACE_PROT_EXEC))) return EINVAL;
        i=(ULONG)(((ranges[r].address&(USER_PAGE_SIZE-1))+ranges[r].bytes+USER_PAGE_SIZE-1)/USER_PAGE_SIZE);
        if(i>(ULONG)~0UL/sizeof(*tx->page)-n) return ENSMEM;
        n+=i;
        if(hold) {
            VADDR first=ranges[r].address&~(VADDR)(USER_PAGE_SIZE-1);
            VADDR length=(VADDR)i*USER_PAGE_SIZE;
            if(first<hold->base || first-hold->base<held.offset ||
                first-hold->base-held.offset>held.bytes ||
                length>held.bytes-(first-hold->base-held.offset)) return EACCDN;
        }
    }
    tx=kalloc(sizeof(*tx));if(!tx) return ENSMEM;
    memset(tx,0,sizeof(*tx));tx->space=kaspace_share(space);
    if(hold) tx->procmem=*hold;
    tx->page=kalloc(n*sizeof(*tx->page));
    if(!tx->space || !tx->page) { permission_free(tx);return ENSMEM; }
    for(r=0;r<count;r++) {
        last=(ranges[r].address+ranges[r].bytes-1)&~(VADDR)(USER_PAGE_SIZE-1);
        for(page=ranges[r].address&~(VADDR)(USER_PAGE_SIZE-1);;page+=USER_PAGE_SIZE) {
            PADDR physical;UWORD rights;
            if(hold) {
#if defined(__m68k__) && CONF_WITH_PRG_LOADER
                if(!aspace_procmem_permission_info(space->backend,(const void *)(uintptr_t)hold->base,
                        hold->allocation,page,&physical,&rights)) goto denied;
#else
                goto denied;
#endif
            } else if(!aspace_translate(space->backend,page,&physical,&rights) ||
                !aspace_private_range_ok(space->backend,page,USER_PAGE_SIZE)) goto denied;
#if CONF_WITH_PRIVATE_CLONE
            if(!aspace_logical_protection(space->backend,page,&rights)) goto denied;
#endif
            for(i=0;i<tx->pages;i++) if(tx->page[i].address==page) break;
            if(i<tx->pages) { if(tx->page[i].after!=ranges[r].rights) goto denied; }
            else if(rights!=ranges[r].rights) {
                if(!kaspace_mapping_can_change(space->backend,page,USER_PAGE_SIZE)) goto denied;
                tx->page[i].address=page;tx->page[i].physical=physical;
                tx->page[i].before=rights;tx->page[i].after=ranges[r].rights;tx->pages++;
            }
            if(page==last) break;
        }
    }
    for(lease=space->leases;lease;lease=lease->next) {
        if(lease->attachment_ticket) continue; /* lifetime claims are not permission snapshots */
        for(i=0;i<tx->pages;i++) if(lease->first<=tx->page[i].address && tx->page[i].address<=lease->last) break;
        if(i<tx->pages) tx->leases++;
    }
    if(tx->leases) {
        if(tx->leases>(ULONG)~0UL/sizeof(*tx->lease)) goto exhausted;
        tx->lease=kalloc(tx->leases*sizeof(*tx->lease));if(!tx->lease) goto exhausted;
    }
    j=0;
    for(lease=space->leases;lease;lease=lease->next) {
        if(lease->attachment_ticket) continue; /* lifetime claims are not permission snapshots */
        for(i=0;i<tx->pages;i++) if(lease->first<=tx->page[i].address && tx->page[i].address<=lease->last) break;
        if(i<tx->pages) { tx->lease[j].identity=lease->identity;tx->lease[j++].revoked=lease->revoked; }
    }
    /* All storage, coverage/ownership and foreign-holder checks precede stores.
     * Providers only update existing leaves; no allocation or callback follows. */
    for(i=0;i<tx->pages;i++) {
        BOOL changed;
#if defined(__m68k__) && CONF_WITH_PRG_LOADER
        if(hold) changed=aspace_protect_procmem(space->backend,(const void *)(uintptr_t)hold->base,
                hold->allocation,tx->page[i].address,USER_PAGE_SIZE,tx->page[i].after);
        else
#endif
        changed=aspace_protect_private(space->backend,tx->page[i].address,USER_PAGE_SIZE,tx->page[i].after);
        if(!changed) panic("preflighted loader protection changed");
    }
    *out=tx;return E_OK;
exhausted:
    permission_free(tx);return ENSMEM;
denied:
    permission_free(tx);return EACCDN;
}
LONG kaspace_permission_prepare(KASPACE *space,const KASPACE_PROTECTION *ranges,
        UWORD count,KASPACE_PERMISSION_TRANSACTION **out)
{ return permission_prepare(space,NULL,ranges,count,out); }
LONG kaspace_permission_prepare_procmem(KASPACE *space,const struct procmem_hold *hold,
        const KASPACE_PROTECTION *ranges,UWORD count,KASPACE_PERMISSION_TRANSACTION **out)
{
#if defined(__m68k__) && CONF_WITH_PRG_LOADER
    if(!hold) return EINVAL;
    return permission_prepare(space,hold,ranges,count,out);
#else
    UNUSED(space);UNUSED(hold);UNUSED(ranges);UNUSED(count);UNUSED(out);
    return EINVFN;
#endif
}
void kaspace_permission_finish(KASPACE_PERMISSION_TRANSACTION **handle,BOOL commit)
{
    KASPACE_PERMISSION_TRANSACTION *tx;ULONG i;
    if(!handle || !*handle) return;
    tx=*handle;*handle=NULL;
    if(tx->procmem.ticket) {
        PROCMEM_HOLD_INFO held;
        if(!procmem_hold_info(&tx->procmem,&held)) panic("loader lost procmem R1 hold");
    }
    if(!commit) {
        for(i=0;i<tx->pages;i++) {
            PADDR physical;
#if defined(__m68k__) && CONF_WITH_PRG_LOADER
            if(tx->procmem.ticket &&
                !aspace_procmem_permission_info(tx->space->backend,(const void *)(uintptr_t)tx->procmem.base,
                    tx->procmem.allocation,tx->page[i].address,NULL,NULL))
                panic("loader rollback lost borrowed allocation");
#endif
            if(!aspace_translate(tx->space->backend,tx->page[i].address,&physical,NULL) ||
                physical!=tx->page[i].physical ||
                !kaspace_mapping_can_change(tx->space->backend,tx->page[i].address,USER_PAGE_SIZE))
                panic("loader rollback retained a conflicting mapping owner");
        }
        for(i=0;i<tx->pages;i++) {
            BOOL changed;
#if defined(__m68k__) && CONF_WITH_PRG_LOADER
            if(tx->procmem.ticket) changed=aspace_protect_procmem(tx->space->backend,
                (const void *)(uintptr_t)tx->procmem.base,tx->procmem.allocation,
                tx->page[i].address,USER_PAGE_SIZE,tx->page[i].before);
            else
#endif
            changed=aspace_protect_private(tx->space->backend,tx->page[i].address,USER_PAGE_SIZE,tx->page[i].before);
            if(!changed) panic("loader permission rollback failed");
        }
        for(i=0;i<tx->leases;i++) {
            KASPACE_LEASE *lease;
            for(lease=tx->space->leases;lease;lease=lease->next)
                if(lease->identity==tx->lease[i].identity) {lease->revoked=tx->lease[i].revoked;break;}
        }
    }
    permission_free(tx);
}
