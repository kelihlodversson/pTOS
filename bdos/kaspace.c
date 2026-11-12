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
    UQUAD reservation_ticket, reservation_id, owner_number;
    VADDR exact_address, exact_bytes;
    void *owner_identity;
    UWORD reservation_rights, reservation_flags;
    BOOL captured;
    KASPACE_LEASE *next;
};

#if CONF_WITH_X86_64_MEMTEST
static UWORD fail_lease_after;
#endif

BOOL kaspace_lease_valid(const KASPACE_LEASE *lease)
{
    VADDR page;
    if (!lease || lease->revoked) return FALSE;
    if (lease->reservation_ticket) {
        KASPACE_RESERVATION_INFO info;
        if (lease->captured) return lease->held;
        return kaspace_reservation_info(lease->space,lease->exact_address,lease->exact_bytes,&info) &&
               info.identity == lease->reservation_id && (info.rights & lease->access) == lease->access;
    }
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
    lease->reservation_ticket = lease->reservation_id = lease->owner_number = 0;
    lease->owner_identity = NULL; lease->exact_address = lease->exact_bytes = 0;
    lease->reservation_rights = lease->reservation_flags = 0; lease->captured = FALSE;
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

BOOL kaspace_mapping_can_change(const ASPACE *backend, VADDR address, VADDR bytes)
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
    return TRUE;
}
BOOL kaspace_mapping_change(ASPACE *backend, VADDR address, VADDR bytes)
{
    KASPACE *space;
    KASPACE_LEASE *lease;
    VADDR first, last;
    if (!kaspace_mapping_can_change(backend,address,bytes)) return FALSE;
    first = address & ~(VADDR)(USER_PAGE_SIZE - 1);
    last = (address + bytes - 1) & ~(VADDR)(USER_PAGE_SIZE - 1);
    for (space = spaces; space && space->backend != backend; space = space->next) { }
    if (!space) return TRUE; /* backend not yet adopted */
    for (lease = space->leases; lease; lease = lease->next)
        if (first <= lease->last && lease->first <= last) lease->revoked = TRUE;
    return TRUE;
}
BOOL kaspace_materialization_change(ASPACE *backend, VADDR address,
                                   VADDR bytes, UWORD rights)
{
    KASPACE *space;
    KASPACE_LEASE *lease;
    VADDR first, last;
    if (!bytes || address >= USER_VA_LIMIT || bytes > USER_VA_LIMIT-address) return FALSE;
    first = address & ~(VADDR)(USER_PAGE_SIZE-1);
    last = (address+bytes-1) & ~(VADDR)(USER_PAGE_SIZE-1);
    for (space=spaces;space && space->backend!=backend;space=space->next) { }
    if (!space) return TRUE;
    for (lease=space->leases;lease;lease=lease->next) {
        if (first > lease->last || lease->first > last) continue;
        if (lease->reservation_ticket) {
            KASPACE_RESERVATION_INFO info;
            if (!kaspace_lease_valid(lease)) return FALSE;
            if (lease->captured) {
                /* Different authenticated owner views can cover the same
                 * stack page. Validate each immutable witness independently. */
                if (!(lease->reservation_flags&KASPACE_RESERVATION_LAZY) ||
                    rights!=lease->reservation_rights) return FALSE;
            } else if (!kaspace_reservation_info(space,address,bytes,&info) ||
                       info.identity!=lease->reservation_id || rights!=info.rights) return FALSE;
        } else if (lease->sealed || lease->held) return FALSE;
    }
    /* Physical translations can change; unsealed alias snapshots are revoked.
     * Reservation holds retain logical birth/rights, never a physical alias. */
    for (lease=space->leases;lease;lease=lease->next)
        if (!lease->reservation_ticket && first <= lease->last && lease->first <= last)
            lease->revoked=TRUE;
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

static UQUAD reservation_ids, reservation_tickets;
UQUAD kaspace_reservation_next(void)
{
    if (reservation_ids == ~(UQUAD)0) return 0;
    return ++reservation_ids;
}

BOOL kaspace_reservation_info(KASPACE *space, VADDR address, VADDR bytes,
                             KASPACE_RESERVATION_INFO *result)
{
    ULONG i;
    if (!space || !result || !bytes || address >= USER_VA_LIMIT ||
        bytes > USER_VA_LIMIT-address) return FALSE;
    for (i=0;i<space->heap.count;i++) {
        KASPACE_HEAP_BLOCK *block = &space->heap.blocks[i];
        VADDR length = (VADDR)block->pages*USER_PAGE_SIZE;
        if (block->reservation && address >= block->va && address-block->va < length &&
            bytes <= length-(address-block->va)) {
            result->address = block->va; result->bytes = length;
            result->identity = block->reservation;
            result->rights = ASPACE_PROT_USER|ASPACE_PROT_WRITE;
            result->flags = KASPACE_RESERVATION_LAZY;
            return TRUE;
        }
    }
#if CONF_WITH_MMAP
    {
        MMAP_REGION *region;
        for (region=space->mmap.regions;region;region=region->next)
            if (region->reservation && address >= region->address &&
                address-region->address < region->length &&
                bytes <= region->length-(address-region->address)) {
                result->address = region->address; result->bytes = region->length;
                result->identity = region->reservation; result->rights = region->prot;
                result->flags = region->backing ? 0 : KASPACE_RESERVATION_LAZY;
                return TRUE;
            }
    }
#endif
    {
        KASPACE_LEASE *lease;
        for (lease=space->leases;lease;lease=lease->next)
            if (lease->captured && lease->held && !lease->revoked &&
                address >= lease->first && address-lease->first <= lease->last-lease->first+USER_PAGE_SIZE &&
                bytes <= lease->last-lease->first+USER_PAGE_SIZE-(address-lease->first)) {
                result->address = lease->first;
                result->bytes = lease->last-lease->first+USER_PAGE_SIZE;
                result->identity = lease->reservation_id;
                result->rights = lease->reservation_rights;
                result->flags = lease->reservation_flags|KASPACE_RESERVATION_CAPTURED;
                return TRUE;
            }
    }
    return FALSE;
}

static LONG reservation_install(KASPACE *space, const KASPACE_RESERVATION_INFO *info,
        VADDR address, VADDR bytes, UWORD rights, UQUAD owner_number,
        void *owner_identity, KASPACE_RESERVATION_HOLD *result)
{
    KASPACE_LEASE *lease;
    if (!result || result->ticket || !space || !info || !info->identity ||
        !owner_number || !owner_identity || !bytes ||
        info->address >= USER_VA_LIMIT || info->bytes > USER_VA_LIMIT-info->address ||
        address < info->address || address-info->address >= info->bytes ||
        bytes > info->bytes-(address-info->address) ||
        !(rights & ASPACE_PROT_USER) ||
        (rights & ~(ASPACE_PROT_USER|ASPACE_PROT_WRITE|ASPACE_PROT_EXEC)) ||
        (info->flags & ~(KASPACE_RESERVATION_LAZY|KASPACE_RESERVATION_CAPTURED))) return EINVAL;
    if ((info->rights & rights) != rights) return EACCDN;
    if (reservation_tickets == ~(UQUAD)0) return ENSMEM;
#if CONF_WITH_X86_64_MEMTEST
    if (fail_lease_after && !--fail_lease_after) return ENSMEM;
#endif
    lease = kalloc(sizeof(*lease));
    if (!lease) return ENSMEM;
    if (!kaspace_share(space)) { kfree(lease); return ENSMEM; }
    memset(lease,0,sizeof(*lease));
    lease->space = space; lease->first = address & ~(VADDR)(USER_PAGE_SIZE-1);
    lease->last = (address+bytes-1) & ~(VADDR)(USER_PAGE_SIZE-1);
    lease->access = rights; lease->held = TRUE;
    lease->exact_address = address; lease->exact_bytes = bytes;
    lease->reservation_id = info->identity; lease->owner_number = owner_number;
    lease->reservation_rights = info->rights; lease->reservation_flags = info->flags;
    lease->captured = (info->flags & KASPACE_RESERVATION_CAPTURED) != 0;
    lease->owner_identity = owner_identity; lease->reservation_ticket = ++reservation_tickets;
    lease->next = space->leases; space->leases = lease;
    result->ticket = lease->reservation_ticket;
    return E_OK;
}

LONG kaspace_reservation_hold(KASPACE *space, UQUAD identity,
        VADDR address, VADDR bytes, UWORD rights, UQUAD owner_number,
        void *owner_identity, KASPACE_RESERVATION_HOLD *result)
{
    KASPACE_RESERVATION_INFO info;
    if (!result || result->ticket || !identity || !owner_number || !owner_identity ||
        !(rights & ASPACE_PROT_USER) ||
        (rights & ~(ASPACE_PROT_USER|ASPACE_PROT_WRITE|ASPACE_PROT_EXEC))) return EINVAL;
    if (!kaspace_reservation_info(space,address,bytes,&info) || info.identity!=identity) return EACCDN;
    return reservation_install(space,&info,address,bytes,rights,owner_number,owner_identity,result);
}

LONG kaspace_reservation_hold_captured(KASPACE *space,
        const KASPACE_RESERVATION_INFO *description, VADDR address, VADDR bytes,
        UWORD rights, UQUAD owner_number, void *owner_identity, KASPACE_RESERVATION_HOLD *result)
{
    KASPACE_RESERVATION_INFO info;
    if (!description) return EINVAL;
    info = *description; info.flags |= KASPACE_RESERVATION_CAPTURED;
    return reservation_install(space,&info,address,bytes,rights,owner_number,owner_identity,result);
}

BOOL kaspace_retained_lazy_protection(KASPACE *space, VADDR address, UWORD *rights)
{
    KASPACE_RESERVATION_INFO info;
    if (!rights || !kaspace_reservation_info(space,address,1,&info) ||
        (info.flags & (KASPACE_RESERVATION_CAPTURED|KASPACE_RESERVATION_LAZY)) !=
        (KASPACE_RESERVATION_CAPTURED|KASPACE_RESERVATION_LAZY)) return FALSE;
    *rights = info.rights;
    return TRUE;
}

static KASPACE_LEASE *reservation_find(const KASPACE_RESERVATION_HOLD *hold)
{
    KASPACE *space;
    KASPACE_LEASE *lease;
    if (!hold || !hold->ticket) return NULL;
    for (space=spaces;space;space=space->next)
        for (lease=space->leases;lease;lease=lease->next)
            if (lease->reservation_ticket == hold->ticket) return lease;
    return NULL;
}

void *kaspace_reservation_owner(const KASPACE_RESERVATION_HOLD *hold)
{
    KASPACE_LEASE *lease = reservation_find(hold);
    return lease ? lease->owner_identity : NULL;
}

LONG kaspace_reservation_duplicate(const KASPACE_RESERVATION_HOLD *source,
        void *owner_identity, KASPACE_RESERVATION_HOLD *result)
{
    KASPACE_LEASE *lease = reservation_find(source), *copy;
    if (!result || result->ticket) return EINVAL;
    if (!lease || !owner_identity || lease->owner_identity != owner_identity ||
        !kaspace_lease_valid(lease)) return EACCDN;
    if (reservation_tickets == ~(UQUAD)0) return ENSMEM;
#if CONF_WITH_X86_64_MEMTEST
    if (fail_lease_after && !--fail_lease_after) return ENSMEM;
#endif
    copy = kalloc(sizeof(*copy));
    if (!copy) return ENSMEM;
    if (!kaspace_share(lease->space)) { kfree(copy); return ENSMEM; }
    *copy = *lease; copy->reservation_ticket = ++reservation_tickets;
    copy->next = lease->space->leases; lease->space->leases = copy;
    result->ticket = copy->reservation_ticket;
    return E_OK;
}

BOOL kaspace_reservation_release(KASPACE_RESERVATION_HOLD *hold, void **owner_identity)
{
    KASPACE *space;
    KASPACE_LEASE **link, *lease;
    if (!hold || !hold->ticket || !owner_identity || *owner_identity) return FALSE;
    /* Authenticate against live registry/list membership before dereferencing
     * any retained storage; a copied spent ticket cannot touch recycled space. */
    for (space=spaces;space;space=space->next)
        for (link=&space->leases;*link;link=&(*link)->next)
            if ((*link)->reservation_ticket == hold->ticket) {
                lease = *link; hold->ticket = 0; *link = lease->next;
                *owner_identity = lease->owner_identity;
                kfree(lease); kaspace_put(space);
                return TRUE;
            }
    return FALSE;
}

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
        {
            ULONG i;
            for (i=0;i<copy->heap.count;i++)
                if (!(copy->heap.blocks[i].reservation=kaspace_reservation_next())) goto failed;
        }
    }
#if CONF_WITH_MMAP
    {
        MMAP_REGION *region, **tail = &copy->mmap.regions;
        for (region = source->mmap.regions; region; region = region->next) {
            MMAP_REGION *cloned = kalloc(sizeof(*cloned));
            if (!cloned) goto failed;
            *cloned = *region; cloned->next = NULL;
            if (!(cloned->reservation=kaspace_reservation_next())) { kfree(cloned); goto failed; }
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
