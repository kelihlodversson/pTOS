/* Actual loader text-publication function, modeled leaves/allocation identity.
 * Native MMU/PRG/ELF execution is separately required on real target emulators. */
#include "emutos.h"
#include "gemerror.h"
#include "procmem.h"
#include "kaspace.h"
#include <assert.h>
#include <stdlib.h>
/* Expose the common 32-bit-backend interface to this host model; do not
 * change the generated target config or model a CPU return context. */
#define MACHINE_VIRT_M68K 1 /* only selects unused window in the modeled backend header */
#if defined(__x86_64__)
#undef __x86_64__
#include "aspace32.h"
#define __x86_64__ 1
#else
#include "aspace32.h"
#endif
#define PAGE USER_PAGE_SIZE
struct aspace { PADDR root; };
static UWORD rights[3], ownership[3];
static BOOL live=TRUE, permitted=TRUE, present[3];
static unsigned changes, flushes;
static PADDR physical[3];
PADDR procmem_phys_of(VADDR page) { return page + 0x100000; }
BOOL procmem_range_live(VADDR va,VADDR bytes)
{ return live && va>=0x10000 && bytes<=0x13000-va; }
BOOL kaspace_mapping_change(ASPACE *as,VADDR va,VADDR bytes)
{ assert(as && va==0x10000 && bytes==3*PAGE); return permitted; }
BOOL aspace_hw_lookup(PADDR root,VADDR va,PADDR *p,UWORD *prot,UWORD *flags)
{
    unsigned n=(va-0x10000)/PAGE;
    assert(root==1 && n<3);
    if(!present[n]) return FALSE;
    *p=physical[n]; *prot=rights[n]; *flags=ownership[n]; return TRUE;
}
BOOL aspace_hw_update(PADDR root,VADDR va,UWORD prot,UWORD flags)
{
    unsigned n=(va-0x10000)/PAGE;
    assert(root==1 && n<3 && present[n] && flags==ownership[n]);
    rights[n]=prot; changes++; return TRUE;
}
void aspace_hw_code_changed(void) { flushes++; }
static void flush_if_current(const ASPACE *as) { assert(as->root==1); }
void panic(const char *fmt, ...) { (void)fmt; abort(); }
#include "../../bios/aspace_procmem_text.h"
int main(void)
{
    ASPACE as; unsigned i;
    as.root=1;
    for(i=0;i<3;i++) {
        rights[i]=ASPACE_PROT_USER|ASPACE_PROT_WRITE; present[i]=TRUE;
        physical[i]=procmem_phys_of(0x10000+i*PAGE);
    }
    /* Invalid second/final leaves must not partially mark earlier text. */
    present[1]=FALSE; assert(!aspace_mark_procmem_text(&as,0x10100,0x2000) && !changes);
    present[1]=TRUE; ownership[2]=ASPACE_LEAF_OWNED;
    assert(!aspace_mark_procmem_text(&as,0x10100,0x2000) && !changes);
    ownership[2]=0; physical[2]++;
    assert(!aspace_mark_procmem_text(&as,0x10100,0x2000) && !changes);
    physical[2]--; rights[2]=ASPACE_PROT_USER;
    assert(!aspace_mark_procmem_text(&as,0x10100,0x2000) && !changes);
    rights[2]=ASPACE_PROT_USER|ASPACE_PROT_WRITE; permitted=FALSE;
    assert(!aspace_mark_procmem_text(&as,0x10100,0x2000) && !changes);
    permitted=TRUE; live=FALSE;
    assert(!aspace_mark_procmem_text(&as,0x10100,0x2000) && !changes);
    live=TRUE;
    assert(aspace_mark_procmem_text(&as,0x10100,0x2000) && changes==3 && flushes==1);
    for(i=0;i<3;i++) assert(rights[i]==(ASPACE_PROT_USER|ASPACE_PROT_WRITE|ASPACE_PROT_EXEC) && !ownership[i]);
    return 0;
}
