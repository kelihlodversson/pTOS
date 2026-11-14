/* Production PROCESS and mapping policy, with counted hosted MMU/file models.
 * Keeping an observer reference to a root must not keep dead PROCESS mappings.
 * This is not a CPU launcher or public shared-Pexec execution test. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "../bdos/kproc.c"
#include "../bdos/kaspace.c"

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"shared-root:%d: %s\n",__LINE__,#c); exit(1); } } while (0)
struct model_page { VADDR address;UWORD protection;BOOL file,cow; };
struct aspace { unsigned destroyed,releases,pages;struct model_page page[64]; };
struct file_backing { unsigned references; };
static struct file_backing file;
static unsigned allocations, frees;
static unsigned fail_allocation,fail_file_hold;
static KASPACE_RESERVATION_HOLD *release_on_file_put;
static KASPACE_RESERVATION_HOLD *release_on_lazy;
static PD *mutating_pd;
static PD child;
PD *run = &child;
volatile LONG hz_200;

void *kalloc(ULONG bytes)
{ void *p;if(fail_allocation && !--fail_allocation) return NULL;p=calloc(1,bytes);if(p) allocations++;return p; }
void kfree(void *p) { if(p) { frees++; free(p); } }
void bzero(void *p,size_t n) { memset(p,0,n); }
void panic(const char *format,...) { fprintf(stderr,"panic: %s\n",format); abort(); }
void doassert(const char *f,long l,const char *fn,const char *e)
{ fprintf(stderr,"%s:%ld %s: %s\n",f,l,fn,e); abort(); }
SBYTE get_default_handle(int i) { (void)i; return -1; }
UQUAD procmem_gen(const void *p) { return p?1:0; }
ULONG procmem_size(const void *p) { (void)p; return 0; }
void kstack_free(PADDR p) { CHECK(!p); }
void aspace_leave(ASPACE_ROOT r) { CHECK(!r); }
void aspace_destroy(ASPACE *as) { CHECK(!as->destroyed); as->destroyed++; }
static struct model_page *model_page(ASPACE *as,VADDR address)
{
    unsigned i;address&=~(VADDR)(USER_PAGE_SIZE-1);
    for(i=0;i<as->pages;i++) if(as->page[i].address==address) return &as->page[i];
    return NULL;
}
BOOL aspace_translate(const ASPACE *as,VADDR va,PADDR *pa,UWORD *prot)
{
    struct model_page *page=model_page((ASPACE *)as,va);
    if(!page) return FALSE;
    if(pa) *pa=va&~(VADDR)(USER_PAGE_SIZE-1);
    if(prot) *prot=page->protection;
    return TRUE;
}
BOOL aspace_private_range_ok(const ASPACE *as,VADDR va,VADDR bytes)
{ (void)as;(void)va;(void)bytes;return TRUE; }
BOOL aspace_file_cow(const ASPACE *as,VADDR va)
{ struct model_page *p=model_page((ASPACE *)as,va);return p && p->cow; }
BOOL aspace_fault_in(ASPACE *as,VADDR va,UWORD prot)
{
    struct model_page *p;
    va&=~(VADDR)(USER_PAGE_SIZE-1);
    if(!kaspace_materialization_change(as,va,USER_PAGE_SIZE,prot)) return FALSE;
    p=model_page(as,va);
    if(!p) { CHECK(as->pages<64);p=&as->page[as->pages++]; }
    p->address=va;p->protection=prot;p->file=p->cow=FALSE;
    return TRUE;
}
BOOL aspace_map_private(ASPACE *as,VADDR va,VADDR bytes,UWORD prot)
{ (void)as;(void)va;(void)bytes;(void)prot;abort();return FALSE; }
int aspace_fault_file(ASPACE *as,VADDR va,FILE_BACKING *backing,ULONG offset,UWORD prot,BOOL write)
{
    struct model_page *p;
    CHECK(backing==&file && file.references && !(offset&(USER_PAGE_SIZE-1)));
    if(!aspace_fault_in(as,va,prot)) return 0;
    p=model_page(as,va);p->file=TRUE;p->cow=!write;
    return 1;
}
ULONG aspace_mapped_bytes(const ASPACE *as) { (void)as;return 0; }
BOOL aspace_release_lazy_allowed(const ASPACE *as,VADDR va,VADDR bytes)
{ (void)as;(void)va;(void)bytes;return TRUE; }
BOOL aspace_release_lazy(ASPACE *as,VADDR va,VADDR bytes)
{
    unsigned i=0;
    CHECK(kaspace_mapping_change(as,va,bytes));as->releases++;
    while(i<as->pages) {
        if(as->page[i].address>=va && as->page[i].address-va<bytes) as->page[i]=as->page[--as->pages];
        else i++;
    }
    if(release_on_lazy) {
        KASPACE_RESERVATION_HOLD *hold=release_on_lazy;
        release_on_lazy=NULL;
        CHECK(kproc_reservation_release(hold));
        CHECK(kproc_mmap(mutating_pd,0,USER_PAGE_SIZE,PROT_READ,MAP_PRIVATE|MAP_ANONYMOUS,0,0)==EACCDN);
    }
    return TRUE;
}
BOOL aspace_release_lazy_retired(ASPACE *as,VADDR va,VADDR bytes)
{
    unsigned i=0;
    CHECK(kaspace_retirement_change(as,va,bytes,TRUE));as->releases++;
    while(i<as->pages) {
        if(as->page[i].address>=va && as->page[i].address-va<bytes) as->page[i]=as->page[--as->pages];
        else i++;
    }
    return TRUE;
}
void aspace_unborrow(ASPACE *as,VADDR va,VADDR bytes)
{ (void)as;(void)va;(void)bytes;abort(); }
void aspace_unborrow_retired(ASPACE *as,VADDR va,VADDR bytes)
{ (void)as;(void)va;(void)bytes;abort(); }
BOOL aspace_defer_unborrow(ASPACE *as,VADDR va,VADDR bytes)
{ (void)as;(void)va;(void)bytes;abort();return FALSE; }
BOOL aspace_move_private(ASPACE *a,ASPACE *b,VADDR va,VADDR bytes,UWORD prot)
{ (void)a;(void)b;(void)va;(void)bytes;(void)prot;abort();return FALSE; }
BOOL aspace_protect_lazy(ASPACE *as,VADDR va,VADDR bytes,UWORD prot)
{ (void)prot;return kaspace_mapping_change(as,va,bytes); }
LONG fileback_get(LONG fd,FILE_BACKING **result)
{ CHECK(fd==0 && result && !*result);file.references++;*result=&file;return E_OK; }
ULONG fileback_size(const FILE_BACKING *f) { CHECK(f==&file);return 65536; }
void fileback_hold(FILE_BACKING *f) { if(f) { CHECK(f==&file && f->references);f->references++; } }
BOOL fileback_try_hold(FILE_BACKING *f)
{ if(!f || !f->references || (fail_file_hold && !--fail_file_hold)) return FALSE;f->references++;return TRUE; }
void fileback_put(FILE_BACKING *f)
{
    if(f) {
        CHECK(f==&file && f->references);f->references--;
        if(release_on_file_put) {
            KASPACE_RESERVATION_HOLD *hold=release_on_file_put;
            release_on_file_put=NULL;CHECK(kproc_reservation_release(hold));
        }
    }
}

static unsigned reservations(KASPACE *space)
{
    MMAP_REGION *r;
    unsigned count=kaspace_heap(space)->count;
    for(r=kaspace_mmap(space)->regions;r;r=r->next) count++;
    return count;
}
static void original_control(void)
{
    ASPACE backend={0};
    KASPACE *observer=kaspace_adopt(&backend);
    LONG anonymous,backed;
    unsigned before,live;
    CHECK(observer && kproc_create(&child));
    CHECK(kproc_test_share_active(&child,observer));
    before=reservations(observer);
    anonymous=kproc_mmap(&child,0,3*USER_PAGE_SIZE,PROT_NONE,
                        MAP_PRIVATE|MAP_ANONYMOUS,0,0);
    backed=kproc_mmap(&child,0,3*USER_PAGE_SIZE,PROT_READ,MAP_PRIVATE,0,0);
    CHECK(anonymous>0 && backed>0 && file.references==1);
    CHECK(kproc_mprot(&child,anonymous+USER_PAGE_SIZE,USER_PAGE_SIZE,PROT_READ)==E_OK);
    CHECK(kproc_munmap(&child,backed+USER_PAGE_SIZE,USER_PAGE_SIZE)==E_OK);
    live=reservations(observer);
    CHECK(live==before+5 && file.references==2);
    CHECK(kproc_begin_exit(&child));
    kproc_destroy(&child);
    printf("observer-root reservations: before=%u live=%u completed=%u; file refs=%u; root destroyed=%u\n",
           before,live,reservations(observer),file.references,backend.destroyed);
    fflush(stdout);
    CHECK(!backend.destroyed && !kproc_find(&child));
    CHECK(reservations(observer)==before && !kaspace_mmap(observer)->bytes && !file.references);
    kaspace_put(observer);
    CHECK(backend.destroyed==1 && allocations==frees);
    puts("Shared-root final PROCESS mapping retirement: PASS (production policy, hosted backend)");
}

/* Host-only root installation adapter: memberships and dependency preparation
 * use the production owners. Native tests additionally use the real factory,
 * scheduler, startup publication and CPU launcher. */
static KPROC *join(PD *pd,KPROC *source,KASPACE *space,BOOL scoped)
{
    KASPACE_MEMBER *members=NULL;
    KPROC *p;
    CHECK(kproc_create(pd));p=kproc_find(pd);CHECK(p);
    CHECK((scoped?kproc_prepare_scoped_mapping_owners(p,source,space,&members):
                  kproc_clone_prepare_mapping_owners(p,source,space,FALSE,&members))==E_OK);
    kproc_clone_commit_mapping_owners(p,space,&members);
    p->active_space=kaspace_share(space);CHECK(p->active_space);
    p->aspace=kaspace_backend(space);
    return p;
}
static void dispose(PD *pd)
{ CHECK(kproc_begin_exit(pd));kproc_complete(pd,0);kproc_destroy(pd);CHECK(!kproc_find(pd)); }

static void common_cohort(void)
{
    ASPACE backend={0};
    KASPACE *space=kaspace_adopt(&backend);
    PD parent={0},peer={0};
    KPROC *source;
    LONG heap,mapping,late;
    CHECK(space && kproc_create(&parent) && kproc_test_share_active(&parent,space));
    source=kproc_find(&parent);
    heap=kproc_uheap_alloc(&parent,3*USER_PAGE_SIZE);CHECK(heap>0);
    mapping=kproc_mmap(&parent,0,3*USER_PAGE_SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE,0,0);CHECK(mapping>0);
    join(&peer,source,space,FALSE);
    late=kproc_mmap(&peer,0,USER_PAGE_SIZE,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,0,0);CHECK(late>0);
    CHECK(reservations(space)==3 && file.references==1);
    dispose(&parent);
    CHECK(reservations(space)==3 && file.references==1 && !backend.destroyed);
    CHECK(kaspace_mmap_demand_page(space,mapping,0)==1); /* untouched file, creator gone */
    CHECK(kaspace_mmap_demand_page(space,mapping,ASPACE_PROT_WRITE)==1); /* clean -> private COW */
    CHECK(kaspace_heap_demand_page(space,heap,ASPACE_PROT_WRITE)==1);
    CHECK(kproc_mprot(&peer,late,USER_PAGE_SIZE,PROT_READ|PROT_WRITE)==E_OK);
    CHECK(kaspace_mmap_demand_page(space,late,ASPACE_PROT_WRITE)==1);
    CHECK(kproc_uheap_shrink(&peer,heap,USER_PAGE_SIZE)==E_OK);
    dispose(&peer);
    CHECK(!reservations(space) && !file.references && !backend.pages && !backend.destroyed);
    kaspace_put(space);CHECK(backend.destroyed && allocations==frees);
    puts("Shared-VM cohort: inherited+late heap/file/PROT_NONE, creator-first exit, lazy fault and COW: PASS");
}

static void scoped_peer(void)
{
    ASPACE backend={0};
    KASPACE *space=kaspace_adopt(&backend);
    PD parent={0},owner={0};
    KPROC *source,*child_owner;
    KASPACE_RESERVATION_INFO info;
    KASPACE_ATTACHMENT middle={0},heap_claim={0},guard_claim={0},spent,bad={0};
    LONG mapping,heap,anonymous,guard,reused;
    unsigned i,base_allocs,before;
    CHECK(space && kproc_create(&parent) && kproc_test_share_active(&parent,space));
    source=kproc_find(&parent);child_owner=join(&owner,source,space,TRUE);
    mapping=kproc_mmap(&owner,0,3*USER_PAGE_SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE,0,0);
    heap=kproc_uheap_alloc(&owner,2*USER_PAGE_SIZE);
    anonymous=kproc_mmap(&owner,0,USER_PAGE_SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,0,0);
    guard=kproc_mmap(&owner,0,USER_PAGE_SIZE,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,0,0);
    CHECK(mapping>0 && heap>0 && anonymous>0 && guard>0);
    CHECK(kaspace_reservation_info(space,mapping,3*USER_PAGE_SIZE,&info));
    CHECK(kproc_munmap(&parent,mapping,USER_PAGE_SIZE)==EACCDN);
    CHECK(kproc_mprot(&parent,mapping,USER_PAGE_SIZE,PROT_READ)==EACCDN);
    base_allocs=allocations-frees;before=reservations(space);
    for(i=1;i<=3;i++) {
        fail_allocation=i;
        CHECK(kproc_mapping_attach(child_owner,source,info.identity,mapping+USER_PAGE_SIZE,USER_PAGE_SIZE,&bad)==ENSMEM);
        CHECK(!bad.ticket && reservations(space)==before && file.references==1 && allocations-frees==base_allocs);
    }
    for(i=1;i<=2;i++) {
        fail_file_hold=i;
        CHECK(kproc_mapping_attach(child_owner,source,info.identity,mapping+USER_PAGE_SIZE,USER_PAGE_SIZE,&bad)==ENSMEM);
        CHECK(!bad.ticket && reservations(space)==before && file.references==1 && allocations-frees==base_allocs);
    }
    CHECK(kproc_mapping_attach(child_owner,source,info.identity,mapping+USER_PAGE_SIZE,USER_PAGE_SIZE,&middle)==E_OK);
    spent=middle;
    CHECK(kaspace_reservation_info(space,heap,2*USER_PAGE_SIZE,&info));
    CHECK(kproc_mapping_attach(child_owner,source,info.identity,heap,2*USER_PAGE_SIZE,&heap_claim)==E_OK);
    CHECK(kaspace_reservation_info(space,guard,USER_PAGE_SIZE,&info) && !info.rights);
    CHECK(kproc_mapping_attach(child_owner,source,info.identity,guard,USER_PAGE_SIZE,&guard_claim)==E_OK);
    dispose(&owner);
    CHECK(reservations(space)==3 && kaspace_mmap(space)->bytes==2*USER_PAGE_SIZE && file.references==1);
    CHECK(kaspace_mmap(space)->regions->offset==USER_PAGE_SIZE);
    CHECK(kaspace_mmap_demand_page(space,mapping+USER_PAGE_SIZE,0)==1);
    CHECK(kaspace_mmap_demand_page(space,mapping+USER_PAGE_SIZE,ASPACE_PROT_WRITE)==1);
    CHECK(kaspace_heap_demand_page(space,heap,ASPACE_PROT_WRITE)==1);
    CHECK(kproc_mprot(&parent,guard,USER_PAGE_SIZE,PROT_READ|PROT_WRITE)==E_OK);
    CHECK(kaspace_mmap_demand_page(space,guard,ASPACE_PROT_WRITE)==1);
    CHECK(!kaspace_attachment_release(&middle,source->number+1,source) && middle.ticket==spent.ticket);
    CHECK(kproc_mapping_detach(source,&middle) && !middle.ticket);
    CHECK(!kproc_mapping_detach(source,&spent) && !file.references);
    reused=kproc_mmap(&parent,mapping,USER_PAGE_SIZE,PROT_READ|PROT_WRITE,
                      MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,0,0);
    CHECK(reused==mapping && !kproc_mapping_detach(source,&spent));
    CHECK(kproc_mapping_detach(source,&heap_claim) && kproc_mapping_detach(source,&guard_claim));
    CHECK(reservations(space)==1 && kaspace_mmap(space)->bytes==USER_PAGE_SIZE);
    dispose(&parent);
    CHECK(!reservations(space) && !backend.pages && !file.references);
    kaspace_put(space);CHECK(backend.destroyed && allocations==frees);
    puts("Scoped peer: exact subrange/file/heap/PROT_NONE claims, split rollback, creator-first, spent birth and reuse: PASS");
}

static void deferred_exact_hold(void)
{
    ASPACE backend={0};
    KASPACE *space=kaspace_adopt(&backend);
    KASPACE_RESERVATION_INFO info;
    KASPACE_RESERVATION_HOLD hold={0},copy={0},bad={0};
    KASPACE_RESERVATION_HOLD unrelated={0};
    KASPACE_RESERVATION_INFO captured;
    int canonical;
    void *released=NULL;
    UQUAD file_birth;
    KASPACE_LEASE *stack=NULL;
    PD owner={0};
    LONG mapping;
    CHECK(space && kproc_create(&owner) && kproc_test_share_active(&owner,space));
    mapping=kproc_mmap(&owner,0,3*USER_PAGE_SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE,0,0);
    CHECK(mapping>0 && kaspace_reservation_info(space,mapping,3*USER_PAGE_SIZE,&info));
    file_birth=info.identity;
    CHECK(kproc_reservation_hold(&owner,mapping+USER_PAGE_SIZE,USER_PAGE_SIZE,info.identity,ASPACE_PROT_USER,&hold)==E_OK);
    captured.address=mapping;captured.bytes=USER_PAGE_SIZE;captured.identity=kaspace_reservation_next();
    captured.rights=ASPACE_PROT_USER|ASPACE_PROT_WRITE;captured.flags=KASPACE_RESERVATION_LAZY;
    CHECK(kaspace_reservation_hold_captured(space,&captured,mapping,USER_PAGE_SIZE,captured.rights,
                                           7,&canonical,&unrelated)==E_OK);
    dispose(&owner);
    CHECK(reservations(space)==1 && file.references==1);
    CHECK(kaspace_reservation_info(space,mapping,USER_PAGE_SIZE,&info) &&
          (info.flags&KASPACE_RESERVATION_CAPTURED) && info.identity==captured.identity);
    CHECK(!kaspace_reservation_info(space,mapping+USER_PAGE_SIZE,USER_PAGE_SIZE,&info));
    CHECK(kaspace_reservation_hold(space,file_birth,mapping,USER_PAGE_SIZE,ASPACE_PROT_USER,1,&owner,&bad)==EACCDN && !bad.ticket);
    CHECK(kaspace_stack_hold_acquire(space,mapping+USER_PAGE_SIZE,USER_PAGE_SIZE,&stack)==EACCDN && !stack);
    CHECK(kaspace_mmap_demand_page(space,mapping,0)==0);
    CHECK(!kaspace_materialization_change(&backend,mapping,USER_PAGE_SIZE,ASPACE_PROT_USER|ASPACE_PROT_WRITE));
    CHECK(kaspace_mmap_demand_page(space,mapping+USER_PAGE_SIZE,0)==1);
    CHECK(kproc_reservation_duplicate(&hold,&copy)==E_OK);
    CHECK(kaspace_reservation_release(&unrelated,&released) && released==&canonical);
    CHECK(kproc_reservation_release(&hold) && reservations(space)==1);
    CHECK(kproc_reservation_release(&copy) && !reservations(space) && !file.references && !backend.pages);
    CHECK(!kproc_reservation_release(&copy));
    kaspace_put(space);CHECK(backend.destroyed && allocations==frees);
    puts("Pending canonical metadata: exact retained file fault, no new/widened authority, duplicate existing hold, final cleanup: PASS");
}

static void pending_predecessor(BOOL heap_case,BOOL shrink)
{
    ASPACE backend={0};
    KASPACE *space=kaspace_adopt(&backend);
    PD parent={0},owner={0};
    KPROC *source;
    KASPACE_RESERVATION_INFO info;
    KASPACE_RESERVATION_HOLD hold={0};
    LONG first,second;
    CHECK(space && kproc_create(&parent) && kproc_test_share_active(&parent,space));
    source=kproc_find(&parent);join(&owner,source,space,TRUE);
    first=heap_case?(LONG)kproc_uheap_alloc(&owner,USER_PAGE_SIZE):
        kproc_mmap(&owner,0,USER_PAGE_SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE,0,0);
    second=heap_case?(LONG)kproc_uheap_alloc(&parent,3*USER_PAGE_SIZE):
        kproc_mmap(&parent,0,USER_PAGE_SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,0,0);
    CHECK(first>0 && second>first && kaspace_reservation_info(space,first,USER_PAGE_SIZE,&info));
    CHECK(kproc_reservation_hold(&owner,first,USER_PAGE_SIZE,info.identity,ASPACE_PROT_USER,&hold)==E_OK);
    dispose(&owner);CHECK(reservations(space)==2);
    release_on_lazy=&hold;mutating_pd=&parent;
    CHECK((heap_case?(shrink?kproc_uheap_shrink(&parent,second,USER_PAGE_SIZE):kproc_uheap_free(&parent,second)):
                     kproc_munmap(&parent,second,USER_PAGE_SIZE))==E_OK);
    CHECK(!release_on_lazy && !hold.ticket && !file.references);
    if(shrink) {
        CHECK(reservations(space)==1 && kaspace_heap(space)->blocks[0].va==(VADDR)second &&
              kaspace_heap(space)->blocks[0].pages==1);
        CHECK(kproc_uheap_free(&parent,second)==E_OK);
    }
    CHECK(!reservations(space));dispose(&parent);kaspace_put(space);
    CHECK(backend.destroyed && allocations==frees);
}

static void prepared_target_exclusion(void)
{
    ASPACE backend={0};
    KASPACE *space=kaspace_adopt(&backend);
    PD parent={0},candidate={0};
    KPROC *source,*target;
    KASPACE_MEMBER *first=NULL,*second=NULL;
    LONG mapping;
    unsigned owned;
    CHECK(space && kproc_create(&parent) && kproc_test_share_active(&parent,space));
    source=kproc_find(&parent);
    mapping=kproc_mmap(&parent,0,USER_PAGE_SIZE,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,0,0);CHECK(mapping>0);
    CHECK(kproc_create(&candidate));target=kproc_find(&candidate);
    CHECK(kproc_prepare_scoped_mapping_owners(target,source,space,&first)==E_OK && first);
    owned=allocations-frees;
    CHECK(kproc_prepare_scoped_mapping_owners(target,source,space,&second)==EACCDN && !second);
    CHECK(kproc_clone_prepare_mapping_owners(target,source,space,FALSE,&second)==EACCDN && !second);
    CHECK(allocations-frees==owned && reservations(space)==1);
    kproc_abort_mapping_owners(target,space,&first);CHECK(!first && !target->mapping_preparation);
    CHECK(kproc_prepare_scoped_mapping_owners(target,source,space,&second)==E_OK && second);
    kproc_clone_commit_mapping_owners(target,space,&second);CHECK(!second && !target->mapping_preparation);
    target->active_space=kaspace_share(space);target->aspace=kaspace_backend(space);
    dispose(&parent);CHECK(reservations(space)==1);
    dispose(&candidate);CHECK(!reservations(space));kaspace_put(space);
    CHECK(backend.destroyed && allocations==frees);
    puts("Prepared target: one constructor, cross-policy exclusion, abort/retry and one commit preserve exact dependencies: PASS");
}

static void attachment_r2_reentry(void)
{
    ASPACE backend={0};
    KASPACE *space=kaspace_adopt(&backend);
    PD parent={0},owner={0};
    KPROC *source,*child_owner;
    KASPACE_RESERVATION_INFO info;
    KASPACE_RESERVATION_HOLD before={0},after={0},duplicate={0};
    KASPACE_ATTACHMENT middle={0};
    LONG mapping;
    CHECK(space && kproc_create(&parent) && kproc_test_share_active(&parent,space));
    source=kproc_find(&parent);child_owner=join(&owner,source,space,TRUE);
    mapping=kproc_mmap(&owner,0,3*USER_PAGE_SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE,0,0);
    CHECK(mapping>0 && kaspace_reservation_info(space,mapping,3*USER_PAGE_SIZE,&info));
    CHECK(kproc_reservation_hold(&owner,mapping,3*USER_PAGE_SIZE,info.identity,ASPACE_PROT_USER,&before)==E_OK);
    CHECK(kproc_mapping_attach(child_owner,source,info.identity,mapping+USER_PAGE_SIZE,USER_PAGE_SIZE,&middle)==E_OK);
    CHECK(kaspace_lease_valid(reservation_find(&before)));
    CHECK(kproc_reservation_duplicate(&before,&duplicate)==E_OK && kproc_reservation_release(&duplicate));
    CHECK(kproc_reservation_hold(&owner,mapping,3*USER_PAGE_SIZE,info.identity,ASPACE_PROT_USER,&after)==E_OK);
    dispose(&owner);
    CHECK(reservations(space)==3 && file.references==3);
    CHECK(kaspace_mmap_demand_page(space,mapping,0)==1 &&
          kaspace_mmap_demand_page(space,mapping+2*USER_PAGE_SIZE,0)==1);
    CHECK(kproc_reservation_release(&after));
    release_on_file_put=&before;
    CHECK(kproc_mapping_detach(source,&middle) && !release_on_file_put && !before.ticket);
    CHECK(!reservations(space) && !file.references && !backend.pages);
    dispose(&parent);kaspace_put(space);CHECK(backend.destroyed && allocations==frees);
    puts("R2 before/after attachment splits: same birth/rights, wide validation, reentrant coalescing/final retirement: PASS");
}

static void deferred_stack_hold(void)
{
    ASPACE backend={0};
    KASPACE *space=kaspace_adopt(&backend);
    PD owner={0};
    KASPACE_LEASE *stack=NULL,*new_alias=NULL;
    LONG mapping;
    CHECK(space && kproc_create(&owner) && kproc_test_share_active(&owner,space));
    mapping=kproc_mmap(&owner,0,2*USER_PAGE_SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,0,0);
    CHECK(mapping>0 && kaspace_mmap_demand_page(space,mapping,ASPACE_PROT_WRITE)==1);
    CHECK(kaspace_stack_hold_acquire(space,mapping,USER_PAGE_SIZE,&stack)==E_OK);
    dispose(&owner);
    CHECK(reservations(space)==1 && backend.pages==1 && kaspace_lease_valid(stack));
    CHECK(kaspace_lease_acquire(space,mapping,USER_PAGE_SIZE,ASPACE_PROT_USER,&new_alias)==EACCDN && !new_alias);
    CHECK(kaspace_mmap_demand_page(space,mapping+USER_PAGE_SIZE,ASPACE_PROT_WRITE)==0);
    kaspace_lease_release(&stack);
    CHECK(!reservations(space) && !backend.pages);
    kaspace_put(space);CHECK(backend.destroyed && allocations==frees);
    puts("Stopped stack hold: pending canonical allocation, no fresh alias/fault expansion, post-hold reclamation: PASS");
}

int main(void)
{
    original_control();common_cohort();scoped_peer();deferred_exact_hold();
    attachment_r2_reentry();deferred_stack_hold();
    pending_predecessor(FALSE,FALSE);pending_predecessor(TRUE,FALSE);pending_predecessor(TRUE,TRUE);
    puts("Reentrant predecessor retirement: Munmap/Mfree/Mshrink cursors survive, recursive mutation rejected: PASS");
    prepared_target_exclusion();
    CHECK(!kproc_count() && allocations==frees);
    return 0;
}

PADDR procmem_phys_of(VADDR va) { return (PADDR)va; }
const void *procmem_owner(const void *base) { (void)base;return NULL; }
