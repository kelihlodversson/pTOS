/* Address-space ownership; mapping and cloning remain backend responsibilities. */
#include "emutos.h"
#include "gemerror.h"
#include "kheap.h"
#include "procmem.h"
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
    BOOL revoked, sealed;
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
        if (!aspace_translate(lease->space->backend, page, NULL, &prot) ||
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
    lease->revoked = lease->sealed = FALSE;
    if (!kaspace_lease_valid(lease)) { kfree(lease); return EIMBA; }
    if (!kaspace_share(space)) { kfree(lease); return ENSMEM; }
    lease->next = space->leases;
    space->leases = lease;
    *result = lease;
    return E_OK;
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
        if (lease->sealed && first <= lease->last && lease->first <= last) return FALSE;
    for (lease = space->leases; lease; lease = lease->next)
        if (first <= lease->last && lease->first <= last) lease->revoked = TRUE;
    return TRUE;
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
    (void)source;
    if (result) *result = NULL;
    return EINVFN;
}
