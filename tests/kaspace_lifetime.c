/* Real KASPACE implementation with a counted fake backend and allocator. */
#include "emutos.h"
#include "biosext.h"
#include "procmem.h"
#include "kheap.h"
#include "gemerror.h"
#include "kaspace.h"
#include <assert.h>
#include <stdlib.h>

struct aspace { int marker; };
static int fail_alloc, allocations, frees, destroys;
void *kalloc(ULONG size)
{
    void *p;
    if (fail_alloc) return NULL;
    p = malloc(size);
    if (p) allocations++;
    return p;
}
void kfree(void *p) { if (p) { frees++; free(p); } }
void aspace_destroy(ASPACE *as) { assert(as && as->marker == 42); destroys++; }
#if CONF_WITH_DEMAND_PAGING
BOOL aspace_translate(const ASPACE *as, VADDR va, PADDR *physical, UWORD *protection)
{ (void)as; (void)va; (void)physical; (void)protection; return FALSE; }
BOOL aspace_fault_in(ASPACE *as, VADDR va, UWORD protection)
{ (void)as; (void)va; (void)protection; return TRUE; }
#endif

BOOL aspace_private_range_ok(const ASPACE *as, VADDR address, VADDR bytes)
{ (void)as; (void)address; (void)bytes; return TRUE; }
int main(void)
{
    ASPACE backend = { 42 };
    KASPACE *a, *b, *out = (KASPACE *)1;
    assert(!kaspace_adopt(NULL));
    fail_alloc = 1;
    assert(!kaspace_adopt(&backend) && !destroys);
    fail_alloc = 0;
    a = kaspace_adopt(&backend);
    assert(a && kaspace_backend(a) == &backend);
    b = kaspace_share(a);
    assert(b == a && kaspace_backend(b) == &backend);
    assert(kaspace_clone(a, &out) == EINVFN && !out);
    kaspace_put(a);
    assert(!destroys && !frees && kaspace_backend(b) == &backend);
    kaspace_put(b);
    assert(destroys == 1 && allocations == 1 && frees == 1);
    kaspace_put(NULL);
    return 0;
}

void panic(const char *format, ...) { (void)format; abort(); }

BOOL aspace_release_lazy(ASPACE *as,VADDR va,VADDR bytes) { (void)as;(void)va;(void)bytes;abort();return FALSE; }

void aspace_unborrow(ASPACE *as,VADDR va,VADDR bytes) { (void)as;(void)va;(void)bytes;abort(); }
