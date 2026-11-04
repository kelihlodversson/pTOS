/* Address-space ownership; mapping and cloning remain backend responsibilities. */
#include "emutos.h"
#include "gemerror.h"
#include "kheap.h"
#include "procmem.h"
#include "kaspace.h"

struct kaspace {
    ASPACE *backend;
    ULONG refs;
};

KASPACE *kaspace_adopt(ASPACE *backend)
{
    KASPACE *space;
    if (!backend) return NULL;
    space = kalloc(sizeof(*space));
    if (!space) return NULL;
    space->backend = backend;
    space->refs = 1;
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

void kaspace_put(KASPACE *space)
{
    if (!space) return;
    if (--space->refs) return;
    aspace_destroy(space->backend);
    kfree(space);
}

LONG kaspace_clone(const KASPACE *source, KASPACE **result)
{
    (void)source;
    if (result) *result = NULL;
    return EINVFN;
}
