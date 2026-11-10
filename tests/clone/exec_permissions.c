/* Production KPROC entry validation; page tables and leases are host models. */
#include "emutos.h"
#include "../../bdos/kproc.c"
#undef assert
#include <assert.h>
PD caller;
PD *run = &caller;
static KPROC process;
static UWORD resident = ASPACE_PROT_USER | ASPACE_PROT_EXEC;
BOOL kaspace_mmap_protection(KASPACE *space, UQUAD address, UWORD *prot)
{ (void)space; (void)address; (void)prot; return FALSE; }
BOOL kaspace_heap_contains(KASPACE *space, UQUAD address)
{ (void)space; (void)address; return FALSE; }
BOOL aspace_translate(const ASPACE *space, VADDR address, PADDR *physical, UWORD *prot)
{ (void)space; (void)address; if(physical) *physical=0x1000; if(prot) *prot=resident; return TRUE; }
int main(void)
{
    struct uheap_block block;
    memset(&process,0,sizeof(process)); memset(&block,0,sizeof(block));
    process.pd=&caller; process.aspace=(ASPACE *)1;
    process.heap=&block; process.nheap=1; kproc_list=&process;
    block.va=0x10000000; block.pages=3; block.kind=UH_IMAGE;
    process.layout.nseg=2;
    process.layout.seg[0].vaddr=block.va;
    process.layout.seg[0].memsz=USER_PAGE_SIZE; process.layout.seg[0].flags=1;
    process.layout.seg[1].vaddr=block.va+USER_PAGE_SIZE;
    process.layout.seg[1].memsz=USER_PAGE_SIZE; process.layout.seg[1].flags=2;
    /* Demand-paged staged text remains executable. No-NX hardware must not
     * make its writable data or unclaimed holes eligible entry points. */
    assert(kproc_validate_user_exec(block.va+16));
    assert(!kproc_validate_user_exec(block.va+USER_PAGE_SIZE+16));
    assert(!kproc_validate_user_exec(block.va+2*USER_PAGE_SIZE));
    resident=ASPACE_PROT_USER;
    assert(!kproc_validate_user_exec(block.va+16));
    resident=ASPACE_PROT_USER|ASPACE_PROT_EXEC;
    block.kind=UH_MALLOC;
    assert(!kproc_validate_user_exec(block.va+16));
    return 0;
}
