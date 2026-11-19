/* Production KASPACE private-capture ownership. RAM/MMU/file providers are
 * counted host models; this is not native CPU or hardware COW evidence. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "../bdos/kaspace.c"
#include "../bdos/thread_return.c"

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"private snapshot:%d: %s\n",__LINE__,#c); abort(); } } while (0)
struct model_page { VADDR address;UWORD rights;BOOL file; };
struct aspace { unsigned count;BOOL owned;struct model_page page[32]; };
struct file_backing { unsigned references; };
static unsigned allocations,frees,fail_allocation,destroyed,retirements,backend_clones;
static struct file_backing backing;
static BOOL fail_file;
static BOOL poison_allocations;
static KASPACE_MEMBER *callback_member;
static VADDR deferred_borrow;
static unsigned borrow_releases;

void *kalloc(ULONG bytes)
{
    void *p;
    if (fail_allocation && !--fail_allocation) return NULL;
    p=poison_allocations?malloc(bytes):calloc(1,bytes);
    if(p) { if(poison_allocations) memset(p,0xa5,bytes);allocations++; }
    return p;
}
void kfree(void *p) { if(p) { frees++;free(p); } }
void panic(const char *format,...) { fprintf(stderr,"panic: %s\n",format);abort(); }
static struct model_page *page_find(ASPACE *as,VADDR address)
{
    unsigned i;address&=~(VADDR)(USER_PAGE_SIZE-1);
    for(i=0;i<as->count;i++) if(as->page[i].address==address) return &as->page[i];
    return NULL;
}
BOOL aspace_translate(const ASPACE *as,VADDR address,PADDR *physical,UWORD *rights)
{
    struct model_page *page=page_find((ASPACE *)as,address);
    if(!page) return FALSE;
    if(physical) *physical=address;
    if(rights) *rights=page->rights;
    return TRUE;
}
BOOL aspace_logical_protection(const ASPACE *as,VADDR address,UWORD *rights)
{ return aspace_translate(as,address,NULL,rights); }
BOOL aspace_private_range_ok(const ASPACE *as,VADDR address,VADDR bytes)
{ (void)bytes;return page_find((ASPACE *)as,address)!=NULL; }
BOOL aspace_load_private(ASPACE *as,VADDR address,VADDR bytes,UWORD rights,const void *code,ULONG size)
{
    struct model_page *page;
    CHECK(address==RETURN_VA && bytes==USER_PAGE_SIZE && size==sizeof(return_code));
    CHECK(!memcmp(code,return_code,size) && !page_find(as,address) && as->count<32);
    if(!kaspace_mapping_change(as,address,bytes)) return FALSE;
    page=&as->page[as->count++];page->address=address;page->rights=rights;page->file=FALSE;
    return TRUE;
}
BOOL aspace_copy_from_user(const ASPACE *as,void *target,VADDR address,ULONG bytes)
{
    if(address!=RETURN_VA || bytes!=sizeof(return_code) || !page_find((ASPACE *)as,address)) return FALSE;
    memcpy(target,return_code,bytes);return TRUE;
}
BOOL aspace_file_cow(const ASPACE *as,VADDR address)
{ struct model_page *page=page_find((ASPACE *)as,address);return page && page->file; }
BOOL aspace_fault_in(ASPACE *as,VADDR address,UWORD rights)
{
    struct model_page *page;
    address&=~(VADDR)(USER_PAGE_SIZE-1);
    if(!kaspace_materialization_change(as,address,USER_PAGE_SIZE,rights)) return FALSE;
    page=page_find(as,address);
    if(!page) { CHECK(as->count<32);page=&as->page[as->count++]; }
    page->address=address;page->rights=rights;page->file=FALSE;
    return TRUE;
}
int aspace_cow_fault(ASPACE *as,VADDR address,UWORD access)
{ (void)as;(void)address;(void)access;return -1; }
LONG aspace_clone(ASPACE *source,ASPACE **result)
{
    ASPACE *copy;
    CHECK(!*result);
    copy=kalloc(sizeof(*copy));if(!copy) return ENSMEM;
    *copy=*source;copy->owned=TRUE;*result=copy;backend_clones++;
    return E_OK;
}
void aspace_destroy(ASPACE *as)
{ destroyed++;if(as->owned) kfree(as); }
static void release_pages(ASPACE *as,VADDR address,VADDR bytes)
{
    unsigned i=0;
    while(i<as->count) {
        if(as->page[i].address>=address && as->page[i].address-address<bytes)
            as->page[i]=as->page[--as->count];
        else i++;
    }
}
BOOL aspace_unmap_private(ASPACE *as,VADDR address,VADDR bytes)
{
    if(!kaspace_mapping_change(as,address,bytes)) return FALSE;
    release_pages(as,address,bytes);return TRUE;
}
BOOL aspace_release_lazy_retired_allowed(const ASPACE *as,VADDR address,VADDR bytes)
{ (void)as;(void)address;(void)bytes;return TRUE; }
BOOL aspace_release_lazy_retired(ASPACE *as,VADDR address,VADDR bytes)
{
    CHECK(kaspace_retirement_change(as,address,bytes,TRUE));
    CHECK(!kaspace_retirement_change(as,address,bytes,TRUE));
    if(callback_member) kaspace_member_release(&callback_member);
    release_pages(as,address,bytes);retirements++;return TRUE;
}
BOOL aspace_release_lazy(ASPACE *as,VADDR address,VADDR bytes)
{ CHECK(kaspace_mapping_change(as,address,bytes));release_pages(as,address,bytes);return TRUE; }
void aspace_unborrow(ASPACE *as,VADDR address,VADDR bytes)
{ (void)as;(void)address;(void)bytes;abort(); }
BOOL aspace_defer_unborrow(ASPACE *as,VADDR address,VADDR bytes)
{ (void)as;CHECK(address==0x91001 && bytes==8 && !deferred_borrow);deferred_borrow=address;return TRUE; }
void aspace_unborrow_retired(ASPACE *as,VADDR address,VADDR bytes)
{
    CHECK(deferred_borrow==0x91001 && address==0x91000 && bytes==4096);
    CHECK(kaspace_retirement_change(as,address,bytes,FALSE));
    deferred_borrow=0;borrow_releases++;
}
BOOL fileback_try_hold(FILE_BACKING *file)
{ CHECK(file==&backing && backing.references);if(fail_file) return FALSE;backing.references++;return TRUE; }
void fileback_put(FILE_BACKING *file)
{ if(file) { CHECK(file==&backing && backing.references);backing.references--; } }
int aspace_fault_file(ASPACE *as,VADDR address,FILE_BACKING *file,ULONG offset,UWORD rights,BOOL write)
{ CHECK(file==&backing && backing.references && offset==0x4000);(void)write;return aspace_fault_in(as,address,rights)?1:0; }

static void release_hold(KASPACE_RESERVATION_HOLD *hold,void *expected)
{ void *owner=NULL;CHECK(kaspace_reservation_release(hold,&owner) && owner==expected); }
static unsigned snapshot_count(const KASPACE *space)
{ unsigned count=0;KASPACE_LEASE *lease;for(lease=space->leases;lease;lease=lease->next) if(lease->private_snapshot) count++;return count; }
static void captured_only(void)
{
    ASPACE source_backend={0};
    KASPACE *source=kaspace_adopt(&source_backend),*child=NULL,*retry=NULL;
    KASPACE_MEMBER *member=NULL,*peer=NULL;
    KASPACE_RESERVATION_INFO info,description={0x20000,12288,0,ASPACE_PROT_USER|ASPACE_PROT_WRITE,KASPACE_RESERVATION_LAZY};
    KASPACE_RESERVATION_HOLD source_hold={0},read_only={0},none={0},child_hold={0};
    KASPACE_LEASE *stack=NULL;
    int canonical,peer_owner;
    unsigned before,i,prior_destroyed=destroyed,clones=backend_clones;
    UQUAD source_birth;
    UWORD rights;
    CHECK(source);
    source_birth=description.identity=kaspace_reservation_next();
    CHECK(kaspace_reservation_hold_captured(source,&description,0x21008,8,description.rights,7,&canonical,&source_hold)==E_OK);
    description.address=0x30000;description.bytes=4096;description.identity=kaspace_reservation_next();description.rights=ASPACE_PROT_USER;
    CHECK(kaspace_reservation_hold_captured(source,&description,0x30008,8,description.rights,7,&canonical,&read_only)==E_OK);
    description.address=0x40000;description.identity=kaspace_reservation_next();description.rights=0;
    description.flags=KASPACE_RESERVATION_LAZY|KASPACE_RESERVATION_INACCESSIBLE;
    CHECK(kaspace_reservation_hold_captured(source,&description,0x40000,4096,0,7,&canonical,&none)==E_OK);
    /* Retiring the source expands only its destruction fence, not the exact
     * eight-byte authenticated lazy reservation copied into the child. */
    CHECK(kaspace_retire_private(source,0x20000,12288));
    before=allocations-frees;
    for(i=1;i<32;i++) {
        LONG error;
        fail_allocation=i;error=kaspace_clone(source,&child);fail_allocation=0;
        if(!error) break;
        CHECK(error==ENSMEM && !child && allocations-frees==before);
        CHECK(kaspace_retained_lazy_protection(source,0x21000,&rights) && rights==(ASPACE_PROT_USER|ASPACE_PROT_WRITE));
        CHECK(!kaspace_retained_lazy_protection(source,0x22000,&rights));
    }
    CHECK(child && i<32 && snapshot_count(child)==3 && backend_clones==clones+1);
    CHECK(kaspace_reservation_info(child,0x21008,8,&info) && info.identity!=source_birth && info.address==0x21008 && info.bytes==8);
    CHECK(!kaspace_reservation_info(child,0x21010,1,&info));
    CHECK(!kaspace_retained_lazy_protection(child,0x20000,&rights) && !kaspace_retained_lazy_protection(child,0x22000,&rights));
    CHECK(kaspace_member_new(child,9,&canonical,&member)==E_OK);kaspace_member_claim_private(member);
    CHECK(kaspace_member_inherit(member,10,&peer_owner,&peer)==E_OK);
    CHECK(kaspace_retire_private(child,0x21000,4096));
    CHECK(kaspace_clone(child,&retry)==E_OK && snapshot_count(retry)==3);
    kaspace_put(retry);retry=NULL; /* unpublished rollback has no root cycle */
    release_hold(&source_hold,&canonical);release_hold(&read_only,&canonical);release_hold(&none,&canonical);
    kaspace_put(source);source=NULL;
    CHECK(destroyed==prior_destroyed+2); /* source and unpublished retry */
    CHECK(kaspace_retained_lazy_protection(child,0x21000,&rights) && rights==(ASPACE_PROT_USER|ASPACE_PROT_WRITE));
    CHECK(aspace_fault_in(kaspace_backend(child),0x21000,rights));
    CHECK(kaspace_retained_lazy_protection(child,0x30000,&rights) && rights==ASPACE_PROT_USER);
    CHECK(!aspace_fault_in(kaspace_backend(child),0x30000,ASPACE_PROT_USER|ASPACE_PROT_WRITE));
    CHECK(aspace_fault_in(kaspace_backend(child),0x30000,ASPACE_PROT_USER));
    CHECK(!kaspace_retained_lazy_protection(child,0x40000,&rights));
    CHECK(!aspace_fault_in(kaspace_backend(child),0x40000,ASPACE_PROT_USER));
    CHECK(kaspace_reservation_info(child,0x21008,8,&info));
    CHECK(kaspace_reservation_hold(child,info.identity,0x21008,8,info.rights,9,&canonical,&child_hold)==E_OK);
    CHECK(kaspace_lease_acquire(child,0x21000,4096,ASPACE_PROT_USER|ASPACE_PROT_WRITE,&stack)==E_OK);
    CHECK(kaspace_lease_hold_private(stack));
    kaspace_member_release(&member);
    CHECK(kaspace_retained_lazy_protection(child,0x21000,&rights));
    CHECK(kaspace_mutation_begin(child));kaspace_member_release(&peer);
    /* Final claim loss revokes canonical authority even during deferred reaping.
     * Only the independently held exact R2 witness remains materializable. */
    CHECK(kaspace_retained_lazy_protection(child,0x21000,&rights));
    CHECK(!kaspace_retained_lazy_protection(child,0x30000,&rights));
    kaspace_mutation_end(child);
    release_hold(&child_hold,&canonical);
    CHECK(!kaspace_retained_lazy_protection(child,0x21000,&rights));
    CHECK(snapshot_count(child)==1); /* stopped stack still protects this range */
    kaspace_lease_release(&stack);
    CHECK(!child->leases && !child->scopes && !kaspace_backend(child)->count);
    CHECK(child->refs==1 && destroyed==prior_destroyed+2); /* observer root survives */
    kaspace_put(child);
    CHECK(destroyed==prior_destroyed+3);
}

static void file_and_ordinary(void)
{
    ASPACE source_backend={0};
    KASPACE *source=kaspace_adopt(&source_backend),*child=NULL;
    KASPACE_MEMBER *source_member=NULL,*child_member=NULL;
    KASPACE_RESERVATION_INFO info;
    KASPACE_RESERVATION_HOLD witness={0};
    MMAP_REGION *region;
    int canonical;
    unsigned before;
    CHECK(source && kaspace_member_new(source,7,&canonical,&source_member)==E_OK);
    region=kalloc(sizeof(*region));CHECK(region);
    region->address=0x60000;region->length=4096;region->offset=0x4000;
    region->prot=ASPACE_PROT_USER|ASPACE_PROT_WRITE;region->reservation=kaspace_reservation_next();
    region->scope=kaspace_member_scope(source_member);region->backing=&backing;
    source->mmap.regions=region;source->mmap.bytes=4096;backing.references=1;
    CHECK(kaspace_reservation_info(source,0x60000,4096,&info));
    CHECK(kaspace_reservation_hold_captured(source,&info,0x60000,4096,info.rights,7,&canonical,&witness)==E_OK);
    before=allocations-frees;fail_file=TRUE;
    CHECK(kaspace_clone(source,&child)==EAGAIN && !child && backing.references==1 && allocations-frees==before);
    fail_file=FALSE;
    CHECK(kaspace_clone(source,&child)==E_OK && backing.references==2 && !snapshot_count(child));
    CHECK(kaspace_member_new(child,8,&canonical,&child_member)==E_OK);kaspace_member_claim_private(child_member);
    release_hold(&witness,&canonical);kaspace_member_release(&source_member);kaspace_put(source);
    CHECK(backing.references==1);
    CHECK(kaspace_mmap_demand_page(child,0x60000,ASPACE_PROT_WRITE)==1);
    kaspace_member_release(&child_member);
    CHECK(!backing.references && !child->mmap.regions && !child->scopes && !kaspace_backend(child)->count && child->refs==1);
    kaspace_put(child);
}

static void reentrant_final_loss(void)
{
    ASPACE backend={0};
    KASPACE *source=kaspace_adopt(&backend),*child=NULL;
    KASPACE_MEMBER *member=NULL,*other=NULL;
    KASPACE_RESERVATION_INFO description={0x70000,4096,0,ASPACE_PROT_USER|ASPACE_PROT_WRITE,KASPACE_RESERVATION_LAZY};
    KASPACE_RESERVATION_HOLD hold={0};
    MMAP_REGION *region;
    int owner1,owner2;
    description.identity=kaspace_reservation_next();
    CHECK(kaspace_reservation_hold_captured(source,&description,0x70000,4096,description.rights,101,&owner1,&hold)==E_OK);
    CHECK(kaspace_clone(source,&child)==E_OK);
    CHECK(kaspace_member_new(child,102,&owner1,&member)==E_OK);kaspace_member_claim_private(member);
    CHECK(kaspace_member_new(child,103,&owner2,&other)==E_OK);
    region=kalloc(sizeof(*region));CHECK(region);
    region->address=0x80000;region->length=4096;region->prot=ASPACE_PROT_USER|ASPACE_PROT_WRITE;
    region->reservation=kaspace_reservation_next();region->scope=kaspace_member_scope(other);
    child->mmap.regions=region;child->mmap.bytes=4096;
    release_hold(&hold,&owner1);kaspace_put(source);
    callback_member=member;member=NULL;
    kaspace_member_release(&other);
    CHECK(!callback_member && child->refs==1 && !child->leases && !child->scopes);
    kaspace_put(child);
}

static void borrowed_fence(void)
{
    ASPACE backend={0};
    KASPACE *source=kaspace_adopt(&backend),*child=NULL;
    KASPACE_MEMBER *member=NULL;
    KASPACE_RESERVATION_INFO description={0x90000,12288,0,ASPACE_PROT_USER|ASPACE_PROT_WRITE,KASPACE_RESERVATION_LAZY};
    KASPACE_RESERVATION_HOLD hold={0};
    VADDR page;
    int owner;
    description.identity=kaspace_reservation_next();
    CHECK(kaspace_reservation_hold_captured(source,&description,0x90000,12288,description.rights,201,&owner,&hold)==E_OK);
    CHECK(kaspace_clone(source,&child)==E_OK);
    CHECK(kaspace_member_new(child,202,&owner,&member)==E_OK);kaspace_member_claim_private(member);
    release_hold(&hold,&owner);kaspace_put(source);
    for(page=0x90000;page<0x93000;page+=4096)
        CHECK(aspace_fault_in(kaspace_backend(child),page,description.rights));
    CHECK(kaspace_retire_borrow(child,0x91001,8) && deferred_borrow);
    kaspace_member_release(&member);
    CHECK(!deferred_borrow && borrow_releases==1 && child->refs==1);
    CHECK(!child->leases && !child->scopes && !kaspace_backend(child)->count);
    kaspace_put(child);
}

static void exact_backing_and_rights(void)
{
    ASPACE backend={0};
    KASPACE *source=kaspace_adopt(&backend),*child=NULL;
    KASPACE_RESERVATION_INFO description={0xa0000,4096,0,ASPACE_PROT_USER|ASPACE_PROT_WRITE,0},info;
    KASPACE_RESERVATION_HOLD hold={0};
    unsigned before;
    int owner;
    description.identity=kaspace_reservation_next();
    backend.page[0].address=description.address;backend.page[0].rights=description.rights;
    backend.page[0].file=TRUE;backend.count=1;
    CHECK(kaspace_reservation_hold_captured(source,&description,description.address,description.bytes,description.rights,301,&owner,&hold)==E_OK);
    before=allocations-frees;
    CHECK(kaspace_clone(source,&child)==EACCDN && !child && allocations-frees==before);
    /* Readable file-COW PTEs cannot manufacture a missing FILE_BACKING owner. */
    backend.page[0].file=FALSE;backend.page[0].rights=ASPACE_PROT_USER;
    CHECK(kaspace_clone(source,&child)==E_OK);
    /* The captured ceiling is immutable: a read-only resident PTE does not
     * narrow it (#1018 removed RELRO, the only rights tightening). */
    CHECK(kaspace_reservation_info(child,description.address,description.bytes,&info) && info.rights==description.rights);
    CHECK(child->leases->reservation_flags==(KASPACE_RESERVATION_CAPTURED|description.flags));
    kaspace_put(child);release_hold(&hold,&owner);kaspace_put(source);
}

static void cloned_return_bridge(void)
{
    ASPACE backend={0};
    KASPACE *source=kaspace_adopt(&backend),*child=NULL;
    THREAD_RETURN_BRIDGE *original=NULL;
    void *copied=NULL;
    unsigned before,step;
    LONG error=ENSMEM;
    CHECK(thread_return_bridge_acquire(source,&original)==E_OK);
    CHECK(kaspace_clone(source,&child)==E_OK);
    before=allocations-frees;poison_allocations=TRUE;
    for(step=1;step<8;step++) {
        fail_allocation=step;
        error=thread_startup_arch_bridge_acquire_cloned(source,child,&copied);
        fail_allocation=0;
        if(!error) break;
        CHECK(error==ENSMEM && !copied && allocations-frees==before);
    }
    poison_allocations=FALSE;
    CHECK(!error && copied && step<8 && thread_startup_arch_bridge_valid(copied));
    CHECK(thread_return_bridge_matches(copied,kaspace_backend(child)));
    thread_startup_arch_bridge_release(&copied);kaspace_put(child);
    CHECK(thread_return_bridge_valid(original));
    thread_return_bridge_release(&original);kaspace_put(source);
    CHECK(!bridges);
}

int main(void)
{
    captured_only();file_and_ordinary();reentrant_final_loss();borrowed_fence();exact_backing_and_rights();cloned_return_bridge();
    CHECK(allocations==frees && !spaces && retirements);
    puts("Private captured snapshots: exact lazy/rights/fences, source-first/nested lifetime, members, file authority, poisoned return bridge, allocation rollback and live-root final conservation: PASS (host MMU model)");
    return 0;
}
