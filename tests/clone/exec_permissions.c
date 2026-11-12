/* Production KPROC validation. Shared MMU policy is modeled here; real m68k
 * descriptor storage and nested/orphan execution are tested natively. */
#include "emutos.h"
#include "../../bdos/kproc.c"
#undef assert
#include <assert.h>
PD caller, child_pd;
PD *run = &caller;
static KPROC parent, child;
static UWORD text = ASPACE_PROT_USER | ASPACE_PROT_EXEC;
BOOL kaspace_mmap_protection(KASPACE *space, UQUAD address, UWORD *prot)
{ (void)space; (void)address; (void)prot; return FALSE; }
BOOL kaspace_retained_lazy_protection(KASPACE *space, VADDR address, UWORD *prot)
{ (void)space; (void)address; (void)prot; return FALSE; }
BOOL kaspace_heap_contains(KASPACE *space, UQUAD address)
{ (void)space; (void)address; return FALSE; }
BOOL aspace_translate(const ASPACE *space, VADDR address, PADDR *physical, UWORD *prot)
{
    UWORD policy;
    if (space != (ASPACE *)1 || address < 0x10000000 || address >= 0x10004000) return FALSE;
    policy = address < 0x10001000 ? text : ASPACE_PROT_USER | ASPACE_PROT_WRITE;
    if(physical) *physical=0x1000;
    if(prot) *prot=policy;
    return TRUE;
}
#if CONF_WITH_PRIVATE_CLONE
BOOL aspace_logical_protection(const ASPACE *space, VADDR address, UWORD *prot)
{ return aspace_translate(space,address,NULL,prot); }
#endif
int main(void)
{
    struct uheap_block block;
    memset(&parent,0,sizeof(parent)); memset(&child,0,sizeof(child)); memset(&block,0,sizeof(block));
    parent.pd=&caller; parent.aspace=(ASPACE *)1;
    parent.heap=&block; parent.nheap=1; parent.next=&child; kproc_list=&parent;
    block.va=0x10000000; block.pages=4; block.kind=UH_IMAGE;
    parent.layout.nseg=1; parent.layout.seg[0].vaddr=block.va;
    parent.layout.seg[0].memsz=USER_PAGE_SIZE; parent.layout.seg[0].flags=1;
    child.pd=&child_pd; child.aspace=parent.aspace;
    assert(kproc_validate_user_exec(block.va+16));
    assert(!kproc_validate_user_exec(block.va+USER_PAGE_SIZE+16));
    run=&child_pd; /* no inherited PROCESS-local heap/layout */
    assert(kproc_validate_user_exec(block.va+16));
    assert(!kproc_validate_user_exec(block.va+USER_PAGE_SIZE+16));
    assert(!kproc_validate_user_exec(block.va+2*USER_PAGE_SIZE));
    assert(!kproc_validate_user_exec(block.va+3*USER_PAGE_SIZE));
    assert(!kproc_validate_user_exec(block.va+4*USER_PAGE_SIZE));
    /* Original owner gone; its PD is reused for an unrelated address space. */
    parent.heap=NULL; parent.nheap=0; memset(&parent.layout,0,sizeof(parent.layout));
    parent.aspace=(ASPACE *)2;
    assert(kproc_validate_user_exec(block.va+16));
    assert(!kproc_validate_user_exec(block.va+USER_PAGE_SIZE+16));
    text=ASPACE_PROT_USER; assert(!kproc_validate_user_exec(block.va+16));
    return 0;
}
