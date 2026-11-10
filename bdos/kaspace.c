/* Address-space ownership; mapping and cloning remain backend responsibilities. */
#include "emutos.h"
#include "gemerror.h"
#include "kheap.h"
#include "procmem.h"
#include "string.h"
#include "kaspace.h"

struct kaspace {
    ASPACE *backend;
    ULONG refs;
    KASPACE_HEAP heap;
    KASPACE_LEASE *leases;
    struct kaspace *next;
#if CONF_WITH_MMAP
    KASPACE_MMAP mmap;
#endif
};

static KASPACE *spaces; /* borrowed registry; serialized with retain/release */
struct kaspace_lease {
    KASPACE *space; /* owning; list membership is non-owning */
    VADDR first, last; /* inclusive page addresses; no end-address overflow */
    UWORD access;
    BOOL revoked, sealed, held, stack;
    KASPACE_LEASE *next;
};

#if CONF_WITH_X86_64_MEMTEST
static UWORD fail_lease_after;
#endif

BOOL kaspace_lease_valid(const KASPACE_LEASE *lease)
{
    VADDR page;
    if (!lease || lease->revoked) return FALSE;
    for (page = lease->first; ; page += USER_PAGE_SIZE) {
        UWORD prot;
        if (!
#if CONF_WITH_PRIVATE_CLONE
            aspace_logical_protection(lease->space->backend, page, &prot)
#else
            aspace_translate(lease->space->backend, page, NULL, &prot)
#endif
            ||
            (prot & lease->access) != lease->access) return FALSE;
        if (page == lease->last) return TRUE;
    }
}

LONG kaspace_lease_acquire(KASPACE *space, VADDR address, VADDR bytes,
                          UWORD access, KASPACE_LEASE **result)
{
    KASPACE_LEASE *lease;
    if (!result || *result || !space || !bytes || address >= USER_VA_LIMIT ||
        bytes > USER_VA_LIMIT - address || !(access & ASPACE_PROT_USER) ||
        (access & ~(ASPACE_PROT_USER | ASPACE_PROT_WRITE | ASPACE_PROT_EXEC))) return EINVAL;
#if CONF_WITH_X86_64_MEMTEST
    if (fail_lease_after && !--fail_lease_after) return ENSMEM;
#endif
    lease = kalloc(sizeof(*lease));
    if (!lease) return ENSMEM;
    lease->space = space;
    lease->first = address & ~(VADDR)(USER_PAGE_SIZE - 1);
    lease->last = (address + bytes - 1) & ~(VADDR)(USER_PAGE_SIZE - 1);
    lease->access = access;
    lease->revoked = lease->sealed = lease->held = lease->stack = FALSE;
    if (!kaspace_lease_valid(lease)) { kfree(lease); return EIMBA; }
    if (!kaspace_share(space)) { kfree(lease); return ENSMEM; }
    lease->next = space->leases;
    space->leases = lease;
    *result = lease;
    return E_OK;
}

BOOL kaspace_lease_hold_private(KASPACE_LEASE *lease)
{
    if (!kaspace_lease_valid(lease) || lease->held ||
        !aspace_private_range_ok(lease->space->backend, lease->first,
                lease->last - lease->first + USER_PAGE_SIZE)) return FALSE;
    lease->held = TRUE;
    return TRUE;
}

LONG kaspace_stack_hold_acquire(KASPACE *space, VADDR address, VADDR bytes, KASPACE_LEASE **result)
{
    KASPACE_LEASE *lease = NULL;
    VADDR page, last;
    LONG error;
    if (!result || *result || !space || !bytes || address >= USER_VA_LIMIT ||
        bytes > USER_VA_LIMIT - address) return EINVAL;
    last = (address + bytes - 1) & ~(VADDR)(USER_PAGE_SIZE - 1);
    for (page = address & ~(VADDR)(USER_PAGE_SIZE - 1); ; page += USER_PAGE_SIZE) {
        BOOL reserved = kaspace_heap_contains(space, page);
#if CONF_WITH_MMAP
        MMAP_REGION *region;
        for (region = space->mmap.regions; !reserved && region; region = region->next)
            if (!region->backing && page >= region->address &&
                page < (UQUAD)region->address + region->length &&
                (region->prot & (ASPACE_PROT_USER | ASPACE_PROT_WRITE)) ==
                    (ASPACE_PROT_USER | ASPACE_PROT_WRITE)) reserved = TRUE;
#endif
        if (!reserved) return EACCDN;
        if (page == last) break;
    }
    error = kaspace_lease_acquire(space, address, bytes, ASPACE_PROT_USER | ASPACE_PROT_WRITE, &lease);
    if (error) return error;
    if (!kaspace_lease_hold_private(lease)) { kaspace_lease_release(&lease); return EACCDN; }
    lease->stack = TRUE;
    *result = lease;
    return E_OK;
}

KASPACE *kaspace_stack_hold_space(const KASPACE_LEASE *hold)
{
    return hold && hold->stack && hold->held && kaspace_lease_valid(hold) ? hold->space : NULL;
}

BOOL kaspace_lease_seal(KASPACE_LEASE *const *leases, UWORD count)
{
    UWORD i;
    if (!leases || !count) return FALSE;
    for (i = 0; i < count; i++) if (!kaspace_lease_valid(leases[i])) return FALSE;
    for (i = 0; i < count; i++) leases[i]->sealed = TRUE;
    return TRUE;
}

void kaspace_lease_release(KASPACE_LEASE **handle)
{
    KASPACE_LEASE *lease, **link;
    KASPACE *space;
    if (!handle || !*handle) return;
    lease = *handle; *handle = NULL; space = lease->space;
    for (link = &space->leases; *link != lease; link = &(*link)->next) { }
    *link = lease->next;
    kfree(lease);
    kaspace_put(space);
}

BOOL kaspace_mapping_change(ASPACE *backend, VADDR address, VADDR bytes)
{
    KASPACE *space;
    KASPACE_LEASE *lease;
    VADDR first, last;
    if (!bytes || address >= USER_VA_LIMIT || bytes > USER_VA_LIMIT - address) return FALSE;
    first = address & ~(VADDR)(USER_PAGE_SIZE - 1);
    last = (address + bytes - 1) & ~(VADDR)(USER_PAGE_SIZE - 1);
    for (space = spaces; space && space->backend != backend; space = space->next) { }
    if (!space) return TRUE; /* backend not yet adopted */
    for (lease = space->leases; lease; lease = lease->next)
        if ((lease->sealed || lease->held) && first <= lease->last && lease->first <= last) return FALSE;
    for (lease = space->leases; lease; lease = lease->next)
        if (first <= lease->last && lease->first <= last) lease->revoked = TRUE;
    return TRUE;
}
#if CONF_WITH_PRIVATE_CLONE
BOOL kaspace_cow_change(ASPACE *backend, VADDR address, VADDR bytes)
{
    KASPACE *space;
    KASPACE_LEASE *lease;
    VADDR first, last;
    if (!bytes || address >= USER_VA_LIMIT || bytes > USER_VA_LIMIT - address) return FALSE;
    first = address & ~(VADDR)(USER_PAGE_SIZE - 1);
    last = (address + bytes - 1) & ~(VADDR)(USER_PAGE_SIZE - 1);
    for (space = spaces; space && space->backend != backend; space = space->next) { }
    if (!space) return TRUE;
    for (lease = space->leases; lease; lease = lease->next)
        if (first <= lease->last && lease->first <= last && (lease->sealed || !lease->held))
            return FALSE;
    return TRUE;
}
#endif

#if CONF_WITH_X86_64_MEMTEST
static BOOL fail_next_adoption;
void kaspace_test_fail_lease_after(UWORD count) { fail_lease_after = count; }
void kaspace_test_fail_next(void) { fail_next_adoption = TRUE; }
#endif

KASPACE *kaspace_adopt(ASPACE *backend)
{
    KASPACE *space;
    if (!backend) return NULL;
#if CONF_WITH_X86_64_MEMTEST
    if (fail_next_adoption) { fail_next_adoption = FALSE; return NULL; }
#endif
    space = kalloc(sizeof(*space));
    if (!space) return NULL;
    space->backend = backend;
    space->refs = 1;
    space->leases = NULL;
    space->next = spaces;
    spaces = space;
    space->heap.blocks = NULL;
    space->heap.count = space->heap.capacity = 0;
#if CONF_WITH_MMAP
    space->mmap.regions = NULL;
    space->mmap.bytes = 0;
#endif
    return space;
}

KASPACE *kaspace_share(KASPACE *space)
{
    if (!space || space->refs == (ULONG)0xffffffffUL) return NULL;
    space->refs++;
    return space;
}

ASPACE *kaspace_backend(const KASPACE *space)
{
    return space ? space->backend : NULL;
}

KASPACE_HEAP *kaspace_heap(KASPACE *space)
{
    return space ? &space->heap : NULL;
}

BOOL kaspace_heap_contains(KASPACE *space, UQUAD address)
{
    ULONG i;
    if (!space) return FALSE;
    for (i = 0; i < space->heap.count; i++) {
        const KASPACE_HEAP_BLOCK *b = &space->heap.blocks[i];
        if (address >= b->va && address < (UQUAD)b->va + (UQUAD)b->pages * USER_PAGE_SIZE)
            return TRUE;
    }
    return FALSE;
}

int kaspace_heap_demand_page(KASPACE *space, UQUAD address, UWORD access)
{
#if CONF_WITH_DEMAND_PAGING
    VADDR va;
    if (!kaspace_heap_contains(space, address)) return -1;
    if (access & ~(ASPACE_PROT_USER | ASPACE_PROT_WRITE)) return 0;
    va = (VADDR)address;
#if CONF_WITH_PRIVATE_CLONE
    {
        int result = aspace_cow_fault(space->backend, va, access);
        if (result >= 0) return result;
    }
#endif
    if (aspace_translate(space->backend, va, NULL, NULL)) return 0;
    return aspace_fault_in(space->backend, va, ASPACE_PROT_USER | ASPACE_PROT_WRITE) ? 1 : 2;
#else
    UNUSED(space); UNUSED(address); UNUSED(access);
    return -1;
#endif
}

#if CONF_WITH_MMAP
KASPACE_MMAP *kaspace_mmap(KASPACE *space)
{
    return space ? &space->mmap : NULL;
}

static MMAP_REGION *mmap_find(KASPACE *space, UQUAD address)
{
    MMAP_REGION *region;
    if (!space) return NULL;
    for (region = space->mmap.regions; region; region = region->next)
        if (address >= region->address && address < (UQUAD)region->address + region->length)
            return region;
    return NULL;
}

BOOL kaspace_mmap_protection(KASPACE *space, UQUAD address, UWORD *protection)
{
    MMAP_REGION *region = mmap_find(space, address);
    if (!region) return FALSE;
    if (protection) *protection = region->prot;
    return TRUE;
}

static int mmap_back(KASPACE *space, MMAP_REGION *region, VADDR va, UWORD access)
{
    if (region->backing)
        return aspace_fault_file(space->backend, va, region->backing,
                region->offset + (ULONG)((va & ~(USER_PAGE_SIZE - 1)) - region->address),
                region->prot, (access & ASPACE_PROT_WRITE) != 0);
    return aspace_fault_in(space->backend, va, region->prot) ? 1 : 2;
}

int kaspace_mmap_fault(KASPACE *space, UQUAD address, UWORD access)
{
    MMAP_REGION *region = mmap_find(space, address);
    if (!region) return -1;
    if (!(region->prot & ASPACE_PROT_USER) || (access & ~region->prot)) return 0;
    return mmap_back(space, region, (VADDR)address, access);
}

int kaspace_mmap_demand_page(KASPACE *space, UQUAD address, UWORD access)
{
    MMAP_REGION *region = mmap_find(space, address);
    VADDR va;
    if (!region) return -1;
    if (!(region->prot & ASPACE_PROT_USER) || (access & ~region->prot)) return 0;
    va = (VADDR)address;
#if CONF_WITH_PRIVATE_CLONE
    {
        int result = aspace_cow_fault(space->backend, va, access);
        if (result >= 0) return result;
    }
#endif
    if (aspace_translate(space->backend, va, NULL, NULL) &&
        !((access & ASPACE_PROT_WRITE) && aspace_file_cow(space->backend, va)))
        return 0;
    return mmap_back(space, region, va, access);
}

void kaspace_shutdown_file_mappings(void)
{
    KASPACE *space;
    for (space = spaces; space; space = space->next) {
        MMAP_REGION **link = &space->mmap.regions, *region;
        while ((region = *link)) {
            if (!region->backing ||
                !aspace_release_lazy(space->backend, region->address, region->length)) {
                link = &region->next;
                continue;
            }
            *link = region->next;
            space->mmap.bytes -= region->length;
            fileback_put(region->backing);
            kfree(region);
        }
    }
    fileback_reap();
}
#endif

void kaspace_put(KASPACE *space)
{
    if (!space) return;
    if (--space->refs) return;
    {
        KASPACE **link;
        for (link = &spaces; *link; link = &(*link)->next)
            if (*link == space) { *link = space->next; break; }
    }
    aspace_destroy(space->backend);
    kfree(space->heap.blocks);
#if CONF_WITH_MMAP
    while (space->mmap.regions) {
        MMAP_REGION *region = space->mmap.regions;
        space->mmap.regions = region->next;
        fileback_put(region->backing);
        kfree(region);
    }
#endif
    kfree(space);
}

LONG kaspace_clone(const KASPACE *source, KASPACE **result)
{
#if CONF_WITH_PRIVATE_CLONE
    KASPACE *copy;
    LONG error = ENSMEM;
    if (!source || !result || *result) return EINVAL;
    copy = kalloc(sizeof(*copy));
    if (!copy) return ENSMEM;
    copy->refs = 1;
    if (source->heap.count) {
        if (source->heap.count > ~(ULONG)0 / sizeof(*copy->heap.blocks)) goto failed;
        copy->heap.blocks = kalloc(source->heap.count * sizeof(*copy->heap.blocks));
        if (!copy->heap.blocks) goto failed;
        memcpy(copy->heap.blocks, source->heap.blocks, source->heap.count * sizeof(*copy->heap.blocks));
        copy->heap.count = copy->heap.capacity = source->heap.count;
    }
#if CONF_WITH_MMAP
    {
        MMAP_REGION *region, **tail = &copy->mmap.regions;
        for (region = source->mmap.regions; region; region = region->next) {
            MMAP_REGION *cloned = kalloc(sizeof(*cloned));
            if (!cloned) goto failed;
            *cloned = *region; cloned->next = NULL;
            if (cloned->backing && !fileback_try_hold(cloned->backing)) {
                kfree(cloned); error = EAGAIN; goto failed;
            }
            *tail = cloned; tail = &cloned->next;
        }
        copy->mmap.bytes = source->mmap.bytes;
    }
#endif
    /* All generic allocations precede backend source write-protection. */
    error = aspace_clone(source->backend, &copy->backend);
    if (error) goto failed;
    copy->next = spaces; spaces = copy;
    *result = copy;
    return E_OK;
failed:
    kfree(copy->heap.blocks);
#if CONF_WITH_MMAP
    while (copy->mmap.regions) {
        MMAP_REGION *region = copy->mmap.regions;
        copy->mmap.regions = region->next;
        fileback_put(region->backing); kfree(region);
    }
#endif
    kfree(copy);
    return error;
#else
    (void)source;
    if (result) *result = NULL;
    return EINVFN;
#endif
}
