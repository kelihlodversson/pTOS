/* Host-executed policy/lifetime test using each architecture's configuration. */
#include "emutos.h"
#include "procmem.h"
#include "kheap.h"
#include "kaspace.h"
#include <assert.h>
#include <stdlib.h>

struct aspace { int destroyed; VADDR mapped; BOOL cow; };
struct file_backing { int references; };
static int pages, file_pages, releases, reaps;

void *kalloc(ULONG bytes) { return malloc(bytes); }
void kfree(void *p) { free(p); }
void aspace_destroy(ASPACE *as) { assert(as && !as->destroyed); as->destroyed = 1; }
BOOL aspace_translate(const ASPACE *as, VADDR va, PADDR *physical, UWORD *protection)
{
    (void)physical; (void)protection;
    return as->mapped == (va & ~(USER_PAGE_SIZE - 1));
}
BOOL aspace_file_cow(const ASPACE *as, VADDR va) { (void)va; return as->cow; }
BOOL aspace_fault_in(ASPACE *as, VADDR va, UWORD protection)
{
    assert(protection & ASPACE_PROT_USER);
    pages++; as->mapped = va & ~(USER_PAGE_SIZE - 1); return TRUE;
}
int aspace_fault_file(ASPACE *as, VADDR va, struct file_backing *backing,
                      ULONG offset, UWORD protection, BOOL write)
{
    assert(backing && backing->references == 1 && offset == 0);
    assert(protection & ASPACE_PROT_USER);
    if (write) { assert(as->cow && (protection & ASPACE_PROT_WRITE)); as->cow = FALSE; }
    file_pages++; as->mapped = va & ~(USER_PAGE_SIZE - 1); return 1;
}
BOOL aspace_release_lazy(ASPACE *as, VADDR va, VADDR bytes)
{
    assert(va == 0x2000 && bytes == USER_PAGE_SIZE);
    as->mapped = 0; releases++; return TRUE;
}
void fileback_put(FILE_BACKING *backing) { if (backing) backing->references--; }
void fileback_reap(void) { reaps++; }

BOOL aspace_private_range_ok(const ASPACE *as, VADDR address, VADDR bytes)
{ (void)as; (void)address; (void)bytes; return TRUE; }
int main(void)
{
    ASPACE backend = { 0, 0, FALSE };
    FILE_BACKING backing = { 1 };
    KASPACE *owner, *retained;
    MMAP_REGION *anonymous, *file;
    UWORD prot = 0;

    owner = kaspace_adopt(&backend);
    assert(owner && (retained = kaspace_share(owner)) == owner);
    kaspace_heap(owner)->blocks = kalloc(sizeof(*kaspace_heap(owner)->blocks));
    assert(kaspace_heap(owner)->blocks);
    kaspace_heap(owner)->blocks[0].va = 0x4000;
    kaspace_heap(owner)->blocks[0].pages = 1;
    kaspace_heap(owner)->count = kaspace_heap(owner)->capacity = 1;
    anonymous = kalloc(sizeof(*anonymous)); file = kalloc(sizeof(*file));
    assert(anonymous && file);
    anonymous->address = 0x1000; anonymous->length = USER_PAGE_SIZE;
    anonymous->prot = ASPACE_PROT_USER | ASPACE_PROT_WRITE;
    anonymous->backing = NULL; anonymous->offset = 0; anonymous->next = file;
    file->address = 0x2000; file->length = USER_PAGE_SIZE;
    file->prot = ASPACE_PROT_USER; file->backing = &backing;
    file->offset = 0; file->next = NULL;
    kaspace_mmap(owner)->regions = anonymous;
    kaspace_mmap(owner)->bytes = 2 * USER_PAGE_SIZE;
    kaspace_put(owner);
    assert(!backend.destroyed && kaspace_mmap_protection(retained, 0x1000, &prot));
    assert(kaspace_heap_contains(retained, 0x4000));
    assert(kaspace_heap_demand_page(retained, 0x4000, ASPACE_PROT_EXEC) == 0 && !pages);
    assert(kaspace_heap_demand_page(retained, 0x4000, 0) == 1 && pages == 1);
    assert(prot == (ASPACE_PROT_USER | ASPACE_PROT_WRITE));
    assert(kaspace_mmap_demand_page(retained, 0x1000, ASPACE_PROT_EXEC) == 0 && pages == 1);
    assert(kaspace_mmap_demand_page(retained, 0x1000, 0) == 1 && pages == 2);
    assert(kaspace_mmap_demand_page(retained, 0x1000, 0) == 0 && pages == 2);
    assert(kaspace_mmap_demand_page(retained, 0x2000, ASPACE_PROT_WRITE) == 0 && !file_pages);
    assert(kaspace_mmap_demand_page(retained, 0x2000, 0) == 1 && file_pages == 1);
    file->prot |= ASPACE_PROT_WRITE;
    backend.cow = TRUE;
    assert(kaspace_mmap_demand_page(retained, 0x2000, ASPACE_PROT_WRITE) == 1 &&
           file_pages == 2 && !backend.cow);
    assert(kaspace_mmap_demand_page(retained, 0x3000, 0) == -1);
    kaspace_shutdown_file_mappings();
    assert(releases == 1 && reaps == 1 && !backing.references &&
           kaspace_mmap(retained)->regions == anonymous && !anonymous->next &&
           kaspace_mmap(retained)->bytes == USER_PAGE_SIZE);
    kaspace_put(retained);
    assert(backend.destroyed);
    return 0;
}
