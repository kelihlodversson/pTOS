/* Live-root borrow transaction, shared by the two address-space cores.
 * The temporary journal is NOT an ownership/mapping registry. It preserves
 * raw descriptors (including permissions/software flags and m68k carve state)
 * and the original table vector until the entire borrow succeeds. Current
 * providers allocate without yielding, callbacks or user copy; preemption/SMP
 * require the documented owner/mapping locks, not local IRQ masking. */
BOOL aspace_map_procmem(ASPACE *as,VADDR va,VADDR bytes,UWORD prot)
{
    VADDR page,first,end;
    PADDR *old_pages,*staged_pages=NULL;
    UBYTE *saved=NULL;
    struct pin *staged_pins=NULL;
    ULONG old_count,old_capacity,root_pages,total,i,new_pin_capacity=as->pin_capacity;
    BOOL pinned=FALSE;

    if (!bytes || va>=USER_VA_LIMIT || bytes>USER_VA_LIMIT-va ||
        !procmem_range_live(va,bytes) || !kaspace_mapping_can_change(as,va,bytes)) return FALSE;
    first=va&~(VADDR)(USER_PAGE_SIZE-1);
    end=(va+bytes+USER_PAGE_SIZE-1)&~(VADDR)(USER_PAGE_SIZE-1);
#if CONF_WITH_PRIVATE_CLONE
    for (page=first;page<end;page+=USER_PAGE_SIZE)
        if (cow_find(as,page) || BORROW_OWNED(as,page)) return FALSE;
#endif
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST
    if (as->test_borrow_failure==-2) { as->test_borrow_failure=-1;return FALSE; }
#endif
    old_pages=as->pages;old_count=as->count;old_capacity=as->capacity;
    root_pages=BORROW_ROOT_PAGES(as);
    if (root_pages>(ULONG)~0UL/USER_PAGE_SIZE ||
        old_count>(ULONG)~0UL/USER_PAGE_SIZE-root_pages ||
        old_capacity>(ULONG)~0UL/sizeof(*old_pages)) return FALSE;
    total=(old_count+root_pages)*USER_PAGE_SIZE;
    if (as->npins==as->pin_capacity) {
        if (as->pin_capacity>(ULONG)~0UL/(2*sizeof(*as->pins))) return FALSE;
        new_pin_capacity=as->pin_capacity?as->pin_capacity*2:4;
        staged_pins=kalloc(new_pin_capacity*sizeof(*staged_pins));
        if (!staged_pins) return FALSE;
        if (as->npins) memcpy(staged_pins,as->pins,as->npins*sizeof(*staged_pins));
    }
    saved=kalloc(total);
    if (!saved) goto prepare_failed;
    if (old_capacity) {
        staged_pages=kalloc(old_capacity*sizeof(*old_pages));
        if (!staged_pages) goto prepare_failed;
        if (old_count) memcpy(staged_pages,old_pages,old_count*sizeof(*old_pages));
    }
    if (root_pages) memcpy(saved,BORROW_ALIAS(BORROW_ROOT(as)),root_pages*USER_PAGE_SIZE);
    for (i=0;i<old_count;i++)
        memcpy(saved+(i+root_pages)*USER_PAGE_SIZE,BORROW_ALIAS(old_pages[i]),USER_PAGE_SIZE);
    /* record_page may grow/free ONLY this staged vector. The original vector
     * and every original table page remain owned until commit or rollback. */
    as->pages=staged_pages;
    if (!procmem_try_pin(va,bytes)) goto rollback;
    pinned=TRUE;
    for (page=first;page<end;page+=USER_PAGE_SIZE) {
        PADDR physical=procmem_phys_of(page);
#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST
        if (!as->test_borrow_failure) { as->test_borrow_failure=-1;goto rollback; }
        if (as->test_borrow_failure>0) as->test_borrow_failure--;
#endif
        if ((physical&(USER_PAGE_SIZE-1)) || !BORROW_MAP(as,page,physical,prot)) goto rollback;
    }
    /* No-fail publication: whole-range admission was pure, no provider above
     * can yield or mutate leases, and all fallible work is finished. */
    if (!kaspace_mapping_change(as,va,bytes)) panic("borrow ownership changed during mapping");
    if (staged_pins) {
        kfree(as->pins);as->pins=staged_pins;as->pin_capacity=new_pin_capacity;
    }
    as->pins[as->npins].va=va;as->pins[as->npins].bytes=bytes;
    as->pins[as->npins].retired=FALSE;as->npins++;
    BORROW_FLUSH(as);
    /* Keep the original vector when its existing capacity suffices. This
     * avoids turning an unchanged successful borrow into heap fragmentation. */
    if (as->count<=old_capacity) {
        if (as->count) memcpy(old_pages,as->pages,as->count*sizeof(*old_pages));
        kfree(as->pages);as->pages=old_pages;as->capacity=old_capacity;
    } else kfree(old_pages);
    kfree(saved);
    return TRUE;
rollback:
    if (root_pages) memcpy(BORROW_ALIAS(BORROW_ROOT(as)),saved,root_pages*USER_PAGE_SIZE);
    for (i=0;i<old_count;i++)
        memcpy(BORROW_ALIAS(old_pages[i]),saved+(i+root_pages)*USER_PAGE_SIZE,USER_PAGE_SIZE);
    /* Clean restored descriptor/cache state and invalidate active translations
     * BEFORE releasing any newly detached page-table memory. */
    BORROW_RESTORED(as);
    while (as->count>old_count) BORROW_FREE(as->pages[--as->count]);
    kfree(as->pages);as->pages=old_pages;as->capacity=old_capacity;
    if (pinned) procmem_pin(va,bytes,-1); /* exactly this acquisition, never a lookup/unborrow */
prepare_failed:
    kfree(staged_pins);kfree(saved);
    return FALSE;
}
#undef BORROW_ROOT_PAGES
#undef BORROW_ROOT
#undef BORROW_ALIAS
#undef BORROW_FREE
#undef BORROW_MAP
#undef BORROW_OWNED
#undef BORROW_FLUSH
#undef BORROW_RESTORED

#if CONF_WITH_X86_64_MEMTEST || CONF_WITH_EXIT_HANDOFF_TEST
/* Existing native test gates only: a per-root, one-shot mapping failure.
 * -2 is preparation before pinning, -1 disables, n>=0 fails after n pages. */
void aspace_test_borrow_failure(ASPACE *as,LONG after)
{ as->test_borrow_failure=after; }
LONG aspace_test_borrow_pending(const ASPACE *as)
{ return as->test_borrow_failure; }
UQUAD aspace_test_table_hash(const ASPACE *as)
{
    UQUAD hash=1469598103934665603ULL;
    ULONG i,j;
    for(i=0;i<BORROW_HASH_COUNT(as);i++) {
        const UBYTE *page=BORROW_HASH_PAGE(as,i);
        for(j=0;j<USER_PAGE_SIZE;j++) hash=(hash^page[j])*1099511628211ULL;
    }
    return hash;
}
#endif
#undef BORROW_HASH_COUNT
#undef BORROW_HASH_PAGE

/* Acquire an independent reference to an EXISTING exact procmem translation.
 * This never remaps a leaf, grants USER rights, revokes a lease or consumes
 * another borrow. It is valid beneath a retained logical reservation fence. */
BOOL aspace_duplicate_borrow(ASPACE *as,VADDR va,VADDR bytes)
{
    VADDR page,last;PADDR physical;
    struct pin *grown=NULL;ULONG capacity=as->pin_capacity;
    if(!bytes || va>=USER_VA_LIMIT || bytes>USER_VA_LIMIT-va || !procmem_range_live(va,bytes)) return FALSE;
    last=(va+bytes-1)&~(VADDR)(USER_PAGE_SIZE-1);
    for(page=va&~(VADDR)(USER_PAGE_SIZE-1);;page+=USER_PAGE_SIZE) {
        if(!aspace_translate(as,page,&physical,NULL) || physical!=procmem_phys_of(page)) return FALSE;
        if(page==last) break;
    }
    if(as->npins==capacity) {
        if(capacity>(ULONG)~0UL/(2*sizeof(*grown))) return FALSE;
        capacity=capacity?capacity*2:4;
        grown=kalloc(capacity*sizeof(*grown));if(!grown) return FALSE;
        if(as->npins) memcpy(grown,as->pins,as->npins*sizeof(*grown));
    }
    if(!procmem_try_pin(va,bytes)) { kfree(grown);return FALSE; }
    if(grown) { kfree(as->pins);as->pins=grown;as->pin_capacity=capacity; }
    as->pins[as->npins].va=va;as->pins[as->npins].bytes=bytes;
    as->pins[as->npins++].retired=FALSE;
    return TRUE;
}
