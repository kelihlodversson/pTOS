/* Actual KPROC capture policy; MMU and existing witness lookup are modeled. */
#include "emutos.h"
#include "../../bdos/kproc.c"
#undef assert
#include <assert.h>
#include <stdlib.h>
static PD pd;
static KPROC process;
static struct uheap_block image;
static BOOL captured,grant_user=TRUE,mutation_permitted;
static VADDR mutation_address,mutation_bytes;
static UQUAD allocation_generation;
static const void *allocation_owner;
static KASPACE_HEAP ordinary;
#if CONF_WITH_MMAP
static KASPACE_MMAP mappings;
KASPACE_MMAP *kaspace_mmap(KASPACE *space) { (void)space; return &mappings; }
#endif
void *kalloc(ULONG bytes) { return malloc(bytes); }
void kfree(void *p) { free(p); }
void panic(const char *format,...) { (void)format;abort(); }
KASPACE *kaspace_share(KASPACE *space) { assert(space==(KASPACE *)1);return space; }
void kaspace_put(KASPACE *space) { assert(space==(KASPACE *)1); }
BOOL kaspace_mutation_busy(const KASPACE *space) { assert(space==(KASPACE *)1);return FALSE; }
BOOL kaspace_mutation_reentrant(const KASPACE *space) { assert(space==(KASPACE *)1);return FALSE; }
BOOL kaspace_mutation_begin(KASPACE *space) { assert(space==(KASPACE *)1);return TRUE; }
void kaspace_mutation_end(KASPACE *space) { assert(space==(KASPACE *)1); }
#if CONF_WITH_NATIVE_SCHED
BOOL ksched_wait_ready(BOOL (*ready)(void *),void *context)
{ (void)ready;(void)context;abort();return FALSE; }
void ksched_cleanup_begin(KSCHED_CLEANUP_SCOPE *scope) { (void)scope; }
void ksched_cleanup_end(KSCHED_CLEANUP_SCOPE *scope) { (void)scope; }
#endif
KASPACE_HEAP *kaspace_heap(KASPACE *space) { (void)space; return &ordinary; }
BOOL kaspace_next_held(const KASPACE *space,VADDR from,VADDR to,VADDR *first,VADDR *end)
{ (void)space;(void)from;(void)to;(void)first;(void)end;return FALSE; }
BOOL kaspace_mapping_can_change(const ASPACE *space,VADDR address,VADDR bytes)
{ (void)space;mutation_address=address;mutation_bytes=bytes;return mutation_permitted; }
BOOL kaspace_mapping_change(ASPACE *space,VADDR address,VADDR bytes)
{ (void)space;(void)address;(void)bytes;assert(0);return FALSE; }
BOOL aspace_map_private(ASPACE *space,VADDR address,VADDR bytes,UWORD rights)
{ (void)space;(void)address;(void)bytes;(void)rights;assert(0);return FALSE; }
static VADDR captured_address=0x10000000UL, captured_bytes=2*USER_PAGE_SIZE;
PD *run=&pd;
PADDR procmem_phys_of(VADDR address) { (void)address;assert(allocation_generation);return 0x1000; }
const void *procmem_owner(const void *base) { (void)base;return allocation_owner; }
UQUAD procmem_gen(const void *base) { (void)base;return allocation_generation; }
UQUAD kaspace_reservation_next(void) { assert(0); return 0; }
BOOL kaspace_reservation_info(KASPACE *space,VADDR address,VADDR bytes,KASPACE_RESERVATION_INFO *info)
{
    (void)space;
    if (!captured || address<captured_address || address-captured_address>=captured_bytes ||
        bytes>captured_bytes-(address-captured_address)) return FALSE;
    info->address=captured_address;info->bytes=captured_bytes;info->identity=7;
    info->flags=KASPACE_RESERVATION_CAPTURED;info->rights=ASPACE_PROT_USER;
    return TRUE;
}
BOOL aspace_translate(const ASPACE *space,VADDR address,PADDR *physical,UWORD *prot)
{
    (void)space;
    if (address<0x10000000UL || address>=0x10000000UL+2*USER_PAGE_SIZE) return FALSE;
    if (physical) *physical=0x1000;
    if (prot) *prot=(grant_user?ASPACE_PROT_USER:0)|(address<0x10000000UL+USER_PAGE_SIZE?ASPACE_PROT_EXEC:ASPACE_PROT_WRITE);
    return TRUE;
}
#if CONF_WITH_PRIVATE_CLONE
BOOL aspace_logical_protection(const ASPACE *space,VADDR address,UWORD *prot)
{ return aspace_translate(space,address,NULL,prot); }
#endif
BOOL aspace_private_range_ok(const ASPACE *space,VADDR address,VADDR bytes)
{ (void)space;(void)address;(void)bytes;return TRUE; }
BOOL aspace_file_cow(const ASPACE *space,VADDR address) { (void)space;(void)address;return FALSE; }
BOOL kaspace_heap_protection(KASPACE *space,UQUAD address,UWORD *prot)
{
    ULONG i;
    if (!space || !prot) return FALSE;
    for (i=0;i<ordinary.count;i++) {
        const KASPACE_HEAP_BLOCK *block=&ordinary.blocks[i];
        if (address>=block->va && address-block->va<(VADDR)block->pages*USER_PAGE_SIZE) {
            *prot=(block->flags&KASPACE_HEAP_GUARD)?0:ASPACE_PROT_USER|ASPACE_PROT_WRITE;
            return TRUE;
        }
    }
    return FALSE;
}
BOOL kaspace_heap_contains(KASPACE *space,UQUAD address) { (void)space;(void)address;return FALSE; }
BOOL kaspace_mmap_protection(KASPACE *space,UQUAD address,UWORD *prot)
{ (void)space;(void)address;(void)prot;return FALSE; }
BOOL kaspace_retained_lazy_protection(KASPACE *space,VADDR address,UWORD *prot)
{ (void)space;(void)address;(void)prot;return FALSE; }
static void check_rights(void)
{
    KASPACE_RESERVATION_INFO info;
    assert(kproc_reservation_info(&pd,0x10000000UL,2*USER_PAGE_SIZE,&info)==E_OK && info.rights==ASPACE_PROT_USER);
    assert(kproc_reservation_info(&pd,0x10000000UL,USER_PAGE_SIZE,&info)==E_OK && info.rights==(ASPACE_PROT_USER|ASPACE_PROT_EXEC));
    assert(kproc_reservation_info(&pd,0x10000000UL+USER_PAGE_SIZE,USER_PAGE_SIZE,&info)==E_OK && info.rights==(ASPACE_PROT_USER|ASPACE_PROT_WRITE));
}
int main(void)
{
    KASPACE_RESERVATION_INFO info,unchanged;
    memset(&process,0,sizeof(process));memset(&image,0,sizeof(image));
    process.pd=&pd;process.active_space=(KASPACE *)1;process.aspace=(ASPACE *)1;
    process.references=1;process.allocation_scope=(KASPACE_SCOPE *)1;
    process.heap=&image;process.nheap=1; kproc_list=&process;
    image.va=0x10000000UL;image.pages=2;image.kind=UH_IMAGE;image.reservation=7;
    check_rights(); /* no earlier broad witness */
    assert(!kproc_uheap_alloc(&pd,USER_PAGE_SIZE) && !ordinary.count);
    kfree(ordinary.blocks);memset(&ordinary,0,sizeof(ordinary));
    captured=TRUE;check_rights(); /* conservative earlier broad capture */
    process.heap=NULL;process.nheap=0;check_rights(); /* immutable bounds after metadata is gone */
    captured_address=0x10000008UL;captured_bytes=16;
    memset(&info,0x5a,sizeof(info));unchanged=info;
    assert(kproc_reservation_info(&pd,0x10000ff0UL,16,&info)==EACCDN && !memcmp(&info,&unchanged,sizeof(info)));
    captured=FALSE;allocation_generation=9;allocation_owner=&pd;
    process.borrowed[0].va=0x10000000UL;process.borrowed[0].bytes=2*USER_PAGE_SIZE;
    process.borrowed[0].generation=9;process.borrowed[0].reservation=7;
    grant_user=FALSE;
    assert(kproc_reservation_info(&pd,0x10000008UL,16,&info)==EACCDN && !memcmp(&info,&unchanged,sizeof(info)));
    grant_user=TRUE;
    assert(kproc_reservation_info(&pd,0x10000008UL,16,&info)==E_OK && info.identity==7);
    unchanged=info;allocation_generation=10;
    assert(kproc_reservation_info(&pd,0x10000008UL,16,&info)==EACCDN && !memcmp(&info,&unchanged,sizeof(info)));
    allocation_generation=9;allocation_owner=(const void *)2;
    assert(kproc_reservation_info(&pd,0x10000008UL,16,&info)==EACCDN && !memcmp(&info,&unchanged,sizeof(info)));
    allocation_owner=&pd;
    assert(kproc_reservation_info(&pd,0x10000008UL,16,&info)==E_OK);
    {
        LONG old=kproc_psetlimit(&pd,PSETLIMIT_STACK,-1);
        LONG grown=old+2*USER_PAGE_SIZE;
        assert(old==(LONG)USER_STACK_SIZE);
        assert(kproc_psetlimit(&pd,PSETLIMIT_STACK,grown)==EACCDN &&
               kproc_psetlimit(&pd,PSETLIMIT_STACK,-1)==old && !process.stack_size);
        assert(mutation_address==USER_STACK_TOP-grown-USER_PAGE_SIZE &&
               mutation_bytes==3*USER_PAGE_SIZE); /* includes new and former guard */
        mutation_permitted=TRUE;
#if CONF_WITH_MMAP
        {
            MMAP_REGION occupied;
            memset(&occupied,0,sizeof(occupied));
            occupied.address=USER_STACK_TOP-grown;occupied.length=USER_PAGE_SIZE;
            mappings.regions=&occupied;
            assert(kproc_psetlimit(&pd,PSETLIMIT_STACK,grown)==ENSMEM &&
                   kproc_psetlimit(&pd,PSETLIMIT_STACK,-1)==old);
            mappings.regions=NULL;
        }
#endif
        assert(kproc_psetlimit(&pd,PSETLIMIT_STACK,grown)==old &&
               kproc_psetlimit(&pd,PSETLIMIT_STACK,-1)==grown);
    }
    {
        KASPACE_HEAP_BLOCK blocks[2];UWORD rights=0x5a5a;
        memset(blocks,0,sizeof(blocks));
        blocks[0].va=0x20000000UL;blocks[0].pages=1;blocks[0].flags=KASPACE_HEAP_GUARD;
        blocks[1].va=blocks[0].va+USER_PAGE_SIZE;blocks[1].pages=1;
        ordinary.blocks=blocks;ordinary.count=ordinary.capacity=2;
        /* A PROCESS-local compatibility block must not override a matching
         * authoritative ordinary-heap guard. Neither heap kind is executable. */
        image.va=blocks[0].va;image.pages=2;image.kind=UH_MALLOC;
        process.heap=&image;process.nheap=1;
        assert(!kaspace_heap_protection(process.active_space,0x21000000UL,&rights) && rights==0x5a5a);
        assert(!lazy_area(&process,blocks[0].va,&rights));
#if CONF_WITH_DEMAND_PAGING
        assert(!rights);
        assert(lazy_area(&process,blocks[1].va,&rights) && rights==(ASPACE_PROT_USER|ASPACE_PROT_WRITE));
#else
        assert(!lazy_area(&process,blocks[1].va,&rights));
#endif
        assert(!kproc_validate_user_exec(blocks[0].va) && !kproc_validate_user_exec(blocks[1].va));
        memset(&ordinary,0,sizeof(ordinary));
    }
    return 0;
}
