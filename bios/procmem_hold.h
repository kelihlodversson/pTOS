/* Shared allocation-record implementation for x32 and 32-bit MMU windows.
 * Included after find_link/release; this is not a second allocation registry. */
struct allocation_hold {
    struct allocation_hold *next;
    UQUAD ticket, owner_number;
    VADDR offset, bytes;
    UWORD rights;
    void *owner_identity;
};
static UQUAD hold_tickets;

static struct allocation_hold **hold_find(const PROCMEM_HOLD *hold,
                                         struct alloc ***allocation)
{
    struct alloc **link;
    struct allocation_hold **at;
    if (!hold || !hold->ticket || !hold->allocation) return NULL;
    link = find_link((const void *)(uintptr_t)hold->base);
    if (!link || (*link)->gen != hold->allocation) return NULL;
    for (at = &(*link)->holds; *at; at = &(*at)->next)
        if ((*at)->ticket == hold->ticket) {
            if (allocation) *allocation = link;
            return at;
        }
    return NULL;
}

BOOL procmem_hold_acquire(const void *base, UQUAD allocation,
        const void *expected_owner, VADDR offset, VADDR bytes, UWORD rights,
        UQUAD owner_number, void *owner_identity, PROCMEM_HOLD *result)
{
    struct alloc **link = find_link(base), *a;
    struct allocation_hold *hold;
    VADDR size;
    if (!result || result->base || result->allocation || result->ticket ||
        !link || !allocation || (*link)->gen != allocation ||
        !expected_owner || (*link)->owner != expected_owner || (*link)->reclaim ||
        !owner_number || !owner_identity || !bytes ||
        !(rights & ASPACE_PROT_USER) ||
        (rights & ~(ASPACE_PROT_USER|ASPACE_PROT_WRITE|ASPACE_PROT_EXEC))) return FALSE;
    a = *link; size = (VADDR)a->pages * HOLD_PAGE;
    if (offset >= size || bytes > size-offset || a->hold_count == (ULONG)~0UL ||
        hold_tickets == ~(UQUAD)0) return FALSE;
    hold = kalloc(sizeof(*hold));
    if (!hold) return FALSE;
    hold->ticket = ++hold_tickets;
    hold->offset = offset; hold->bytes = bytes; hold->rights = rights;
    hold->owner_number = owner_number; hold->owner_identity = owner_identity;
    /* No-fail publication; all allocation/owner acquisition precedes this. */
    hold->next = a->holds; a->holds = hold; a->hold_count++;
    result->base = (VADDR)(uintptr_t)base;
    result->allocation = allocation; result->ticket = hold->ticket;
    return TRUE;
}

BOOL procmem_hold_duplicate(const PROCMEM_HOLD *source, void *owner_identity,
                           PROCMEM_HOLD *result)
{
    struct alloc **allocation;
    struct allocation_hold **link, *copy;
    if (!result || result->base || result->allocation || result->ticket) return FALSE;
    link = hold_find(source,&allocation);
    if (!link || !owner_identity || (*link)->owner_identity != owner_identity ||
        (*allocation)->hold_count == (ULONG)~0UL || hold_tickets == ~(UQUAD)0) return FALSE;
    copy = kalloc(sizeof(*copy));
    if (!copy) return FALSE;
    *copy = **link; copy->ticket = ++hold_tickets;
    copy->next = (*allocation)->holds; (*allocation)->holds = copy;
    (*allocation)->hold_count++;
    *result = *source; result->ticket = copy->ticket;
    return TRUE;
}

BOOL procmem_hold_info(const PROCMEM_HOLD *token, PROCMEM_HOLD_INFO *result)
{
    struct allocation_hold **link = hold_find(token,NULL), *hold;
    if (!link || !result) return FALSE;
    hold = *link;
    result->offset = hold->offset; result->bytes = hold->bytes;
    result->rights = hold->rights; result->owner_number = hold->owner_number;
    result->owner_identity = hold->owner_identity;
    return TRUE;
}

BOOL procmem_mutation_allowed(const void *base, UQUAD allocation,
                             VADDR offset, VADDR bytes)
{
    struct alloc **link = find_link(base);
    struct allocation_hold *hold;
    VADDR size;
    if (!link || !allocation || (*link)->gen != allocation) return FALSE;
    size = (VADDR)(*link)->pages * HOLD_PAGE;
    if (offset > size || bytes > size-offset) return FALSE;
    if (!bytes) return TRUE;
    for (hold = (*link)->holds; hold; hold = hold->next)
        if (offset < hold->offset+hold->bytes && hold->offset < offset+bytes)
            return FALSE;
    return TRUE;
}

BOOL procmem_hold_release(PROCMEM_HOLD *token, void **owner_identity)
{
    struct alloc **allocation;
    struct allocation_hold **link, *hold;
    if (!owner_identity || *owner_identity) return FALSE;
    link = hold_find(token,&allocation);
    if (!link) return FALSE;
    hold = *link;
    /* Invalidate/unpublish before anything can reclaim or notify. */
    memset(token,0,sizeof(*token)); *link = hold->next;
    (*allocation)->hold_count--;
    *owner_identity = hold->owner_identity;
    kfree(hold);
    if ((*allocation)->reclaim && !(*allocation)->pins && !(*allocation)->hold_count)
        release(allocation);
    return TRUE;
}

BOOL procmem_shrink(void *base, ULONG bytes)
{
    struct alloc **link = find_link(base);
    struct alloc *a;
    ULONG keep;
    if (!link || bytes > (ULONG)~0UL - (HOLD_PAGE-1)) return FALSE;
    a = *link; keep = (bytes+HOLD_PAGE-1)/HOLD_PAGE;
    if (!keep) keep = 1;
    if (keep >= a->pages) return TRUE;
    if (!procmem_mutation_allowed(base,a->gen,(VADDR)keep*HOLD_PAGE,
                                 (VADDR)(a->pages-keep)*HOLD_PAGE)) return FALSE;
    set_pages((ULONG)((a->va-HOLD_WINDOW)/HOLD_PAGE)+keep,a->pages-keep,FALSE);
    free_pages += a->pages-keep; a->pages = keep;
    return TRUE;
}
#undef HOLD_PAGE
#undef HOLD_WINDOW
