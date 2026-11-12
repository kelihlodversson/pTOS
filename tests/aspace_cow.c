/* Production anonymous-COW/metadata code with modeled leaves and RAM.
 * This is NOT native MMU/fault/TLB evidence. */
#include "emutos.h"
#include "biosext.h"
#include "gemerror.h"
#include "procmem.h"
#include "kheap.h"
#include "kaspace.h"
#include "fileback.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static ULONG index_visits;
#define COW_INDEX_VISIT() (index_visits++)
void panic(const char *format, ...);
#include "../bios/aspace_cow_types.h"
#define N 16386
struct leaf { PADDR physical; UWORD prot; BOOL present, owned; };
struct aspace { struct leaf leaves[N]; struct cow_mapping *cow; struct cow_index *cow_root; };
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
    static unsigned cursor=1;
    unsigned i,n;
    if (fail_page) return 0;
    for (n=1;n<N;n++) {
        i=cursor; if (++cursor==N) cursor=1;
        if (!allocated[i]) {
            allocated[i]=TRUE; live_pages++; memset(ram[i],0,4096); return i*4096;
        }
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
#include "../bios/aspace_file_index.h"
static ULONG file_queries;
static BOOL file_hint=TRUE;
static LONG cow_file_leaf(ASPACE *as,VADDR va,PADDR p,UWORD prot)
{
    (void)as;(void)va;(void)prot;
    file_queries++;
    /* These stress-cache records are unrelated to the modeled RAM leaves. */
    assert(!file_page_find(p));
    return 1;
}
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
    assert(!as->cow && !as->cow_root); kfree(as);
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
        error=cow_clone_leaf(source,child,i*4096,leaf->physical,leaf->prot,leaf->owned,file_hint);
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
/* Structural and operation-count assertions, not a wall-clock benchmark. */
static unsigned check_index(const struct cow_index *root, UQUAD low, UQUAD high)
{
    unsigned left,right;
    if (!root) return 0;
    assert(root->key>=low && root->key<high);
    left=check_index(root->left,low,root->key);
    right=check_index(root->right,root->key+1,high);
    assert((int)cow_height(root->left)-(int)cow_height(root->right)>=-1 &&
           (int)cow_height(root->left)-(int)cow_height(root->right)<=1);
    assert(root->height==1+(cow_height(root->left)>cow_height(root->right)?
                                cow_height(root->left):cow_height(root->right)));
    return 1+left+right;
}
static void populated_clone(unsigned count)
{
    ASPACE *source=kalloc(sizeof(*source)), *child=NULL, *nested=NULL;
    struct cow_mapping *mapping;
    unsigned i;
    ULONG visits;
    for(i=1;i<=count;i++) {
        PADDR physical=page_alloc(); assert(physical);
        assert(cow_map(source,i*4096,physical,ASPACE_PROT_USER|ASPACE_PROT_WRITE,TRUE));
    }
    index_visits=0;
    assert(aspace_clone(source,&child)==E_OK);
    visits=index_visits;
    /* The old two growing-list searches exceed268million visits at16384.
     * Count all indexed searches/insertions, including rotation preparation. */
    assert(visits>count && visits<96UL*count);
    assert(check_index(source->cow_root,0,(UQUAD)N*4096)==count);
    assert(check_index(child->cow_root,0,(UQUAD)N*4096)==count);
    assert(check_index(cow_frames,0,(UQUAD)N*4096)==count);
    for(mapping=source->cow;mapping;mapping=mapping->next)
        assert(*mapping->previous==mapping && cow_find(source,mapping->address)==mapping);
    assert(aspace_clone(child,&nested)==E_OK);
    aspace_destroy(source); source=NULL;
    aspace_destroy(nested); nested=NULL;
    /* Odd permutation exercises interior/root/two-child AVL deletion through
     * exclusive promotion, without allocating replacement physical pages. */
    for(i=0;i<count;i++) {
        unsigned page=1+((i*4051UL)&(count-1));
        assert(aspace_cow_fault(child,page*4096,ASPACE_PROT_WRITE)==1);
        if (!(i&255)) {
            assert(check_index(child->cow_root,0,(UQUAD)N*4096)==count-i-1);
            assert(check_index(cow_frames,0,(UQUAD)N*4096)==count-i-1);
        }
    }
    assert(!cow_frames);
    aspace_destroy(child);
    assert(!cow_frames && !cow_frame_count && !cow_reference_count && !live_pages);
    printf("Indexed COW clone %u populated pages: %lu metadata visits (file hint %d), exact reclamation PASS\n",
           count,(unsigned long)visits,file_hint);
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
    heap->blocks[0]=(KASPACE_HEAP_BLOCK){4096,2,kaspace_reservation_next()};heap->count=heap->capacity=1;
    before=allocations-frees;
    for(n=1;n<32;n++) {
        LONG result;
        fail_allocation=n;
        result=kaspace_clone(one,&two);fail_allocation=0;
        if(!result)break;
        assert(!two && allocations-frees==before);
    }
    assert(two && kaspace_heap(two)->blocks!=heap->blocks);
    assert(heap->blocks[0].reservation && kaspace_heap(two)->blocks[0].reservation &&
           heap->blocks[0].reservation!=kaspace_heap(two)->blocks[0].reservation);
    kaspace_heap(two)->blocks[0].pages=1;
    assert(heap->blocks[0].pages==2);
    kaspace_put(one); /* parent-first final release */
    assert(kaspace_heap_contains(two,4096));
    write_byte(kaspace_backend(two),4096+17,44);
    kaspace_put(two);
    assert(!cow_frame_count && !cow_reference_count && !live_pages && allocations==frees && flushes);
    populated_clone(1024);
    populated_clone(16384);
    {
        unsigned i;
        ULONG queries;
        FILE_PAGE **cache=calloc(16384,sizeof(*cache));
        assert(cache);
        /* Production cache index with16384 unrelated metadata records, not
         * modeled allocated physical pages or real filesystem ownership. */
        for(i=0;i<16384;i++) {
            FILE_PAGE *page=kalloc(sizeof(*page)); assert(page);
            page->physical=((UQUAD)1<<40)+(UQUAD)i*4096;
            page->references=1; cache[i]=page; file_page_publish(page);
        }
        assert(check_index(file_page_index,0,((UQUAD)1<<41))==16384);
        queries=file_queries;
        populated_clone(1024); populated_clone(16384);
        assert(file_queries-queries==2UL*(1024+16384));
        /* x32's authoritative non-FILE PTE hint skips cache lookup entirely. */
        file_hint=FALSE; queries=file_queries;
        populated_clone(1024); populated_clone(16384);
        assert(file_queries==queries);
        for(i=0;i<16384;i++) {
            unsigned slot=(i*4051UL)&16383;
            FILE_PAGE *page=cache[slot];
            assert(file_page_find(page->physical)==page && *page->previous==page);
            file_page_unpublish(page); kfree(page);
            if (!(i&255))
                assert(check_index(file_page_index,0,((UQUAD)1<<41))==16383-i);
        }
        assert(!file_pages && !file_page_index); free(cache);
    }
    assert(allocations==frees);
    puts("Production COW metadata/refcount/rollback with modeled MMU: PASS");
    return 0;
}

BOOL aspace_release_lazy(ASPACE *as,VADDR va,VADDR bytes) { (void)as;(void)va;(void)bytes;abort();return FALSE; }

void aspace_unborrow(ASPACE *as,VADDR va,VADDR bytes) { (void)as;(void)va;(void)bytes;abort(); }
