/* Production allocator records, modeled physical window; never native evidence. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#if defined(ALLOCATION_HOLD_GENERIC)
/* Headers for libc are already selected; only the production MMU-core guard
 * is changed for hosted compilation, never a kernel configuration. */
#undef __x86_64__
#include "../bios/procmem.c"
#define __x86_64__ 1
#else
#include "../bios/machine/pc-x86_64/procmem.c"
#endif
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"allocation hold:%d: %s\n",__LINE__,#c); abort(); } } while (0)
static unsigned allocated, freed, fail_alloc, notices;
void *kalloc(ULONG bytes) { void *p; if(fail_alloc) return NULL; p=calloc(1,bytes); if(p) allocated++; return p; }
void kfree(void *p) { CHECK(p); freed++; free(p); }
static void notify(void *token) { CHECK(token==&notices); notices++; }
int main(void)
{
    int owner, replacement, canonical;
    void *p, *q, *identity=NULL;
    UQUAD generation;
    PROCMEM_HOLD a={0},b={0},stale={0};
    PROCMEM_HOLD_INFO info;
    struct alloc **allocation;
    window_phys=0x200000; free_pages=WINDOW_PAGES;
    p=procmem_alloc(12288,0); CHECK(p); generation=procmem_gen(p);
    CHECK(procmem_set_owner(p,&owner));
    CHECK(!procmem_hold_acquire(p,generation+1,&owner,0,1,ASPACE_PROT_USER,7,&canonical,&a));
    CHECK(!procmem_hold_acquire(p,generation,&replacement,0,1,ASPACE_PROT_USER,7,&canonical,&a));
    CHECK(!procmem_hold_acquire(p,generation,&owner,12287,2,ASPACE_PROT_USER,7,&canonical,&a));
    CHECK(!procmem_hold_acquire(p,generation,&owner,0,1,0,7,&canonical,&a));
    fail_alloc=1;
    CHECK(!procmem_hold_acquire(p,generation,&owner,0,1,ASPACE_PROT_USER,7,&canonical,&a));
    fail_alloc=0; CHECK(!a.ticket && !a.allocation && !a.base);
    allocation=find_link(p); (*allocation)->hold_count=(ULONG)~0UL;
    CHECK(!procmem_hold_acquire(p,generation,&owner,0,1,ASPACE_PROT_USER,7,&canonical,&a));
    (*allocation)->hold_count=0;
    CHECK(procmem_hold_acquire(p,generation,&owner,4096,4096,ASPACE_PROT_USER|ASPACE_PROT_WRITE,7,&canonical,&a));
    CHECK(procmem_hold_info(&a,&info) && info.offset==4096 && info.bytes==4096 &&
          info.owner_identity==&canonical && info.owner_number==7 && info.rights==(ASPACE_PROT_USER|ASPACE_PROT_WRITE));
    stale=a;
    fail_alloc=1;
    CHECK(!procmem_hold_acquire(p,generation,&owner,4096,4096,ASPACE_PROT_USER,7,&canonical,&b));
    fail_alloc=0; CHECK(!b.ticket && procmem_hold_info(&a,&info));
    CHECK(!procmem_free(p));
    CHECK(!procmem_shrink(p,4096) && procmem_size(p)==12288);
    CHECK(procmem_shrink(p,8192) && procmem_size(p)==8192);
    CHECK(!procmem_set_owner(p,&replacement) && procmem_owner(p)==&owner);
    CHECK(procmem_set_owner(p,&owner));
    CHECK(procmem_hold_acquire(p,generation,&owner,4096,4096,ASPACE_PROT_USER,7,&canonical,&b));
    CHECK(procmem_on_free(p,notify,&notices));
    procmem_free_owned(&owner,NULL);
    CHECK(!procmem_owner(p) && procmem_size(p)==8192 && !notices);
    CHECK(!procmem_set_owner(p,NULL)); /* cannot cancel a retired owner's reclaim */
    {
        PROCMEM_HOLD child = {0};
        CHECK(procmem_hold_duplicate(&a,&canonical,&child));
        CHECK(child.ticket != a.ticket && child.allocation == a.allocation);
        CHECK(procmem_hold_release(&child,&identity) && identity==&canonical);
        identity=NULL;
    }
    CHECK(procmem_hold_release(&a,&identity) && identity==&canonical && !a.ticket);
    identity=NULL;
    CHECK(!procmem_hold_release(&stale,&identity) && !identity);
    CHECK(procmem_size(p)==8192 && !notices);
    procmem_pin((VADDR)(uintptr_t)p,8192,1);
    CHECK(procmem_hold_release(&b,&identity) && identity==&canonical);
    CHECK(procmem_size(p)==8192 && !notices);
    procmem_pin((VADDR)(uintptr_t)p,8192,-1);
    CHECK(!procmem_size(p) && notices==1 && allocated==freed);
    q=procmem_alloc(12288,0); CHECK(q==p && procmem_gen(q)!=generation);
    identity=NULL; CHECK(!procmem_hold_release(&stale,&identity));
    CHECK(procmem_set_owner(q,&owner));
    hold_tickets=~(UQUAD)0-1;
    CHECK(procmem_hold_acquire(q,procmem_gen(q),&owner,0,1,ASPACE_PROT_USER,7,&canonical,&a));
    CHECK(a.ticket==~(UQUAD)0);
    CHECK(!procmem_hold_acquire(q,procmem_gen(q),&owner,0,1,ASPACE_PROT_USER,7,&canonical,&b));
    CHECK(procmem_hold_release(&a,&identity)); identity=NULL;
    CHECK(procmem_free(q));
#if defined(ALLOCATION_HOLD_GENERIC)
    gens=~(UQUAD)0-2;
#else
    next_gen=~(UQUAD)0-1;
#endif
    q=procmem_alloc(4096,0); CHECK(q && procmem_gen(q)==~(UQUAD)0-1);
    CHECK(!procmem_alloc(4096,0) && procmem_next_gen()==~(UQUAD)0);
    CHECK(procmem_free(q) && allocated==freed && free_pages==WINDOW_PAGES);
    puts("Allocation holds: exact generation/extent/rights/owner, stale tickets, saturation, shrink barrier, owner retirement, deferred pin release and exhaustion: PASS (host)");
    return 0;
}
