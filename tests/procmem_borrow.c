/* Actual allocator + address-space + KASPACE ownership. Only physical RAM,
 * hardware tables/cache/TLB and allocation failure are hosted providers. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emutos.h"
#include "biosext.h"
#include "gemerror.h"
#if defined(BORROW_GENERIC)
#undef __x86_64__
#define pmem32_virt unused_native_alias
#include "pmem32.h"
#undef pmem32_virt
#define pmem32_virt(p) ((void *)(uintptr_t)(p))
#include "../bios/procmem.c"
#include "../bios/aspace.c"
#define __x86_64__ 1
#else
#include "pgtable.h"
#undef X86_64_PHYS_MAP_BASE
#define X86_64_PHYS_MAP_BASE 0
#include "../bios/machine/pc-x86_64/procmem.c"
#include "../bios/machine/pc-x86_64/aspace.c"
#endif
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"borrow:%d: %s\n",__LINE__,#c);abort(); } } while (0)
#define TEST_PAGE 4096
static UBYTE physical[96][TEST_PAGE] __attribute__((aligned(TEST_PAGE)));
static UBYTE window[WINDOW_PAGES][TEST_PAGE] __attribute__((aligned(TEST_PAGE)));
static BOOL physical_used[96];
static ULONG metadata_live, physical_live, allocation_call, fail_metadata;
static ULONG map_calls, fail_map, table_calls, fail_table, flushes;
static PADDR selected_root;
void *kalloc(ULONG size)
{
    void *p;
    if (++allocation_call==fail_metadata) return NULL;
    p=calloc(1,size);if (p) metadata_live++;return p;
}
void kfree(void *p) { if (p) { CHECK(metadata_live);metadata_live--;free(p); } }
void panic(const char *format,...) { fprintf(stderr,"unexpected panic: %s\n",format);abort(); }
static PADDR ram_alloc(ULONG pages)
{
    ULONG i,j;
    for (i=0;i+pages<=96;i++) {
        for (j=0;j<pages && !physical_used[i+j];j++) { }
        if (j==pages) {
            for (j=0;j<pages;j++) physical_used[i+j]=TRUE;
            physical_live+=pages;memset(physical[i],0,pages*TEST_PAGE);
            return (PADDR)(uintptr_t)physical[i];
        }
    }
#if defined(BORROW_GENERIC)
    return PMEM32_NONE;
#else
    return (PADDR)-1;
#endif
}
static void ram_free(PADDR p,ULONG pages)
{
    ULONG i=(p-(PADDR)(uintptr_t)physical)/TEST_PAGE,j;
    CHECK(i+pages<=96);
    for (j=0;j<pages;j++) { CHECK(physical_used[i+j]);physical_used[i+j]=FALSE; }
    physical_live-=pages;
}
static BOOL ram_owned(PADDR p)
{
    PADDR first=(PADDR)(uintptr_t)physical,backing=(PADDR)(uintptr_t)window;
    if(p>=backing && p-backing<sizeof(window)) return TRUE;
    return p>=first && p-first<sizeof(physical) && physical_used[(p-first)/TEST_PAGE];
}
#if defined(BORROW_GENERIC)
PADDR pmem32_alloc(ULONG pages,ULONG align)
{ (void)align;return pages==WINDOW_PAGES ? (PADDR)(uintptr_t)window : ram_alloc(pages); }
void pmem32_free(PADDR p,ULONG pages) { ram_free(p,pages); }
BOOL pmem32_is_allocated(PADDR p) { return ram_owned(p); }
struct modeled_leaf { PADDR physical; UWORD prot,flags; };
struct modeled_root { ULONG count,carve;struct { VADDR va;PADDR table; } groups[128]; };
ULONG aspace_hw_root_pages(void) { return 1; }
ULONG aspace_hw_root_align(void) { return 1; }
void aspace_hw_init_root(PADDR p) { (void)p; }
static struct modeled_leaf *model_leaf(PADDR p,VADDR va,ASPACE_TABLE_ALLOC alloc,void *ctx)
{
    struct modeled_root *r=(void *)(uintptr_t)p;
    ULONG i;VADDR group=va&~(VADDR)0xffff;
    for (i=0;i<r->count;i++) if (r->groups[i].va==group) break;
    if (i==r->count) {
        PADDR t;
        if (!alloc || ++table_calls==fail_table || (t=alloc(ctx))==PMEM32_NONE) return NULL;
        CHECK(i<128);r->groups[i].va=group;r->groups[i].table=t;r->count++;r->carve+=37;
    }
    return &((struct modeled_leaf *)(uintptr_t)r->groups[i].table)[(va>>12)&15];
}
BOOL aspace_hw_map(PADDR p,VADDR va,PADDR pa,UWORD prot,UWORD flags,ASPACE_TABLE_ALLOC alloc,void *ctx)
{
    struct modeled_leaf *l=model_leaf(p,va,alloc,ctx);
    if (++map_calls==fail_map || !l) return FALSE; /* may have allocated/linked a table */
    l->physical=pa;l->prot=prot;l->flags=flags|0x8000;return TRUE;
}
BOOL aspace_hw_lookup(PADDR p,VADDR va,PADDR *pa,UWORD *prot,UWORD *flags)
{
    struct modeled_leaf *l=model_leaf(p,va,NULL,NULL);
    if (!l || !(l->flags&0x8000)) return FALSE;
    *pa=l->physical;*prot=l->prot;*flags=l->flags&0x7fff;return TRUE;
}
BOOL aspace_hw_unmap(PADDR p,VADDR va)
{ struct modeled_leaf *l=model_leaf(p,va,NULL,NULL);if(!l)return FALSE;memset(l,0,sizeof(*l));return TRUE; }
BOOL aspace_hw_update(PADDR root,VADDR va,UWORD prot,UWORD flags)
{
    struct modeled_leaf *leaf=model_leaf(root,va,NULL,NULL);
    if(!leaf || !(leaf->flags&0x8000)) return FALSE;
    leaf->prot=prot;leaf->flags=flags|0x8000;return TRUE;
}
void aspace_hw_walk(PADDR p,void (*fn)(VADDR,PADDR,UWORD,void *),void *ctx)
{ (void)p;(void)fn;(void)ctx; } /* no owned leaves in this fixture */
PADDR aspace_hw_current(void) { return selected_root; }
void aspace_hw_switch(PADDR p) { selected_root=p; }
void aspace_hw_flush(void) { flushes++; }
void aspace_hw_code_changed(void) { flushes++; }
#else
UQUAD x86_64_pmem_try_alloc_pages(UQUAD pages,UQUAD flags) { (void)flags;return ram_alloc(pages); }
BOOL x86_64_pmem_free_pages(UQUAD p,UQUAD pages) { ram_free(p,pages);return TRUE; }
BOOL x86_64_pmem_is_allocated(UQUAD p) { return ram_owned(p); }
UQUAD x86_64_read_cr3(void) { return selected_root; }
void x86_64_write_cr3(UQUAD p) { selected_root=p;flushes++; }
UQUAD x86_64_kernel_pml4_phys(void) { return 0; }
void x86_64_new_address_space(UQUAD p) { memset((void *)(uintptr_t)p,0,TEST_PAGE); }
int x86_64_kernel_low_2m_phys(UQUAD va,UQUAD *p) { (void)va;(void)p;return 0; }
void x86_64_low_kernel_range(UQUAD *first,UQUAD *end) { *first=*end=0; }
int x86_64_map_kernel_2m_into(UQUAD root,UQUAD va,UQUAD p,UQUAD (*alloc)(void *),void *ctx)
{ (void)root;(void)va;(void)p;(void)alloc;(void)ctx;abort(); }
static UQUAD *model_pte(UQUAD root,UQUAD va,UQUAD (*alloc)(void *),void *ctx)
{
    UQUAD *t=(void *)(uintptr_t)root;int level;
    for (level=39;level>12;level-=9) {
        UQUAD *slot=t+((va>>level)&511),p;
        if (!(*slot&1)) {
            if (!alloc || ++table_calls==fail_table || (p=alloc(ctx))==X86_64_PGTABLE_NO_PAGE) return NULL;
            *slot=p|7;
        }
        t=(void *)(uintptr_t)(*slot&PTE_ADDR);
    }
    return t+((va>>12)&511);
}
int x86_64_map_user_page(UQUAD root,UQUAD va,UQUAD pa,int write,int exec,int user,UQUAD (*alloc)(void *),void *ctx)
{
    UQUAD *p=model_pte(root,va,alloc,ctx);
    if (++map_calls==fail_map || !p) return -1;
    *p=pa|1|(write?2:0)|(user?4:0)|(exec?0:(1ULL<<63));return 0;
}
UQUAD *x86_64_user_pte(UQUAD root,UQUAD va) { return model_pte(root,va,NULL,NULL); }
int x86_64_unmap_user_page(UQUAD root,UQUAD va)
{ UQUAD *p=model_pte(root,va,NULL,NULL);if(!p)return -1;*p=0;return 0; }
#endif
static ULONG pins(void *p) { struct alloc **a=find_link(p);CHECK(a);return (*a)->pins; }
static void reset_failure(void) { fail_metadata=fail_map=fail_table=0; }
static void failed_ownerless(void)
{
    ASPACE *as=aspace_create();
    void *p=procmem_alloc(3*TEST_PAGE,0);
    ULONG meta=metadata_live, tables=aspace_table_pages(as), ram=physical_live;
    BOOL mapped,freed;ULONG retained;
    CHECK(as && p && !procmem_owner(p));selected_root=aspace_root(as);
    fail_map=map_calls+1;
    mapped=aspace_map_procmem(as,(VADDR)(uintptr_t)p,3*TEST_PAGE,ASPACE_PROT_WRITE);
    retained=pins(p);freed=procmem_free(p);
    fprintf(stderr,"ownerless mapping failure: result=%d pins=%lu free=%d remaining=%lu\n",mapped,(unsigned long)retained,freed,(unsigned long)procmem_size(p));
    CHECK(!mapped && freed && !procmem_size(p)); /* the independent rejection */
    CHECK(as->npins==0 && aspace_table_pages(as)==tables && physical_live==ram && metadata_live==meta-1);
    reset_failure();aspace_destroy(as);
}
struct expected {
    ULONG metadata,ram,npins,pincap,count,capacity,live,free,bad;
    void *pages,*pin_vector;
    struct pin pin_records[16];
    struct alloc allocation,unrelated;
    UBYTE bytes[96][TEST_PAGE];
    BOOL used[96];
};
static struct expected before;
static void remember(ASPACE *as,void *p,void *q)
{
    before.metadata=metadata_live;before.ram=physical_live;
    before.npins=as->npins;before.pincap=as->pin_capacity;
    before.count=as->count;before.capacity=as->capacity;
    before.pages=as->pages;before.pin_vector=as->pins;
    CHECK(as->npins<16);
    if (as->npins) memcpy(before.pin_records,as->pins,as->npins*sizeof(*as->pins));
    before.allocation=**find_link(p);if(q) before.unrelated=**find_link(q);
    before.live=live_allocs;before.free=free_pages;before.bad=bad_frees;
    memcpy(before.bytes,physical,sizeof(physical));memcpy(before.used,physical_used,sizeof(physical_used));
}
static void unchanged(ASPACE *as,void *p,void *q)
{
    ULONG i;
    CHECK(metadata_live==before.metadata && physical_live==before.ram);
    CHECK(as->npins==before.npins && as->pin_capacity==before.pincap && as->pins==before.pin_vector);
    CHECK(as->count==before.count && as->capacity==before.capacity && as->pages==before.pages);
    CHECK(!as->npins || !memcmp(as->pins,before.pin_records,as->npins*sizeof(*as->pins)));
    CHECK(!memcmp(*find_link(p),&before.allocation,sizeof(before.allocation)));
    CHECK(!q || !memcmp(*find_link(q),&before.unrelated,sizeof(before.unrelated)));
    CHECK(live_allocs==before.live && free_pages==before.free && bad_frees==before.bad);
    CHECK(!memcmp(before.used,physical_used,sizeof(physical_used)));
    for(i=0;i<96;i++) if(before.used[i]) CHECK(!memcmp(before.bytes[i],physical[i],TEST_PAGE));
}
static void check_alias(ASPACE *as,VADDR va,PADDR expected,UWORD rights,ULONG value)
{
    PADDR pa;UWORD prot;ULONG bytes=0;
    CHECK(aspace_translate(as,va,&pa,&prot) && pa==expected && prot==rights);
    CHECK(aspace_copy_from_user(as,&bytes,va,sizeof(bytes)) && bytes==value);
}
static void overlapping_attempts(void)
{
    ASPACE *as=aspace_create();KASPACE *space=kaspace_adopt(as);
    KASPACE_LEASE *snapshot=NULL;
    void *p=procmem_alloc(3*TEST_PAGE,0),*q=procmem_alloc(TEST_PAGE,0);
    VADDR va=(VADDR)(uintptr_t)p;
    ULONG i,held;
    const UWORD old_rights=ASPACE_PROT_USER|ASPACE_PROT_EXEC;
    CHECK(as && space && p && q);selected_root=aspace_root(as);
    CHECK(procmem_set_owner(p,&map_calls) && procmem_set_owner(q,&flushes));
    for(i=0;i<3;i++) CHECK(aspace_map_procmem(as,va,3*TEST_PAGE,old_rights));
    CHECK(aspace_map_procmem(as,va+TEST_PAGE,13,old_rights));
    CHECK(pins(p)==4 && as->npins==4 && as->pin_capacity==4);
    /* Same VA, different physical alias/permissions: a failed retry must not
     * replace or unmap it, nor consume any older matching reference. */
    CHECK(aspace_map_page(as,va,procmem_phys_of((VADDR)(uintptr_t)q),old_rights));
    *(ULONG *)(uintptr_t)procmem_phys_of((VADDR)(uintptr_t)q)=0x952abc;
    CHECK(kaspace_lease_acquire(space,va,3*TEST_PAGE,ASPACE_PROT_USER,&snapshot)==E_OK);
    for(i=1;i<=3;i++) { /* pin vector, journal, staged table vector */
        remember(as,p,q);fail_metadata=allocation_call+i;
        CHECK(!aspace_borrow(as,va,3*TEST_PAGE));reset_failure();unchanged(as,p,q);
        CHECK(kaspace_lease_valid(snapshot));
    }
    held=pins(p);(*find_link(p))->pins=(ULONG)~0UL;
    remember(as,p,q);CHECK(!aspace_borrow(as,va,3*TEST_PAGE));unchanged(as,p,q);
    (*find_link(p))->pins=held;
    for(i=0;i<40;i++) {
        ULONG old_flush=flushes;
        remember(as,p,q);fail_map=map_calls+1+(i%3);
        CHECK(!aspace_borrow(as,va,3*TEST_PAGE));reset_failure();unchanged(as,p,q);
        CHECK(flushes>old_flush && kaspace_lease_valid(snapshot));
        check_alias(as,va,procmem_phys_of((VADDR)(uintptr_t)q),old_rights,0x952abc);
    }
    CHECK(aspace_map_procmem(as,va,13,ASPACE_PROT_USER|ASPACE_PROT_WRITE));
    CHECK(!kaspace_lease_valid(snapshot) && pins(p)==5);
    kaspace_lease_release(&snapshot);
    aspace_unborrow(as,va,13);
    CHECK(pins(p)==4 && aspace_translate(as,va,NULL,NULL));
    for(i=0;i<3;i++) aspace_unborrow(as,va,3*TEST_PAGE);
    CHECK(pins(p)==1 && !aspace_translate(as,va,NULL,NULL) && aspace_translate(as,va+TEST_PAGE,NULL,NULL));
    aspace_unborrow(as,va+TEST_PAGE,13);
    CHECK(!pins(p) && !as->npins && !aspace_translate(as,va,NULL,NULL));
    CHECK(procmem_free(p) && procmem_free(q));kaspace_put(space);
}
static void table_failures(void)
{
    unsigned n;
    for(n=1;n<=3;n++) {
        ASPACE *as=aspace_create();
        void *p=procmem_alloc(
            17*TEST_PAGE,
            0);
        VADDR va=(VADDR)(uintptr_t)p;
        CHECK(as && p);selected_root=aspace_root(as);
        remember(as,p,NULL);fail_table=table_calls+n;
        /* Generic provider has one table per 64 KiB; n=1/2 exercise first
         * and later-page failure, x32 n=2/3 leave intermediate table links. */
#if defined(BORROW_GENERIC)
        if(n==3) fail_map=map_calls+17;
#endif
        CHECK(!aspace_map_procmem(as,va,procmem_size(p),ASPACE_PROT_WRITE));
        reset_failure();unchanged(as,p,NULL);
        CHECK(procmem_free(p)); /* do not defer failed acquisition to root destruction */
        aspace_destroy(as);
    }
    {
        ASPACE *as=aspace_create();void *p=procmem_alloc(TEST_PAGE,0);
        CHECK(as && p);selected_root=aspace_root(as);
        as->capacity=as->count; /* valid full vector: next table must grow it */
        remember(as,p,NULL);
        fail_metadata=allocation_call+(as->capacity?4:3);
        CHECK(!aspace_map_procmem(as,(VADDR)(uintptr_t)p,TEST_PAGE,ASPACE_PROT_WRITE));
        reset_failure();unchanged(as,p,NULL);
        CHECK(procmem_free(p));aspace_destroy(as);
    }
}
static void successful_retirement(void)
{
    ASPACE *as=aspace_create();void *p=procmem_alloc(TEST_PAGE,0),*owner=NULL;
    int canonical;PROCMEM_HOLD hold={0};
    CHECK(as && p && procmem_set_owner(p,&canonical));selected_root=aspace_root(as);
    CHECK(procmem_hold_acquire(p,procmem_gen(p),&canonical,0,TEST_PAGE,ASPACE_PROT_USER,1,&canonical,&hold));
    CHECK(aspace_borrow(as,(VADDR)(uintptr_t)p,TEST_PAGE));
    CHECK(!procmem_free(p));procmem_free_owned(&canonical,NULL);
    CHECK(procmem_size(p) && pins(p)==1 && !procmem_owner(p));
    aspace_destroy(as);CHECK(procmem_size(p) && !pins(p));
    CHECK(procmem_hold_release(&hold,&owner) && owner==&canonical && !procmem_size(p));
}
#if defined(TEST_SHARED_PERMISSIONS)
static void shared_permission_transactions(void)
{
    ASPACE *as=aspace_create();KASPACE *space=kaspace_adopt(as);
    KASPACE_LEASE *old=NULL,*replacement=NULL;
    KASPACE_PERMISSION_TRANSACTION *tx=NULL;
    const VADDR va=0x2000000;
    KASPACE_PROTECTION change={0};
    PADDR physical=ram_alloc(1),actual;UWORD rights;ULONG count,i;
    CHECK(as && space && physical!=(PADDR)-1);selected_root=aspace_root(as);
    CHECK(aspace_map_page(as,va,physical,ASPACE_PROT_USER|ASPACE_PROT_WRITE));
    /* Existing image setup is modeled; validation/update/rollback/reclamation
     * below uses actual address-space/KASPACE implementation. */
#if defined(BORROW_GENERIC)
    model_leaf(as->root,va,NULL,NULL)->flags|=ASPACE_LEAF_OWNED;
#else
    *x86_64_user_pte(as->pml4_phys,va)|=X86_64_PTE_OWNED;
#endif
    as->nowned++;
    CHECK(kaspace_lease_acquire(space,va,16,ASPACE_PROT_USER|ASPACE_PROT_WRITE,&old)==E_OK);
    change.address=va;change.bytes=TEST_PAGE;change.rights=ASPACE_PROT_USER|ASPACE_PROT_EXEC;
    count=metadata_live;
    for(i=1;i<=3;i++) {
        fail_metadata=allocation_call+i;
        CHECK(kaspace_permission_prepare(space,&change,1,&tx)==ENSMEM && !tx);
        reset_failure();CHECK(metadata_live==count && kaspace_lease_valid(old));
        CHECK(aspace_translate(as,va,&actual,&rights) && actual==physical && rights==(ASPACE_PROT_USER|ASPACE_PROT_WRITE));
    }
    CHECK(kaspace_permission_prepare(space,&change,1,&tx)==E_OK && tx && !kaspace_lease_valid(old));
    CHECK(aspace_translate(as,va,&actual,&rights) && actual==physical && rights==change.rights);
    kaspace_permission_finish(&tx,FALSE);
    CHECK(!tx && kaspace_lease_valid(old) && metadata_live==count);
    CHECK(aspace_translate(as,va,&actual,&rights) && actual==physical && rights==(ASPACE_PROT_USER|ASPACE_PROT_WRITE));
    CHECK(kaspace_permission_prepare(space,&change,1,&tx)==E_OK);
    kaspace_lease_release(&old);
    CHECK(kaspace_lease_acquire(space,va,16,ASPACE_PROT_USER,&replacement)==E_OK);
    CHECK(kaspace_mapping_change(as,va,16) && !kaspace_lease_valid(replacement));
    kaspace_permission_finish(&tx,FALSE); /* never dereferences/revives a recycled lease */
    CHECK(!kaspace_lease_valid(replacement));kaspace_lease_release(&replacement);
    CHECK(kaspace_permission_prepare(space,&change,1,&tx)==E_OK);
    kaspace_permission_finish(&tx,TRUE);
    CHECK(aspace_translate(as,va,&actual,&rights) && actual==physical && rights==change.rights);
    CHECK(kaspace_lease_acquire(space,va,16,ASPACE_PROT_USER,&old)==E_OK && kaspace_lease_hold_private(old));
    change.rights=ASPACE_PROT_USER|ASPACE_PROT_WRITE;
    CHECK(kaspace_permission_prepare(space,&change,1,&tx)==EACCDN && !tx && kaspace_lease_valid(old));
    kaspace_lease_release(&old);CHECK(aspace_unmap_private(as,va,TEST_PAGE));kaspace_put(space);
    puts("Production loader permissions: allocation prefix rollback, exact PTE/lease restore, stale lease reuse, commit and foreign-holder refusal: PASS (host MMU)");
}
#endif
static void duplicate_borrow_reference(void)
{
    ASPACE *as=aspace_create();KASPACE *space=kaspace_adopt(as);
    KASPACE_LEASE *lease=NULL;void *p=procmem_alloc(TEST_PAGE,0);
    VADDR va=(VADDR)(uintptr_t)p;ULONG i;
    CHECK(as && space && p);selected_root=aspace_root(as);
    for(i=0;i<4;i++) CHECK(aspace_map_procmem(as,va,TEST_PAGE,ASPACE_PROT_USER|ASPACE_PROT_WRITE));
    CHECK(kaspace_lease_acquire(space,va,16,ASPACE_PROT_USER|ASPACE_PROT_WRITE,&lease)==E_OK);
    CHECK(kaspace_lease_seal(&lease,1));
    remember(as,p,NULL);fail_metadata=allocation_call+1;
    CHECK(!aspace_duplicate_borrow(as,va,TEST_PAGE));reset_failure();unchanged(as,p,NULL);
    CHECK(aspace_duplicate_borrow(as,va,TEST_PAGE) && pins(p)==5 && kaspace_lease_valid(lease));
    kaspace_lease_release(&lease);
    for(i=0;i<5;i++) aspace_unborrow(as,va,TEST_PAGE);
    CHECK(!pins(p) && procmem_free(p));kaspace_put(space);
}

int main(void)
{
    window_phys=(PADDR)(uintptr_t)window;free_pages=WINDOW_PAGES;
    failed_ownerless();overlapping_attempts();table_failures();successful_retirement();duplicate_borrow_reference();
#if defined(TEST_SHARED_PERMISSIONS)
    shared_permission_transactions();
#endif
    CHECK(!metadata_live && !physical_live && !live_allocs && free_pages==WINDOW_PAGES);
    puts("Production borrow rollback: pre-pin/map/prefix/table OOM, aliases/permissions, stale-free prevention, retries, ownerless cleanup and final hold/pin reclamation: PASS (hosted MMU providers)");
    return 0;
}
