/* Generic lease policy, host execution under all three user-ABI configs.
 * Backend mutation hooks themselves are exercised by target runtime tests. */
#include "emutos.h"
#include "procmem.h"
#include "kheap.h"
#include "kaspace.h"
#include "gemerror.h"
#include <assert.h>
#include <stdlib.h>
struct aspace { UWORD protection[2]; unsigned destroyed; };
static unsigned allocations, frees, fail_alloc;
void *kalloc(ULONG bytes)
{
    void *p;
    if (fail_alloc) return NULL;
    p = malloc(bytes); if (p) allocations++; return p;
}
void kfree(void *p) { if (p) { frees++; free(p); } }
void aspace_destroy(ASPACE *space) { assert(!space->destroyed); space->destroyed++; }
BOOL aspace_translate(const ASPACE *space, VADDR va, PADDR *phys, UWORD *prot)
{
    unsigned index;
    (void)phys;
    if (va < 0x1000 || va >= 0x3000) return FALSE;
    index = (va - 0x1000) / USER_PAGE_SIZE;
    if (!space->protection[index]) return FALSE;
    if (prot) *prot = space->protection[index];
    return TRUE;
}
#if CONF_WITH_MMAP
void fileback_put(FILE_BACKING *backing) { assert(!backing); }
#endif
int main(void)
{
    ASPACE a = { { ASPACE_PROT_USER | ASPACE_PROT_WRITE, ASPACE_PROT_USER }, 0 };
    ASPACE b = { { ASPACE_PROT_USER | ASPACE_PROT_WRITE, ASPACE_PROT_USER }, 0 };
    KASPACE *one = kaspace_adopt(&a), *two = kaspace_adopt(&b);
    KASPACE_LEASE *leases[2] = { NULL, NULL }, *other = NULL;
    unsigned baseline = allocations - frees;
    assert(one && two);
    assert(kaspace_lease_acquire(one,0x1fff,2,ASPACE_PROT_USER|ASPACE_PROT_WRITE,&leases[0]) == EIMBA);
    assert(allocations - frees == baseline && !leases[0]);
    fail_alloc = 1;
    assert(kaspace_lease_acquire(one,0x1000,1,ASPACE_PROT_USER,&leases[0]) == ENSMEM);
    fail_alloc = 0;
    assert(kaspace_lease_acquire(one,0x1000,1,0,&leases[0]) == EINVAL);
    assert(kaspace_lease_acquire(one,0x1000,1,ASPACE_PROT_USER|0x8000,&leases[0]) == EINVAL);
    assert(kaspace_lease_acquire(one,USER_VA_LIMIT-1,2,ASPACE_PROT_USER,&leases[0]) == EINVAL);
    assert(kaspace_lease_acquire(one,0x1000,1,ASPACE_PROT_USER,&leases[0]) == E_OK);
    assert(kaspace_lease_acquire(one,0x2000,1,ASPACE_PROT_USER,&leases[1]) == E_OK);
    assert(kaspace_lease_acquire(two,0x1000,1,ASPACE_PROT_USER,&other) == E_OK);
    /* Same-VA mutations in another space do not revoke these identities. */
    assert(kaspace_mapping_change(&b,0x1000,1) && !kaspace_lease_valid(other));
    assert(kaspace_lease_valid(leases[0]) && kaspace_lease_valid(leases[1]));
    assert(kaspace_mapping_change(&a,0x2000,1));
    a.protection[1] = 0; a.protection[1] = ASPACE_PROT_USER;
    assert(!kaspace_lease_valid(leases[1]));
    assert(!kaspace_lease_seal(leases,2));
    /* Failed group seal must not seal the earlier, still-valid lease. */
    assert(kaspace_mapping_change(&a,0x1000,1));
    kaspace_lease_release(&leases[0]); kaspace_lease_release(&leases[1]);
    assert(kaspace_lease_acquire(one,0x1000,1,ASPACE_PROT_USER,&leases[0]) == E_OK);
    assert(kaspace_lease_acquire(one,0x2000,1,ASPACE_PROT_USER,&leases[1]) == E_OK);
    assert(kaspace_lease_seal(leases,2));
    assert(!kaspace_mapping_change(&a,0x1000,2*USER_PAGE_SIZE));
    assert(kaspace_lease_valid(leases[0]) && kaspace_lease_valid(leases[1]));
    kaspace_put(one); kaspace_put(two);
    assert(!a.destroyed && !b.destroyed); /* leases independently retain both */
    kaspace_lease_release(&other); assert(b.destroyed == 1);
    kaspace_lease_release(&leases[1]); assert(!a.destroyed);
    kaspace_lease_release(&leases[0]); assert(a.destroyed == 1);
    kaspace_lease_release(&leases[0]);
    assert(allocations == frees);
    return 0;
}
