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
