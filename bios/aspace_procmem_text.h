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
