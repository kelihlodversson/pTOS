/* Address-space ownership; mapping and cloning remain backend responsibilities. */
#include "emutos.h"
#include "gemerror.h"
#include "kheap.h"
#include "procmem.h"
#include "kaspace.h"

struct kaspace {
    ASPACE *backend;
    ULONG refs;
#if CONF_WITH_MMAP
    KASPACE_MMAP mmap;
    struct kaspace *next;
#endif
};

#if CONF_WITH_MMAP
static KASPACE *spaces; /* borrowed registry; serialized with retain/release */
#endif

#if CONF_WITH_X86_64_MEMTEST
static BOOL fail_next_adoption;
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
#if CONF_WITH_MMAP
    space->mmap.regions = NULL;
    space->mmap.bytes = 0;
    space->next = spaces;
    spaces = space;
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

#if CONF_WITH_MMAP
KASPACE_MMAP *kaspace_mmap(KASPACE *space)
{
    return space ? &space->mmap : NULL;
}

int kaspace_mmap_fault(KASPACE *space, UQUAD address, UWORD access)
{
    MMAP_REGION *region;
    if (!space) return -1;
    for (region = space->mmap.regions; region; region = region->next) {
        VADDR va;
        if (address < region->address || address >= (UQUAD)region->address + region->length)
            continue;
        if (!(region->prot & ASPACE_PROT_USER) || (access & ~region->prot)) return 0;
        va = (VADDR)address;
        if (region->backing)
            return aspace_fault_file(space->backend, va, region->backing,
                    region->offset + (ULONG)((va & ~(USER_PAGE_SIZE - 1)) - region->address),
                    region->prot, (access & ASPACE_PROT_WRITE) != 0);
        return aspace_fault_in(space->backend, va, region->prot) ? 1 : 2;
    }
    return -1;
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
#if CONF_WITH_MMAP
    {
        KASPACE **link;
        for (link = &spaces; *link; link = &(*link)->next)
            if (*link == space) { *link = space->next; break; }
    }
#endif
    aspace_destroy(space->backend);
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
