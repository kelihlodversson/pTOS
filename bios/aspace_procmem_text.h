/* Loader-only borrowed text policy; included by the MMU core and its
 * production-code host fixture. No extra allocation pin or CPU transition. */
BOOL aspace_mark_procmem_text(ASPACE *as, VADDR va, VADDR bytes)
{
    VADDR first = va & ~(VADDR)(PAGE - 1), page, end;
    PADDR phys;
    UWORD prot, flags;
    if (!as || !bytes || va >= USER_VA_LIMIT || bytes > USER_VA_LIMIT - va ||
        !procmem_range_live(va, bytes)) return FALSE;
    end = (va + bytes + PAGE - 1) & ~(VADDR)(PAGE - 1);
    if (!kaspace_mapping_change(as, first, end - first)) return FALSE;
    for (page = first; page < end; page += PAGE) {
        if (!aspace_hw_lookup(as->root, page, &phys, &prot, &flags) ||
            flags & ASPACE_LEAF_OWNED || phys != procmem_phys_of(page) ||
            (prot & (ASPACE_PROT_USER | ASPACE_PROT_WRITE)) !=
                (ASPACE_PROT_USER | ASPACE_PROT_WRITE)) return FALSE;
    }
    /* Same leaves in an unpublished root: no allocation/yield/callback. */
    for (page = first; page < end; page += PAGE) {
        if (!aspace_hw_lookup(as->root, page, &phys, &prot, &flags) ||
            !aspace_hw_update(as->root, page, prot | ASPACE_PROT_EXEC, flags))
            panic("PRG text publication lost mapping");
    }
    aspace_hw_code_changed();
    flush_if_current(as);
    return TRUE;
}

/* These helpers share the existing borrowed mapping and R1 authorities. */
BOOL aspace_procmem_permission_info(const ASPACE *as, const void *base, UQUAD generation,
                                   VADDR page, PADDR *physical, UWORD *rights)
{
    VADDR start=(VADDR)(uintptr_t)base, size=procmem_size(base);
    PADDR phys;UWORD prot,flags;
    if(!as || !generation || procmem_gen(base)!=generation ||
        (page&(PAGE-1)) || page<start || page-start>=size || PAGE>size-(page-start) ||
        !aspace_hw_lookup(as->root,page,&phys,&prot,&flags) ||
        (flags&ASPACE_LEAF_OWNED) || !(prot&ASPACE_PROT_USER) ||
        phys!=procmem_phys_of(page)) return FALSE;
    if(physical) *physical=phys;
    if(rights) *rights=prot;
    return TRUE;
}
BOOL aspace_protect_procmem(ASPACE *as, const void *base, UQUAD generation,
                           VADDR address, VADDR bytes, UWORD rights)
{
    VADDR page,end;PADDR phys;UWORD before,flags;
    if(!bytes || (address&(PAGE-1)) || (bytes&(PAGE-1)) ||
        address>=USER_VA_LIMIT || bytes>USER_VA_LIMIT-address ||
        !(rights&ASPACE_PROT_USER) ||
        (rights&~(ASPACE_PROT_USER|ASPACE_PROT_WRITE|ASPACE_PROT_EXEC))) return FALSE;
    end=address+bytes;
    for(page=address;page<end;page+=PAGE)
        if(!aspace_procmem_permission_info(as,base,generation,page,NULL,NULL)) return FALSE;
    if(!kaspace_mapping_change(as,address,bytes)) return FALSE;
    for(page=address;page<end;page+=PAGE) {
        if(!aspace_hw_lookup(as->root,page,&phys,&before,&flags) ||
            !aspace_hw_update(as->root,page,rights,flags))
            panic("preflighted procmem permission changed");
    }
    aspace_hw_code_changed();flush_if_current(as);return TRUE;
}
