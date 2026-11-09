/* Selected MMU core supplies cow_map/replace/flush and physical operations. */
#if CONF_WITH_PRIVATE_CLONE
static struct cow_frame *cow_frames;
static ULONG cow_frame_count, cow_reference_count;
static ULONG clone_fail_after;
#if CONF_WITH_PRIVATE_CLONE_TEST
static BOOL cow_fail_allocation;
void aspace_cow_test_fail_allocation(BOOL fail) { cow_fail_allocation = fail; }
#endif
void aspace_clone_test_fail_after(ULONG count) { clone_fail_after = count; }
static BOOL clone_injected(void)
{
    return clone_fail_after && !--clone_fail_after;
}
static void *clone_allocate(ULONG bytes)
{
    return clone_injected() ? NULL : kalloc(bytes);
}
ULONG aspace_cow_frames(void) { return cow_frame_count; }
ULONG aspace_cow_references(void) { return cow_reference_count; }
static struct cow_frame *cow_frame_find(PADDR physical)
{
    struct cow_frame *frame;
    for (frame = cow_frames; frame; frame = frame->next)
        if (frame->physical == physical) return frame;
    return NULL;
}
static struct cow_mapping *cow_find(const ASPACE *as, VADDR address)
{
    struct cow_mapping *mapping;
    address &= ~(VADDR)(USER_PAGE_SIZE - 1);
    for (mapping = as->cow; mapping; mapping = mapping->next)
        if (mapping->address == address) return mapping;
    return NULL;
}
static void cow_unlink_mapping(ASPACE *as, VADDR address)
{
    struct cow_mapping **link = &as->cow, *mapping;
    address &= ~(VADDR)(USER_PAGE_SIZE - 1);
    while ((mapping = *link)) {
        if (mapping->address == address) {
            *link = mapping->next; kfree(mapping); return;
        }
        link = &mapping->next;
    }
}
/* Drop metadata only. Ownership of the final physical page can be transferred
 * back to an ordinary unique leaf without freeing/reallocating that page. */
static void cow_frame_drop(struct cow_frame *frame, BOOL release)
{
    struct cow_frame **link;
    if (!frame || !frame->references) panic("COW reference underflow");
    cow_reference_count--;
    if (--frame->references) return;
    for (link = &cow_frames; *link != frame; link = &(*link)->next) { }
    *link = frame->next; cow_frame_count--;
    if (release) COW_FREE(frame->physical);
    kfree(frame);
}
static BOOL cow_release_backing(PADDR physical)
{
    struct cow_frame *frame = cow_frame_find(physical);
    if (!frame) return FALSE;
    cow_frame_drop(frame, TRUE);
    return TRUE;
}
BOOL aspace_logical_protection(const ASPACE *as, VADDR address, UWORD *protection)
{
    struct cow_mapping *mapping = cow_find(as, address);
    if (mapping) { *protection = mapping->protection; return TRUE; }
    return aspace_translate(as, address, NULL, protection);
}
/* Failed higher-level construction may discard the child after backend clone
 * succeeded. Normalize exclusive source leaves without allocation or copies. */
void aspace_cow_collapse(ASPACE *as)
{
    struct cow_mapping **link = &as->cow, *mapping;
    BOOL changed = FALSE;
    while ((mapping = *link)) {
        if (mapping->frame->references == 1 &&
            kaspace_cow_change(as, mapping->address, USER_PAGE_SIZE)) {
            struct cow_frame *frame = mapping->frame;
            if (!cow_replace(as, mapping->address, frame->physical, mapping->protection))
                panic("COW exclusive collapse");
            *link = mapping->next; kfree(mapping); cow_frame_drop(frame, FALSE);
            changed = TRUE;
        } else link = &mapping->next;
    }
    if (changed) cow_flush(as);
}
/* -1: not an anonymous COW leaf; 0: forbidden access; 1: retry; 2: OOM.
 * A COW change preserves the VA/rights/hold, but never a sealed write alias. */
int aspace_cow_fault(ASPACE *as, VADDR address, UWORD access)
{
    struct cow_mapping *mapping = cow_find(as, address);
    struct cow_frame *frame;
    PADDR physical;
    UWORD protection;
    if (!mapping) return -1;
    if (!(access & ASPACE_PROT_WRITE) || !(mapping->protection & ASPACE_PROT_USER) ||
        (access & ~mapping->protection)) return 0;
    address &= ~(VADDR)(USER_PAGE_SIZE - 1);
    if (!kaspace_cow_change(as, address, USER_PAGE_SIZE)) return 2;
    frame = mapping->frame; protection = mapping->protection;
    physical = frame->physical;
    if (frame->references > 1) {
#if CONF_WITH_PRIVATE_CLONE_TEST
        if (cow_fail_allocation) { cow_fail_allocation = FALSE; return 2; }
#endif
        physical = COW_ALLOC();
        if (physical == COW_NONE) return 2;
        memcpy(COW_ALIAS(physical), COW_ALIAS(frame->physical), USER_PAGE_SIZE);
    }
    /* Existing leaf/tables only; replacement allocates no translation storage. */
    if (!cow_replace(as, address, physical, protection)) {
        if (physical != frame->physical) COW_FREE(physical);
        return 2;
    }
    cow_unlink_mapping(as, address);
    cow_frame_drop(frame, physical != frame->physical);
    if (protection & ASPACE_PROT_EXEC) COW_CODE_CHANGED();
    cow_flush(as);
    return 1;
}
static void cow_finish_source(ASPACE *source, BOOL commit)
{
    struct cow_mapping **link = &source->cow, *mapping;
    while ((mapping = *link)) {
        if (!commit && mapping->prepared) {
            struct cow_frame *frame = mapping->frame;
            if (frame->references != 1) panic("COW rollback retained child");
            *link = mapping->next; kfree(mapping);
            cow_frame_drop(frame, FALSE); /* original leaf still owns page */
            continue;
        }
        if (commit) {
            if (!cow_replace(source, mapping->address, mapping->frame->physical,
                    mapping->protection & ~ASPACE_PROT_WRITE)) panic("COW source commit");
            mapping->prepared = FALSE;
        }
        link = &mapping->next;
    }
    if (commit) {
        /* Borrowed executable USER RAM was copied through physical aliases.
         * Synchronize code once, before any child root can be installed. */
        COW_CODE_CHANGED();
        cow_flush(source);
    }
}
/* One leaf of an unpublished child. No source permission changes until every
 * child table, mapping and reference is ready. Failure destroys child first. */
static LONG cow_clone_leaf(ASPACE *source, ASPACE *child, VADDR address,
                           PADDR physical, UWORD protection, BOOL owned)
{
    struct cow_mapping *original, *copy;
    struct cow_frame *frame;
    if (!owned) {
        PADDR fresh;
        if (!(protection & ASPACE_PROT_USER)) return E_OK; /* kernel borrow */
        if (!COW_RAM(physical)) return EINVFN; /* device/external memory */
        fresh = clone_injected() ? COW_NONE : COW_ALLOC();
        if (fresh == COW_NONE) return ENSMEM;
        memcpy(COW_ALIAS(fresh), COW_ALIAS(physical), USER_PAGE_SIZE);
        if (!cow_map(child, address, fresh, protection, TRUE)) {
            COW_FREE(fresh); return ENSMEM;
        }
        return E_OK;
    }
    /* Existing clean MAP_PRIVATE file pages keep their own backing references.
     * Their original logical permissions remain in the cloned mmap metadata. */
    {
        LONG file = cow_file_leaf(child, address, physical, protection);
        if (file != 1) return file; /* 1: not a file leaf */
    }
    original = cow_find(source, address);
    if (!original) {
        if (cow_frame_find(physical)) return EINVFN; /* untracked owned alias */
        if (cow_frame_count == ~(ULONG)0 || cow_reference_count == ~(ULONG)0) return EAGAIN;
        frame = clone_allocate(sizeof(*frame));
        if (!frame) return ENSMEM;
        original = clone_allocate(sizeof(*original));
        if (!original) { kfree(frame); return ENSMEM; }
        frame->physical = physical; frame->references = 1;
        frame->next = cow_frames; cow_frames = frame;
        cow_frame_count++; cow_reference_count++;
        original->address = address; original->protection = protection;
        original->frame = frame; original->prepared = TRUE;
        original->next = source->cow; source->cow = original;
    }
    frame = original->frame;
    if (frame->references == ~(ULONG)0 || cow_reference_count == ~(ULONG)0) return EAGAIN;
    copy = clone_allocate(sizeof(*copy));
    if (!copy) return ENSMEM;
    *copy = *original; copy->prepared = FALSE;
    if (!cow_map(child, address, physical, original->protection & ~ASPACE_PROT_WRITE, TRUE)) {
        kfree(copy); return ENSMEM;
    }
    frame->references++; cow_reference_count++;
    copy->next = child->cow; child->cow = copy;
    return E_OK;
}
#endif
