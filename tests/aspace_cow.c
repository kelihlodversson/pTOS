/* Production anonymous-COW/metadata code with modeled leaves and RAM.
 * This is NOT native MMU/fault/TLB evidence. */
#include "emutos.h"
#include "gemerror.h"
#include "procmem.h"
#include "kheap.h"
#include "kaspace.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../bios/aspace_cow_types.h"
#define N 32
struct leaf { PADDR physical; UWORD prot; BOOL present, owned; };
struct aspace { struct leaf leaves[N]; struct cow_mapping *cow; };
static UBYTE ram[N][4096];
static BOOL allocated[N], fail_page, fail_map;
static ULONG allocations, frees, flushes, live_pages, fail_allocation;
void panic(const char *format, ...) { (void)format; abort(); }
void *kalloc(ULONG bytes)
{
    void *p;
    if (fail_allocation && !--fail_allocation) return NULL;
    p = calloc(1,bytes); if (p) allocations++; return p;
}
void kfree(void *p) { if (p) { frees++; free(p); } }
static PADDR page_alloc(void)
{
    unsigned i;
    if (fail_page) return 0;
    for (i=1;i<N;i++) if (!allocated[i]) {
        allocated[i]=TRUE; live_pages++; memset(ram[i],0,4096); return i*4096;
    }
    return 0;
}
static void page_free(PADDR physical)
{ assert(physical && allocated[physical/4096]); allocated[physical/4096]=FALSE; live_pages--; }
#define COW_FREE(p) page_free(p)
#define COW_ALLOC() page_alloc()
#define COW_NONE 0
#define COW_CODE_CHANGED() ((void)0)
#define COW_ALIAS(p) ram[(p)/4096]
#define COW_RAM(p) ((p) && (p)/4096<N && allocated[(p)/4096])
BOOL aspace_translate(const ASPACE *as,VADDR va,PADDR *physical,UWORD *prot)
{
    const struct leaf *leaf;
    if (va/4096>=N) return FALSE;
    leaf=&as->leaves[va/4096]; if (!leaf->present) return FALSE;
    if (physical) *physical=leaf->physical+(va%4096);
    if (prot) *prot=leaf->prot;
    return TRUE;
}
static BOOL cow_map(ASPACE *as,VADDR va,PADDR p,UWORD prot,BOOL owned)
{
    if (fail_map || clone_injected()) return FALSE;
    as->leaves[va/4096]=(struct leaf){p,prot,TRUE,owned}; return TRUE;
}
static BOOL cow_replace(ASPACE *as,VADDR va,PADDR p,UWORD prot)
{
    assert(as->leaves[va/4096].present);
    if (fail_map) return FALSE;
    as->leaves[va/4096].physical=p; as->leaves[va/4096].prot=prot; return TRUE;
}
static void cow_flush(const ASPACE *as) { (void)as; flushes++; }
static LONG cow_file_leaf(ASPACE *as,VADDR va,PADDR p,UWORD prot)
{ (void)as;(void)va;(void)p;(void)prot;return 1; }
#include "../bios/aspace_cow.h"
void aspace_destroy(ASPACE *as)
{
    unsigned i;
    if (!as) return;
    for(i=0;i<N;i++) if(as->leaves[i].present && as->leaves[i].owned) {
        PADDR p=as->leaves[i].physical;
        cow_unlink_mapping(as,i*4096);
        if(!cow_release_backing(p)) page_free(p);
    }
    assert(!as->cow); kfree(as);
}
LONG aspace_clone(ASPACE *source,ASPACE **out)
{
    unsigned i;
    LONG error;
    ASPACE *child;
    if(!source||!out||*out)return EINVAL;
    if(!kaspace_cow_change(source,0,USER_VA_LIMIT))return EACCDN;
    child=clone_allocate(sizeof(*child)); if(!child)return ENSMEM;
    for(i=0;i<N;i++) if(source->leaves[i].present) {
        struct leaf *leaf=&source->leaves[i];
        error=cow_clone_leaf(source,child,i*4096,leaf->physical,leaf->prot,leaf->owned);
        if(error) { aspace_destroy(child);cow_finish_source(source,FALSE);return error; }
    }
    cow_finish_source(source,TRUE); *out=child;return E_OK;
}
BOOL aspace_private_range_ok(const ASPACE *as,VADDR va,VADDR bytes)
{ (void)bytes;return va/4096<N && as->leaves[va/4096].owned; }
BOOL aspace_fault_in(ASPACE *as,VADDR va,UWORD prot)
{
    PADDR p=page_alloc(); if(!p)return FALSE;
    if(!cow_map(as,va,p,prot,TRUE)) {page_free(p);return FALSE;} return TRUE;
}
static void write_byte(ASPACE *as,VADDR address,UBYTE byte)
{
    assert(aspace_cow_fault(as,address,ASPACE_PROT_WRITE)==1);
    ram[as->leaves[address/4096].physical/4096][address%4096]=byte;
}
int main(void)
{
    ASPACE *parent=kalloc(sizeof(*parent)),*child=NULL,*third=NULL;
    KASPACE *one,*two=NULL;
    KASPACE_HEAP *heap;
    unsigned n,before,cycle;
    PADDR physical=page_alloc(),code=page_alloc();
    UWORD logical;
    assert(cow_map(parent,4096,physical,ASPACE_PROT_USER|ASPACE_PROT_WRITE,TRUE));
    assert(cow_map(parent,8192,code,ASPACE_PROT_USER|ASPACE_PROT_EXEC,TRUE));
    ram[physical/4096][17]=11;
    before=allocations-frees;
    for(n=1;n<32;n++) {
        LONG result;
        aspace_clone_test_fail_after(n);
        result=aspace_clone(parent,&child);
        aspace_clone_test_fail_after(0);
        if(!result) {aspace_destroy(child);child=NULL;break;}
        assert(result==ENSMEM && !child && !parent->cow && !cow_frame_count && !cow_reference_count);
        assert(allocations-frees==before && live_pages==2);
        assert(parent->leaves[1].prot&ASPACE_PROT_WRITE);
    }
    assert(n<32);
    assert(aspace_logical_protection(parent,4096,&logical) && (logical&ASPACE_PROT_WRITE));
    for(cycle=0;cycle<12;cycle++) {
        assert(aspace_clone(parent,&child)==E_OK);
        assert(parent->leaves[1].physical==child->leaves[1].physical);
        assert(!(parent->leaves[1].prot&ASPACE_PROT_WRITE) && !(child->leaves[1].prot&ASPACE_PROT_WRITE));
        assert(aspace_cow_fault(child,8192,ASPACE_PROT_WRITE)==0);
        fail_page=TRUE;
        assert(aspace_cow_fault(child,4096,ASPACE_PROT_WRITE)==2);
        fail_page=FALSE;
        assert(child->leaves[1].physical==parent->leaves[1].physical);
        assert(aspace_clone(child,&third)==E_OK);
        write_byte(cycle&1?parent:child,4096+17,22);
        assert(ram[third->leaves[1].physical/4096][17]==11);
        write_byte(third,4096+18,33);
        aspace_destroy(third);third=NULL;
        aspace_destroy(child);child=NULL;
        assert(aspace_cow_fault(parent,4096,ASPACE_PROT_WRITE)==1 ||
               (parent->leaves[1].prot&ASPACE_PROT_WRITE));
        ram[parent->leaves[1].physical/4096][17]=11;
    }
    one=kaspace_adopt(parent);assert(one);
    heap=kaspace_heap(one);heap->blocks=kalloc(sizeof(*heap->blocks));
    heap->blocks[0]=(KASPACE_HEAP_BLOCK){4096,2};heap->count=heap->capacity=1;
    before=allocations-frees;
    for(n=1;n<32;n++) {
        LONG result;
        fail_allocation=n;
        result=kaspace_clone(one,&two);fail_allocation=0;
        if(!result)break;
        assert(!two && allocations-frees==before);
    }
    assert(two && kaspace_heap(two)->blocks!=heap->blocks);
    kaspace_heap(two)->blocks[0].pages=1;
    assert(heap->blocks[0].pages==2);
    kaspace_put(one); /* parent-first final release */
    assert(kaspace_heap_contains(two,4096));
    write_byte(kaspace_backend(two),4096+17,44);
    kaspace_put(two);
    assert(!cow_frame_count && !cow_reference_count && !live_pages && allocations==frees && flushes);
    puts("Production COW metadata/refcount/rollback with modeled MMU: PASS");
    return 0;
}
