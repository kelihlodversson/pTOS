/* Actual KPROC capture policy; MMU and existing witness lookup are modeled. */
#include "emutos.h"
#include "../../bdos/kproc.c"
#undef assert
#include <assert.h>
#include <stdlib.h>
static PD pd;
static KPROC process;
static struct uheap_block image;
static BOOL captured;
static KASPACE_HEAP ordinary;
#if CONF_WITH_MMAP
static KASPACE_MMAP mappings;
KASPACE_MMAP *kaspace_mmap(KASPACE *space) { (void)space; return &mappings; }
#endif
void *kalloc(ULONG bytes) { return malloc(bytes); }
void kfree(void *p) { free(p); }
KASPACE_HEAP *kaspace_heap(KASPACE *space) { (void)space; return &ordinary; }
BOOL kaspace_mapping_can_change(const ASPACE *space,VADDR address,VADDR bytes)
{ (void)space;(void)address;(void)bytes;return FALSE; }
BOOL kaspace_mapping_change(ASPACE *space,VADDR address,VADDR bytes)
{ (void)space;(void)address;(void)bytes;assert(0);return FALSE; }
BOOL aspace_map_private(ASPACE *space,VADDR address,VADDR bytes,UWORD rights)
{ (void)space;(void)address;(void)bytes;(void)rights;assert(0);return FALSE; }
static VADDR captured_address=0x10000000UL, captured_bytes=2*USER_PAGE_SIZE;
PD *run=&pd;
PADDR procmem_phys_of(VADDR address) { (void)address; assert(0); return 0; }
UQUAD procmem_gen(const void *base) { (void)base; return 0; }
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
    if (prot) *prot=ASPACE_PROT_USER|(address<0x10000000UL+USER_PAGE_SIZE?ASPACE_PROT_EXEC:ASPACE_PROT_WRITE);
    return TRUE;
}
#if CONF_WITH_PRIVATE_CLONE
BOOL aspace_logical_protection(const ASPACE *space,VADDR address,UWORD *prot)
{ return aspace_translate(space,address,NULL,prot); }
#endif
BOOL aspace_private_range_ok(const ASPACE *space,VADDR address,VADDR bytes)
{ (void)space;(void)address;(void)bytes;return TRUE; }
BOOL aspace_file_cow(const ASPACE *space,VADDR address) { (void)space;(void)address;return FALSE; }
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
    return 0;
}
